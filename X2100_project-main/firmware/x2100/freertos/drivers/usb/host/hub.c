#include <spinlock.h>
#include <os.h>

#include "usb.h"
#include "hcd.h"
#include "hub.h"

/* Protect struct usb_device->state and ->children members
 * Note: Both are also protected by ->mutex, except that ->state can
 * change to USB_STATE_NOTATTACHED even when the semaphore isn't held. */
static DEFINE_SPINLOCK(device_state_lock);

/* khubd's worklist and its lock */
static DEFINE_SPINLOCK(hub_event_lock);
static LIST_HEAD(hub_event_list);	/* List of hubs needing servicing */

/* workqueue to process tt clear_work */
static struct workqueue_struct hub_wq;

/* Wakes up khubd */
static DEFINE_THREAD_WAITER(khubd_wait);

static unsigned int hub_thread_active;
static DEFINE_THREAD_WAITER(khubd_exited);

static DEFINE_MUTEX(usb_address0_sem);

/* Note that hdev or one of its children must be locked! */
static inline struct usb_hub *hdev_to_hub(struct usb_device *hdev)
{
	return usb_get_intfdata(hdev->actconfig->interface[0]);
}

/* USB 2.0 spec Section 11.24.4.5 */
static int get_hub_descriptor(struct usb_device *hdev, void *data, int size)
{
	int i, ret;

	for (i = 0; i < 3; i++) {
		ret = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN | USB_RT_HUB,
			USB_DT_HUB << 8, 0, data, size,
			USB_CTRL_GET_TIMEOUT);
		if (ret >= (USB_DT_HUB_NONVAR_SIZE + 2))
			return ret;
	}
	return -EINVAL;
}

/*
 * USB 2.0 spec Section 11.24.2.1
 */
static int clear_hub_feature(struct usb_device *hdev, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_HUB, feature, 0, NULL, 0, 1000);
}

/*
 * USB 2.0 spec Section 11.24.2.2
 */
static int clear_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, 1000);
}

/*
 * USB 2.0 spec Section 11.24.2.13
 */
static int set_port_feature(struct usb_device *hdev, int port1, int feature)
{
	return usb_control_msg(hdev, usb_sndctrlpipe(hdev, 0),
		USB_REQ_SET_FEATURE, USB_RT_PORT, feature, port1,
		NULL, 0, 1000);
}

/* use a short timeout for hub/port status fetches */
#define	USB_STS_TIMEOUT		1000
#define	USB_STS_RETRIES		5

/*
 * USB 2.0 spec Section 11.24.2.6
 */
static int get_hub_status(struct usb_device *hdev,
		struct usb_hub_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES && status == -ETIMEDOUT; i++) {
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_HUB, 0, 0,
			data, sizeof(*data), USB_STS_TIMEOUT);
	}
	return status;
}

/*
 * USB 2.0 spec Section 11.24.2.7
 */
static int get_port_status(struct usb_device *hdev, int port1,
		struct usb_port_status *data)
{
	int i, status = -ETIMEDOUT;

	for (i = 0; i < USB_STS_RETRIES && status == -ETIMEDOUT; i++) {
		status = usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			USB_REQ_GET_STATUS, USB_DIR_IN | USB_RT_PORT, 0, port1,
			data, sizeof(*data), USB_STS_TIMEOUT);
	}
	return status;
}

static void kick_khubd(struct usb_hub *hub)
{
	unsigned long	flags;

	usb_spin_lock_irqsave(&hub_event_lock, flags);
	if (list_empty(&hub->event_list)) {
		list_add_tail(&hub->event_list, &hub_event_list);
		thread_waiter_wakeup(&khubd_wait);
	}
	usb_spin_unlock_irqrestore(&hub_event_lock, flags);
}

void usb_kick_khubd(struct usb_device *hdev)
{
	kick_khubd(hdev_to_hub(hdev));
}

/* completion function, fires on port status changes and various faults */
static void hub_irq(struct urb *urb)
{
	struct usb_hub *hub = (struct usb_hub *)urb->context;
	int status;
	int i;
	unsigned long bits;

	switch (urb->status) {
	case -ENOENT:		/* synchronous unlink */
	case -ECONNRESET:	/* async unlink */
	case -ESHUTDOWN:	/* hardware going away */
		return;

	default:		/* presumably an error */
		/* Cause a hub reset after 10 consecutive errors */
		if ((++hub->nerrors < 10) || hub->error)
			goto resubmit;
		hub->error = urb->status;
		/* FALL THROUGH */
	
	/* let khubd handle things */
	case 0:			/* we got data:  port status changed */
		bits = 0;
		for (i = 0; i < urb->actual_length; ++i)
			bits |= ((unsigned long) ((*hub->buffer)[i]))
					<< (i*8);
		hub->event_bits[0] = bits;
		break;
	}

	hub->nerrors = 0;

	/* Something happened, let khubd figure it out */
	kick_khubd(hub);

resubmit:
	if (hub->quiescing)
		return;

	if ((status = usb_submit_urb (hub->urb)) != 0
			&& status != -ENODEV && status != -EPERM)
		printf ("%s: resubmit --> %d\n", __func__, status);
}

/* USB 2.0 spec Section 11.24.2.3 */
static inline int
hub_clear_tt_buffer (struct usb_device *hdev, u16 devinfo, u16 tt)
{
	return usb_control_msg(hdev, usb_rcvctrlpipe(hdev, 0),
			       HUB_CLEAR_TT_BUFFER, USB_RT_PORT, devinfo,
			       tt, NULL, 0, 1000);
}

/*
 * enumeration blocks khubd for a long time. we use keventd instead, since
 * long blocking there is the exception, not the rule.  accordingly, HCDs
 * talking to TTs must queue control transfers (not just bulk and iso), so
 * both can talk to the same hub concurrently.
 */
static void hub_tt_work (void *arg)
{
	struct usb_hub		*hub = arg;
	unsigned long		flags;

	usb_spin_lock_irqsave (&hub->tt.lock, flags);
	while (!list_empty (&hub->tt.clear_list)) {
		struct list_head	*temp;
		struct usb_tt_clear	*clear;
		struct usb_device	*hdev = hub->hdev;
		const struct hc_driver	*drv;
		int			status;

		temp = hub->tt.clear_list.next;
		clear = list_entry (temp, struct usb_tt_clear, clear_list);
		list_del (&clear->clear_list);

		/* drop lock so HCD can concurrently report other TT errors */
		usb_spin_unlock_irqrestore (&hub->tt.lock, flags);
		status = hub_clear_tt_buffer (hdev, clear->devinfo, clear->tt);
		if (status)
			printf ("clear tt %d (%04x) error %d\n",
				clear->tt, clear->devinfo, status);

		/* Tell the HCD, even if the operation failed */
		drv = clear->hcd->driver;
		if (drv->clear_tt_buffer_complete)
			(drv->clear_tt_buffer_complete)(clear->hcd, clear->ep);

		free (clear);
		usb_spin_lock_irqsave (&hub->tt.lock, flags);

	}
	usb_spin_unlock_irqrestore (&hub->tt.lock, flags);
}

/**
 * usb_hub_clear_tt_buffer - clear control/bulk TT state in high speed hub
 * @udev: the device whose split transaction failed
 * @pipe: identifies the endpoint of the failed transaction
 *
 * High speed HCDs use this to tell the hub driver that some split control or
 * bulk transaction failed in a way that requires clearing internal state of
 * a transaction translator.  This is normally detected (and reported) from
 * interrupt context.
 *
 * It may not be possible for that hub to handle additional full (or low)
 * speed transactions until that state is fully cleared out.
 */
int usb_hub_clear_tt_buffer(struct urb *urb)
{
	struct usb_device	*udev = urb->dev;
	int			pipe = urb->pipe;
	struct usb_tt		*tt = udev->tt;
	unsigned long		flags;
	struct usb_tt_clear	*clear;

	/* we've got to cope with an arbitrary number of pending TT clears,
	 * since each TT has "at least two" buffers that can need it (and
	 * there can be many TTs per hub).  even if they're uncommon.
	 */
	if ((clear = malloc (sizeof *clear)) == NULL) {
		printf ("devnum %d: can't save CLEAR_TT_BUFFER state\n", udev->devnum);
		/* FIXME recover somehow ... RESET_TT? */
		return -ENOMEM;
	}

	/* info that CLEAR_TT_BUFFER needs */
	clear->tt = tt->multi ? udev->ttport : 1;
	clear->devinfo = usb_pipeendpoint (pipe);
	clear->devinfo |= udev->devnum << 4;
	clear->devinfo |= usb_pipecontrol (pipe)
			? (USB_ENDPOINT_XFER_CONTROL << 11)
			: (USB_ENDPOINT_XFER_BULK << 11);
	if (usb_pipein (pipe))
		clear->devinfo |= 1 << 15;
	
	/* info for completion callback */
	clear->hcd = bus_to_hcd(udev->bus);
	clear->ep = urb->ep;

	/* tell keventd to clear state for this TT */
	usb_spin_lock_irqsave (&tt->lock, flags);
	list_add_tail (&clear->clear_list, &tt->clear_list);
	queue_work(&hub_wq, &tt->clear_work);
	usb_spin_unlock_irqrestore (&tt->lock, flags);

	return 0;
}

static void hub_power_on(struct usb_hub *hub)
{
	int port1;
	unsigned pgood_delay = hub->descriptor->bPwrOn2PwrGood * 2;

	/* if hub supports power switching, enable power on each port */
	if ((hub->descriptor->wHubCharacteristics & HUB_CHAR_LPSM) < 2) {
		for (port1 = 1; port1 <= hub->descriptor->bNbrPorts; port1++)
			set_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_POWER);
	}

	/* Wait at least 100 msec for power to become stable */
	msleep(max(pgood_delay, (unsigned) 100));
}

static void hub_quiesce(struct usb_hub *hub)
{
	/* stop khubd and related activity */
	hub->quiescing = 1;
	usb_kill_urb(hub->urb);

	if (hub->tt.hub) {
		while (work_pending(&hub->tt.clear_work))
			msleep(100);
	}
}

static void hub_activate(struct usb_hub *hub)
{
	int	status;

	hub->quiescing = 0;
	hub->activating = 1;
	status = usb_submit_urb(hub->urb);
	if (status < 0)
		printf("%s: activate --> %d\n", __func__, status);

	/* scan all ports ASAP */
	kick_khubd(hub);
}

static int hub_hub_status(struct usb_hub *hub,
		u16 *status, u16 *change)
{
	int ret;

	mutex_lock(&hub->status_mutex);
	ret = get_hub_status(hub->hdev, &hub->status->hub);
	if (ret < 0)
		printf ("%s failed (err = %d)\n", __FUNCTION__, ret);
	else {
		*status = hub->status->hub.wHubStatus;
		*change = hub->status->hub.wHubChange; 
		ret = 0;
	}
	mutex_unlock(&hub->status_mutex);
	return ret;
}

static int hub_configure(struct usb_hub *hub,
	struct usb_endpoint_descriptor *endpoint)
{
	struct usb_device *hdev = hub->hdev;
	u16 hubstatus, hubchange;
	unsigned int pipe;
	int maxp, ret;
	char *message;

	hub->buffer = usb_alloc_coherent(hdev, sizeof(*hub->buffer), &hub->buffer_dma);
	if (!hub->buffer) {
		message = "can't allocate hub irq buffer";
		ret = -ENOMEM;
		goto fail;
	}

	hub->status = cache_align_malloc(sizeof(*hub->status));
	if (!hub->status) {
		message = "can't malloc hub status buffer";
		ret = -ENOMEM;
		goto fail;
	}
	mutex_init(&hub->status_mutex);

	hub->descriptor = cache_align_malloc(sizeof(*hub->descriptor));
	if (!hub->descriptor) {
		message = "can't malloc hub descriptor";
		ret = -ENOMEM;
		goto fail;
	}

	/* Request the entire hub descriptor.
	 * hub->descriptor can handle USB_MAXCHILDREN ports,
	 * but the hub can/will return fewer bytes here.
	 */
	ret = get_hub_descriptor(hdev, hub->descriptor,
			sizeof(*hub->descriptor));
	if (ret < 0) {
		message = "can't read hub descriptor";
		goto fail;
	} else if (hub->descriptor->bNbrPorts > USB_MAXCHILDREN) {
		message = "hub has too many ports!";
		ret = -ENODEV;
		goto fail;
	}

	hdev->maxchild = hub->descriptor->bNbrPorts;
	printf ("devnum %d: %d port%s detected\n", hdev->devnum, hdev->maxchild,
		(hdev->maxchild == 1) ? "" : "s");

	spin_lock_init (&hub->tt.lock);
	INIT_LIST_HEAD (&hub->tt.clear_list);
	init_work_struct(&hub->tt.clear_work, hub_tt_work, hub);

	switch (hdev->descriptor.bDeviceProtocol) {
		case 0:
			break;
		case 1:
			hub->tt.hub = hdev;
			break;
		case 2:
			ret = usb_set_interface(hdev, 0, 1);
			if (ret == 0) {
				hub->tt.multi = 1;
			} else
				printf("Using single TT (err %d)\n", ret);
			hub->tt.hub = hdev;
			break;
		default:
			printf("Unrecognized hub protocol %d\n",
				hdev->descriptor.bDeviceProtocol);
			break;
	}

	/* power budgeting mostly matters with bus-powered hubs,
	 * and battery-powered root hubs (may provide just 8 mA).
	 */
	ret = usb_get_status(hdev, USB_RECIP_DEVICE, 0, &hubstatus);
	if (ret < 0) {
		message = "can't get hub status";
		goto fail;
	}
	if ((hubstatus & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
		hub->power_budget = (501 - hub->descriptor->bHubContrCurrent) / 2;
	}


	ret = hub_hub_status(hub, &hubstatus, &hubchange);
	if (ret < 0) {
		message = "can't get hub status";
		goto fail;
	}

	/* set up the interrupt endpoint */
	pipe = usb_rcvintpipe(hdev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(hdev, pipe, usb_pipeout(pipe));

	if (maxp > sizeof(*hub->buffer))
		maxp = sizeof(*hub->buffer);

	hub->urb = usb_alloc_urb(0);
	if (!hub->urb) {
		message = "couldn't allocate interrupt urb";
		ret = -ENOMEM;
		goto fail;
	}

	usb_fill_int_urb(hub->urb, hdev, pipe, *hub->buffer, maxp, hub_irq,
		hub, endpoint->bInterval);
	hub->urb->transfer_dma = hub->buffer_dma;
	hub->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	hub_power_on(hub);
	hub_activate(hub);
	return 0;

fail:
	printf("devnum %d: config failed, %s (err %d)\n",
			hdev->devnum, message, ret);
	/* hub_disconnect() frees urb and descriptor */
	return ret;
}

static void hub_disconnect(struct usb_interface *intf)
{
	struct usb_hub *hub = usb_get_intfdata (intf);
	struct usb_device *hdev;

	if (!hub)
		return;
	hdev = hub->hdev;

	usb_set_intfdata (intf, NULL);

	hub_quiesce(hub);
	usb_free_urb(hub->urb);
	hub->urb = NULL;

	usb_spin_lock_irq(&hub_event_lock);
	list_del_init(&hub->event_list);
	usb_spin_unlock_irq(&hub_event_lock);

	if (hub->descriptor) {
		free(hub->descriptor);
		hub->descriptor = NULL;
	}

	if (hub->status) {
		free(hub->status);
		hub->status = NULL;
	}

	if (hub->buffer) {
		usb_free_coherent(hdev, sizeof(*hub->buffer), hub->buffer);
		hub->buffer = NULL;
	}

	/* Free the memory */
	free(hub);
}

static int hub_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_host_interface *desc;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_device *hdev;
	struct usb_hub *hub;

	desc = intf->cur_altsetting;
	hdev = interface_to_usbdev(intf);

	/* Some hubs have a subclass of 1, which AFAICT according to the */
	/*  specs is not defined, but it works */
	if ((desc->desc.bInterfaceSubClass != 0) &&
	    (desc->desc.bInterfaceSubClass != 1)) {
descriptor_error:
		printf ("devnum %d: bad descriptor, ignoring hub\n", hdev->devnum);
		return -EIO;
	}

	/* Multiple endpoints? What kind of mutant ninja-hub is this? */
	if (desc->desc.bNumEndpoints != 1)
		goto descriptor_error;

	endpoint = &desc->endpoint[0].desc;

	/* Output endpoint? Curiouser and curiouser.. */
	if (!(endpoint->bEndpointAddress & USB_DIR_IN))
		goto descriptor_error;

	/* If it's not an interrupt endpoint, we'd better punt! */
	if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			!= USB_ENDPOINT_XFER_INT)
		goto descriptor_error;

	/* We found a hub */
	printf ("devnum %d: USB hub found\n", hdev->devnum);

	hub = malloc(sizeof(*hub));
	if (!hub)
		return -ENOMEM;

	memset(hub, 0, sizeof(*hub));

	INIT_LIST_HEAD(&hub->event_list);
	hub->intf = intf;
	hub->hdev = hdev;

	usb_set_intfdata (intf, hub);

	if (hub_configure(hub, endpoint) >= 0)
		return 0;

	hub_disconnect (intf);
	return -ENODEV;
}

/* caller has locked the hub device */
static void hub_pre_reset(struct usb_hub *hub)
{
	struct usb_device *hdev = hub->hdev;
	int i;

	for (i = 0; i < hdev->maxchild; ++i) {
		if (hdev->children[i])
			usb_disconnect(&hdev->children[i]);
	}
	hub_quiesce(hub);
}

/* caller has locked the hub device */
static void hub_post_reset(struct usb_hub *hub)
{
	hub_activate(hub);
	hub_power_on(hub);
}


/* grab device/port lock, returning index of that port (zero based).
 * protects the upstream link used by this device from concurrent
 * tree operations like suspend, resume, reset, and disconnect, which
 * apply to everything downstream of a given port.
 */
static int locktree(struct usb_device *udev)
{
	int			t;
	struct usb_device	*hdev;

	if (!udev)
		return -ENODEV;

	/* root hub is always the first lock in the series */
	hdev = udev->parent;
	if (!hdev) {
		usb_lock_device(udev);
		return 0;
	}

	/* on the path from root to us, lock everything from
	 * top down, dropping parent locks when not needed
	 */
	t = locktree(hdev);
	if (t < 0)
		return t;
	for (t = 0; t < hdev->maxchild; t++) {
		if (hdev->children[t] == udev) {
			/* everything is fail-fast once disconnect
			 * processing starts
			 */
			if (udev->state == USB_STATE_NOTATTACHED)
				break;

			/* when everyone grabs locks top->bottom,
			 * non-overlapping work may be concurrent
			 */
			mutex_lock(&udev->mutex);
			mutex_unlock(&hdev->mutex);
			return t + 1;
		}
	}
	usb_unlock_device(hdev);
	return -ENODEV;
}

static void recursively_mark_NOTATTACHED(struct usb_device *udev)
{
	int i;

	for (i = 0; i < udev->maxchild; ++i) {
		if (udev->children[i])
			recursively_mark_NOTATTACHED(udev->children[i]);
	}
	udev->state = USB_STATE_NOTATTACHED;
}

/**
 * usb_set_device_state - change a device's current state (usbcore, hcds)
 * @udev: pointer to device whose state should be changed
 * @new_state: new state value to be stored
 *
 * udev->state is _not_ fully protected by the device lock.  Although
 * most transitions are made only while holding the lock, the state can
 * can change to USB_STATE_NOTATTACHED at almost any time.  This
 * is so that devices can be marked as disconnected as soon as possible,
 * without having to wait for any semaphores to be released.  As a result,
 * all changes to any device's state must be protected by the
 * device_state_lock spinlock.
 *
 * Once a device has been added to the device tree, all changes to its state
 * should be made using this routine.  The state should _not_ be set directly.
 *
 * If udev->state is already USB_STATE_NOTATTACHED then no change is made.
 * Otherwise udev->state is set to new_state, and if new_state is
 * USB_STATE_NOTATTACHED then all of udev's descendants' states are also set
 * to USB_STATE_NOTATTACHED.
 */
void usb_set_device_state(struct usb_device *udev,
		enum usb_device_state new_state)
{
	unsigned long flags;

	usb_spin_lock_irqsave(&device_state_lock, flags);
	if (udev->state == USB_STATE_NOTATTACHED)
		;	/* do nothing */
	else if (new_state != USB_STATE_NOTATTACHED)
		udev->state = new_state;
	else
		recursively_mark_NOTATTACHED(udev);
	usb_spin_unlock_irqrestore(&device_state_lock, flags);
}


static void choose_address(struct usb_device *udev)
{
	int		devnum;
	struct usb_bus	*bus = udev->bus;

	/* If khubd ever becomes multithreaded, this will need a lock */

	/* Try to allocate the next devnum beginning at bus->devnum_next. */
	devnum = find_next_zero_bit(bus->devmap.devicemap, 128,
			bus->devnum_next);
	if (devnum >= 128)
		devnum = find_next_zero_bit(bus->devmap.devicemap, 128, 1);

	bus->devnum_next = ( devnum >= 127 ? 1 : devnum + 1);

	if (devnum < 128) {
		set_bit(devnum, bus->devmap.devicemap);
		udev->devnum = devnum;
	}
}

static void release_address(struct usb_device *udev)
{
	if (udev->devnum > 0) {
		clear_bit(udev->devnum, udev->bus->devmap.devicemap);
		udev->devnum = -1;
	}
}

/**
 * usb_disconnect - disconnect a device (usbcore-internal)
 * @pdev: pointer to device being disconnected
 * Context: !in_interrupt ()
 *
 * Something got disconnected. Get rid of it and all of its children.
 *
 * If *pdev is a normal device then the parent hub must already be locked.
 * If *pdev is a root hub then this routine will acquire the
 * usb_bus_list_lock on behalf of the caller.
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 */
void usb_disconnect(struct usb_device **pdev)
{
	struct usb_device	*udev = *pdev;
	int			i;

	if (!udev)
		return;

	/* mark the device as inactive, so any further urb submissions for
	 * this device (and any of its children) will fail immediately.
	 * this quiesces everyting except pending urbs.
	 */
	usb_set_device_state(udev, USB_STATE_NOTATTACHED);

	/* lock the bus list on behalf of HCDs unregistering their root hubs */
	if (!udev->parent) {
		mutex_lock(&usb_bus_list_lock);
		usb_lock_device(udev);
	} else {
		mutex_lock(&udev->mutex);
	}

	printf ("USB disconnect, address %d\n", udev->devnum);

	/* Free up all the children before we remove this device */
	for (i = 0; i < USB_MAXCHILDREN; i++) {
		if (udev->children[i])
			usb_disconnect(&udev->children[i]);
	}

	/* deallocate hcd/hardware state ... nuking all pending urbs and
	 * cleaning up all state associated with the current configuration
	 * so that the hardware is now fully quiesced.
	 */
	usb_disable_device(udev, 0);

	/* Free the device number, remove the /proc/bus/usb entry and
	 * the sysfs attributes, and delete the parent's children[]
	 * (or root_hub) pointer.
	 */
	release_address(udev);

	/* Avoid races with recursively_mark_NOTATTACHED() */
	usb_spin_lock_irq(&device_state_lock);
	*pdev = NULL;
	usb_spin_unlock_irq(&device_state_lock);

	if (udev->drvwrap) {
		assert(udev->drvwrap->device_remove);
		udev->drvwrap->device_remove(udev, udev->drvwrap->private_data);
		udev->drvwrap = NULL;
	}

	if (!udev->parent) {
		usb_unlock_device(udev);
		mutex_unlock(&usb_bus_list_lock);
	} else {
		mutex_unlock(&udev->mutex);
	}

	usb_release_dev(udev);
}

static void get_string(struct usb_device *udev, char **string, int index)
{
	char *buf;

	if (!index)
		return;
	buf = malloc(256);
	if (!buf)
		return;
	if (usb_string(udev, index, buf, 256) > 0)
		*string = buf;
	else
		free(buf);
}

/**
 * usb_new_device - perform initial device setup (usbcore-internal)
 * @udev: newly addressed device (in ADDRESS state)
 *
 * This is called with devices which have been enumerated, but not yet
 * configured.  The device descriptor is available, but not descriptors
 * for any device configuration.  The caller must have locked udev and
 * either the parent hub (if udev is a normal device) or else the
 * usb_bus_list_lock (if udev is a root hub).  The parent's pointer to
 * udev has already been installed, but udev is not yet visible through
 * sysfs or other filesystem code.
 *
 * Returns 0 for success (device is configured and listed, with its
 * interfaces, in sysfs); else a negative errno value.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Only the hub driver should ever call this; root hub registration
 * uses it indirectly.
 */
int usb_new_device(struct usb_device *udev)
{
	struct usbdrv_wrap *drvwrap;
	int err;

	err = usb_get_configuration(udev);
	if (err < 0) {
		printf("devnum %d: can't read configurations, error %d\n", udev->devnum, err);
		goto fail;
	}

	/* read the standard strings and cache them if present */
	get_string(udev, &udev->product, udev->descriptor.iProduct);
	get_string(udev, &udev->manufacturer, udev->descriptor.iManufacturer);
	get_string(udev, &udev->serial, udev->descriptor.iSerialNumber);

	printf("New USB device devnum %d, busnum %d\n", udev->devnum, udev->bus->busnum);
	printf("New USB device found, idVendor=%04x, idProduct=%04x\n",
			udev->descriptor.idVendor, udev->descriptor.idProduct);
	printf("New USB device strings: Mfr=%d, Product=%d, SerialNumber=%d\n",
		udev->descriptor.iManufacturer, udev->descriptor.iProduct, udev->descriptor.iSerialNumber);
	printf("Product: %s\n", udev->product);
	printf("Manufacturer: %s\n", udev->manufacturer);
	printf("SerialNumber: %s\n", udev->serial);

	err = -ENODEV;
	/* match driver */
	list_for_each_entry(drvwrap, &usb_driver_list, list) {
		if (usb_device_match(udev, drvwrap)) {
			assert(drvwrap->device_probe);
			err = drvwrap->device_probe(udev, drvwrap->private_data);
			if (err)
				continue;

			udev->drvwrap = drvwrap;
			return 0;
		}
	}

fail:
	usb_set_device_state(udev, USB_STATE_NOTATTACHED);
	return err;
}


static int hub_port_status(struct usb_hub *hub, int port1,
			       u16 *status, u16 *change)
{
	int ret;

	mutex_lock(&hub->status_mutex);
	ret = get_port_status(hub->hdev, port1, &hub->status->port);
	if (ret < 0)
		printf ("%s failed (err = %d)\n", __FUNCTION__, ret);
	else {
		*status = hub->status->port.wPortStatus;
		*change = hub->status->port.wPortChange; 
		ret = 0;
	}
	mutex_unlock(&hub->status_mutex);
	return ret;
}

#define PORT_RESET_TRIES	5
#define SET_ADDRESS_TRIES	2
#define GET_DESCRIPTOR_TRIES	2
#define SET_CONFIG_TRIES	(2 * (1 + 1))
#define USE_NEW_SCHEME(i)	((i) / 2 == 0)

#define HUB_ROOT_RESET_TIME	50	/* times are in msec */
#define HUB_SHORT_RESET_TIME	10
#define HUB_LONG_RESET_TIME	200
#define HUB_RESET_TIMEOUT	500

static int hub_port_wait_reset(struct usb_hub *hub, int port1,
				struct usb_device *udev, unsigned int delay)
{
	int delay_time, ret;
	u16 portstatus;
	u16 portchange;

	for (delay_time = 0;
			delay_time < HUB_RESET_TIMEOUT;
			delay_time += delay) {
		/* wait to give the device a chance to reset */
		msleep(delay);

		/* read and decode port status */
		ret = hub_port_status(hub, port1, &portstatus, &portchange);
		if (ret < 0)
			return ret;

		/* Device went away? */
		if (!(portstatus & USB_PORT_STAT_CONNECTION))
			return -ENOTCONN;

		/* bomb out completely if something weird happened */
		if ((portchange & USB_PORT_STAT_C_CONNECTION))
			return -EINVAL;

		/* if we`ve finished resetting, then break out of the loop */
		if (!(portstatus & USB_PORT_STAT_RESET) &&
		    (portstatus & USB_PORT_STAT_ENABLE)) {
			if (portstatus & USB_PORT_STAT_HIGH_SPEED)
				udev->speed = USB_SPEED_HIGH;
			else if (portstatus & USB_PORT_STAT_LOW_SPEED)
				udev->speed = USB_SPEED_LOW;
			else
				udev->speed = USB_SPEED_FULL;
			return 0;
		}

		/* switch to the long delay after two short delay failures */
		if (delay_time >= 2 * HUB_SHORT_RESET_TIME)
			delay = HUB_LONG_RESET_TIME;
	}

	return -EBUSY;
}

static int hub_port_reset(struct usb_hub *hub, int port1,
				struct usb_device *udev, unsigned int delay)
{
	int i, status;

	/* Reset the port */
	for (i = 0; i < PORT_RESET_TRIES; i++) {
		status = set_port_feature(hub->hdev,
				port1, USB_PORT_FEAT_RESET);
		if (status)
			printf("%s: cannot reset port %d (err = %d)\n",
					__func__, port1, status);
		else {
			status = hub_port_wait_reset(hub, port1, udev, delay);
		}

		/* return on disconnect or reset */
		switch (status) {
		case 0:
			/* TRSTRCY = 10 ms; plus some extra */
			msleep(10 + 40);
			/* FALL THROUGH */
		case -ENOTCONN:
		case -ENODEV:
			clear_port_feature(hub->hdev,
				port1, USB_PORT_FEAT_C_RESET);
			/* FIXME need disconnect() for NOTATTACHED device */
			usb_set_device_state(udev, status
					? USB_STATE_NOTATTACHED
					: USB_STATE_DEFAULT);
			return status;
		}

		delay = HUB_LONG_RESET_TIME;
	}

	printf ("%s: Cannot enable port %i.  Maybe the USB cable is bad?\n",
		__func__, port1);

	return status;
}

static int hub_port_disable(struct usb_hub *hub, int port1, int set_state)
{
	struct usb_device *hdev = hub->hdev;
	int ret;

	if (hdev->children[port1-1] && set_state) {
		usb_set_device_state(hdev->children[port1-1],
				USB_STATE_NOTATTACHED);
	}
	ret = clear_port_feature(hdev, port1, USB_PORT_FEAT_ENABLE);
	if (ret)
		printf("%s: cannot disable port %d (err = %d)\n",
			__func__, port1, ret);

	return ret;
}

/*
 * Disable a port and mark a logical connnect-change event, so that some
 * time later khubd will disconnect() any existing usb_device on the port
 * and will re-enumerate if there actually is a device attached.
 */
static void hub_port_logical_disconnect(struct usb_hub *hub, int port1)
{
	hub_port_disable(hub, port1, 1);

	/* FIXME let caller ask to power down the port:
	 *  - some devices won't enumerate without a VBUS power cycle
	 *  - SRP saves power that way
	 *  - usb_suspend_device(dev,PM_SUSPEND_DISK)
	 * That's easy if this hub can switch power per-port, and
	 * khubd reactivates the port later (timer, SRP, etc).
	 * Powerdown must be optional, because of reset/DFU.
	 */

	set_bit(port1, hub->change_bits);
 	kick_khubd(hub);
}

/* USB 2.0 spec, 7.1.7.3 / fig 7-29:
 *
 * Between connect detection and reset signaling there must be a delay
 * of 100ms at least for debounce and power-settling.  The corresponding
 * timer shall restart whenever the downstream port detects a disconnect.
 * 
 * Apparently there are some bluetooth and irda-dongles and a number of
 * low-speed devices for which this debounce period may last over a second.
 * Not covered by the spec - but easy to deal with.
 *
 * This implementation uses a 1500ms total debounce timeout; if the
 * connection isn't stable by then it returns -ETIMEDOUT.  It checks
 * every 25ms for transient disconnects.  When the port status has been
 * unchanged for 100ms it returns the port status.
 */

#define HUB_DEBOUNCE_TIMEOUT	1500
#define HUB_DEBOUNCE_STEP	  25
#define HUB_DEBOUNCE_STABLE	 100

static int hub_port_debounce(struct usb_hub *hub, int port1)
{
	int ret;
	int total_time, stable_time = 0;
	u16 portchange, portstatus;
	unsigned connection = 0xffff;

	for (total_time = 0; ; total_time += HUB_DEBOUNCE_STEP) {
		ret = hub_port_status(hub, port1, &portstatus, &portchange);
		if (ret < 0)
			return ret;

		if (!(portchange & USB_PORT_STAT_C_CONNECTION) &&
		     (portstatus & USB_PORT_STAT_CONNECTION) == connection) {
			stable_time += HUB_DEBOUNCE_STEP;
			if (stable_time >= HUB_DEBOUNCE_STABLE)
				break;
		} else {
			stable_time = 0;
			connection = portstatus & USB_PORT_STAT_CONNECTION;
		}

		if (portchange & USB_PORT_STAT_C_CONNECTION) {
			clear_port_feature(hub->hdev, port1,
					USB_PORT_FEAT_C_CONNECTION);
		}

		if (total_time >= HUB_DEBOUNCE_TIMEOUT)
			break;
		msleep(HUB_DEBOUNCE_STEP);
	}

	if (stable_time < HUB_DEBOUNCE_STABLE)
		return -ETIMEDOUT;
	return portstatus;
}

static void ep0_reinit(struct usb_device *udev)
{
	usb_disable_endpoint(udev, 0 + USB_DIR_IN);
	usb_disable_endpoint(udev, 0 + USB_DIR_OUT);
	udev->ep_in[0] = udev->ep_out[0] = &udev->ep0;
	udev->ep0.enabled = 1;
}

#define usb_sndaddr0pipe()	(PIPE_CONTROL << 30)
#define usb_rcvaddr0pipe()	((PIPE_CONTROL << 30) | USB_DIR_IN)

static int hub_set_address(struct usb_device *udev)
{
	int retval;

	if (udev->devnum == 0)
		return -EINVAL;
	if (udev->state == USB_STATE_ADDRESS)
		return 0;
	if (udev->state != USB_STATE_DEFAULT)
		return -EINVAL;
	retval = usb_control_msg(udev, usb_sndaddr0pipe(),
		USB_REQ_SET_ADDRESS, 0, udev->devnum, 0,
		NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (retval == 0) {
		usb_set_device_state(udev, USB_STATE_ADDRESS);
		ep0_reinit(udev);
	}
	return retval;
}

/* Reset device, (re)assign address, get device descriptor.
 * Device connection must be stable, no more debouncing needed.
 * Returns device in USB_STATE_ADDRESS, except on error.
 *
 * If this is called for an already-existing device (as part of
 * usb_reset_device), the caller must own the device lock.  For a
 * newly detected device that is not accessible through any global
 * pointers, it's not necessary to lock the device.
 */
static int
hub_port_init (struct usb_hub *hub, struct usb_device *udev, int port1,
		int retry_counter)
{
	struct usb_device	*hdev = hub->hdev;
	int			i, j, retval;
	unsigned		delay = HUB_SHORT_RESET_TIME;
	enum usb_device_speed	oldspeed = udev->speed;

	/* root hub ports have a slightly longer reset period
	 * (from USB 2.0 spec, section 7.1.7.5)
	 */
	if (!hdev->parent)
		delay = HUB_ROOT_RESET_TIME;

	/* Some low speed devices have problems with the quick delay, so */
	/*  be a bit pessimistic with those devices. RHbug #23670 */
	if (oldspeed == USB_SPEED_LOW)
		delay = HUB_LONG_RESET_TIME;

	mutex_lock(&usb_address0_sem);

	/* Reset the device; full speed may morph to high speed */
	retval = hub_port_reset(hub, port1, udev, delay);
	if (retval < 0)		/* error or disconnect */
		goto fail;
				/* success, speed is known */
	retval = -ENODEV;

	if (oldspeed != USB_SPEED_UNKNOWN && oldspeed != udev->speed) {
		goto fail;
	}
	oldspeed = udev->speed;
  
	/* USB 2.0 section 5.5.3 talks about ep0 maxpacket ...
	 * it's fixed size except for full speed devices.
	 */
	switch (udev->speed) {
	case USB_SPEED_HIGH:		/* fixed at 64 */
		udev->ep0.desc.wMaxPacketSize = 64;
		break;
	case USB_SPEED_FULL:		/* 8, 16, 32, or 64 */
		/* to determine the ep0 maxpacket size, try to read
		 * the device descriptor to get bMaxPacketSize0 and
		 * then correct our initial guess.
		 */
		udev->ep0.desc.wMaxPacketSize = 64;
		break;
	case USB_SPEED_LOW:		/* fixed at 8 */
		udev->ep0.desc.wMaxPacketSize = 8;
		break;
	default:
		goto fail;
	}
 
	printf ("%s %s speed USB device using and address %d\n",
			(udev->config) ? "reset" : "new",
			({ char *speed; switch (udev->speed) {
			case USB_SPEED_LOW:	speed = "low";	break;
			case USB_SPEED_FULL:	speed = "full";	break;
			case USB_SPEED_HIGH:	speed = "high";	break;
			default: 		speed = "?";	break;
			}; speed;}),
			udev->devnum);

	/* Set up TT records, if needed  */
	if (hdev->tt) {
		udev->tt = hdev->tt;
		udev->ttport = hdev->ttport;
	} else if (udev->speed != USB_SPEED_HIGH
			&& hdev->speed == USB_SPEED_HIGH) {
		udev->tt = &hub->tt;
		udev->ttport = port1;
	}
 
	/* Why interleave GET_DESCRIPTOR and SET_ADDRESS this way?
	 * Because device hardware and firmware is sometimes buggy in
	 * this area, and this is how Linux has done it for ages.
	 * Change it cautiously.
	 *
	 * NOTE:  If USE_NEW_SCHEME() is true we will start by issuing
	 * a 64-byte GET_DESCRIPTOR request.  This is what Windows does,
	 * so it may help with some non-standards-compliant devices.
	 * Otherwise we start with SET_ADDRESS and then try to read the
	 * first 8 bytes of the device descriptor to get the ep0 maxpacket
	 * value.
	 */
	for (i = 0; i < GET_DESCRIPTOR_TRIES; (++i, msleep(100))) {
		if (USE_NEW_SCHEME(retry_counter)) {
			struct usb_device_descriptor *buf;
			int r = 0;

#define GET_DESCRIPTOR_BUFSIZE	64
			buf = cache_align_malloc(GET_DESCRIPTOR_BUFSIZE);
			if (!buf) {
				retval = -ENOMEM;
				continue;
			}

			/* Retry on all errors; some devices are flakey.
			 * 255 is for WUSB devices, we actually need to use
			 * 512 (WUSB1.0[4.8.1]).
			 */
			for (j = 0; j < 3; ++j) {
				buf->bMaxPacketSize0 = 0;
				r = usb_control_msg(udev, usb_rcvaddr0pipe(),
					USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
					USB_DT_DEVICE << 8, 0,
					buf, GET_DESCRIPTOR_BUFSIZE,
					USB_CTRL_GET_TIMEOUT);
				switch (buf->bMaxPacketSize0) {
				case 8: case 16: case 32: case 64:
					if (buf->bDescriptorType ==
							USB_DT_DEVICE) {
						r = 0;
						break;
					}
					/* FALL THROUGH */
				default:
					if (r == 0)
						r = -EPROTO;
					break;
				}
				if (r == 0)
					break;
			}
			udev->descriptor.bMaxPacketSize0 =
					buf->bMaxPacketSize0;
			free(buf);

			retval = hub_port_reset(hub, port1, udev, delay);
			if (retval < 0)		/* error or disconnect */
				goto fail;
			if (oldspeed != udev->speed) {
				retval = -ENODEV;
				goto fail;
			}
			if (r) {
				printf("devnum %d: device descriptor read/64, error %d\n",
						udev->devnum, r);
				retval = -EMSGSIZE;
				continue;
			}
#undef GET_DESCRIPTOR_BUFSIZE
		}

		for (j = 0; j < SET_ADDRESS_TRIES; ++j) {
			retval = hub_set_address(udev);
			if (retval >= 0)
				break;
			msleep(200);
		}
		if (retval < 0) {
			printf("devnum %d: device not accepting address, error %d\n",
				udev->devnum, retval);
			goto fail;
		}
 
		/* cope with hardware quirkiness:
		 *  - let SET_ADDRESS settle, some device hardware wants it
		 *  - read ep0 maxpacket even for high and low speed,
  		 */
		msleep(10);
		if (USE_NEW_SCHEME(retry_counter))
			break;

		retval = usb_get_device_descriptor(udev, 8);
		if (retval < 8) {
			printf("devnum %d: device descriptor read/8, error %d\n",
					udev->devnum, retval);
			if (retval >= 0)
				retval = -EMSGSIZE;
		} else {
			retval = 0;
			break;
		}
	}
	if (retval)
		goto fail;

	i = udev->descriptor.bMaxPacketSize0;
	if (udev->ep0.desc.wMaxPacketSize != i) {
		if (udev->speed != USB_SPEED_FULL ||
				!(i == 8 || i == 16 || i == 32 || i == 64)) {
			printf("devnum %d: ep0 maxpacket = %d\n", udev->devnum, i);
			retval = -EMSGSIZE;
			goto fail;
		}
		udev->ep0.desc.wMaxPacketSize = i;
		ep0_reinit(udev);
	}
  
	retval = usb_get_device_descriptor(udev, USB_DT_DEVICE_SIZE);
	if (retval < (signed)sizeof(udev->descriptor)) {
		printf("devnum %d: device descriptor read/all, error %d\n",
			udev->devnum, retval);
		if (retval >= 0)
			retval = -ENOMSG;
		goto fail;
	}

	retval = 0;

fail:
	if (retval)
		hub_port_disable(hub, port1, 0);
	mutex_unlock(&usb_address0_sem);
	return retval;
}

static unsigned
hub_power_remaining (struct usb_hub *hub)
{
	struct usb_device *hdev = hub->hdev;
	int remaining;
	unsigned i;

	remaining = hub->power_budget;
	if (!remaining)		/* self-powered */
		return 0;

	for (i = 0; i < hdev->maxchild; i++) {
		struct usb_device	*udev = hdev->children[i];
		int			delta, ceiling;

		if (!udev)
			continue;

		ceiling = 50;

		if (udev->actconfig)
			delta = udev->actconfig->desc.bMaxPower;
		else
			delta = ceiling;
		if (delta > ceiling)
			printf("devnum %d: %dmA over %dmA budget!\n", hdev->devnum,
				2 * (delta - ceiling), 2 * ceiling);
		remaining -= delta;
	}
	if (remaining < 0) {
		printf("%s: %dmA over power budget!\n", __func__, -2 * remaining);
		remaining = 0;
	}
	return remaining;
}

/* Handle physical or logical connection change events.
 * This routine is called when:
 * 	a port connection-change occurs;
 *	a port enable-change occurs (often caused by EMI);
 *	usb_reset_device() encounters changed descriptors (as from
 *		a firmware download)
 * caller already locked the hub
 */
static void hub_port_connect_change(struct usb_hub *hub, int port1,
					u16 portstatus, u16 portchange)
{
	struct usb_device *hdev = hub->hdev;
	int status, i;
 
	/* Disconnect any existing devices under this port */
	if (hdev->children[port1-1])
		usb_disconnect(&hdev->children[port1-1]);
	clear_bit(port1, hub->change_bits);

	if (portchange & USB_PORT_STAT_C_CONNECTION) {
		status = hub_port_debounce(hub, port1);
		if (status < 0) {
			printf ("%s: connect-debounce failed, port %d disabled\n",
				__func__, port1);
			goto done;
		}
		portstatus = status;
	}

	/* Return now if nothing is connected */
	if (!(portstatus & USB_PORT_STAT_CONNECTION)) {

		/* maybe switch power back on (e.g. root hub was reset) */
		if ((hub->descriptor->wHubCharacteristics
					& HUB_CHAR_LPSM) < 2
				&& !(portstatus & (1 << USB_PORT_FEAT_POWER)))
			set_port_feature(hdev, port1, USB_PORT_FEAT_POWER);
 
		if (portstatus & USB_PORT_STAT_ENABLE)
  			goto done;
		return;
	}

	for (i = 0; i < SET_CONFIG_TRIES; i++) {
		struct usb_device *udev;

		/* reallocate for each attempt, since references
		 * to the previous one can escape in various ways
		 */
		udev = usb_alloc_dev(hdev, hdev->bus, port1);
		if (!udev) {
			printf ("%s: couldn't allocate port %d usb_device\n",
				__func__, port1);
			goto done;
		}

		usb_set_device_state(udev, USB_STATE_POWERED);
		udev->speed = USB_SPEED_UNKNOWN;
 
		/* set the address */
		choose_address(udev);
		if (udev->devnum <= 0) {
			status = -ENOTCONN;	/* Don't retry */
			goto loop;
		}

		/* reset and get descriptor */
		status = hub_port_init(hub, udev, port1, i);
		if (status < 0)
			goto loop;

		/* consecutive bus-powered hubs aren't reliable; they can
		 * violate the voltage drop budget.  if the new child has
		 * a "powered" LED, users should notice we didn't enable it
		 * (without reading syslog), even without per-port LEDs
		 * on the parent.
		 */
		if (udev->descriptor.bDeviceClass == USB_CLASS_HUB
				&& hub->power_budget) {
			u16	devstat;

			status = usb_get_status(udev, USB_RECIP_DEVICE, 0,
					&devstat);
			if (status < 0) {
				goto loop_disable;
			}
			if ((devstat & (1 << USB_DEVICE_SELF_POWERED)) == 0) {
				printf("%s: can't connect bus-powered hub to this port\n", __func__);
				status = -ENOTCONN;	/* Don't retry */
				goto loop_disable;
			}
		}

		/* Store the parent's children[] pointer.  At this point
		 * udev becomes globally accessible, although presumably
		 * no one will look at it until hdev is unlocked.
		 */
		mutex_lock (&udev->mutex);
		status = 0;

		/* We mustn't add new devices if the parent hub has
		 * been disconnected; we would race with the
		 * recursively_mark_NOTATTACHED() routine.
		 */
		usb_spin_lock_irq(&device_state_lock);
		if (hdev->state == USB_STATE_NOTATTACHED)
			status = -ENOTCONN;
		else
			hdev->children[port1-1] = udev;
		usb_spin_unlock_irq(&device_state_lock);

		/* Run it through the hoops (find a driver, etc) */
		if (!status) {
			status = usb_new_device(udev);
			if (status) {
				usb_spin_lock_irq(&device_state_lock);
				hdev->children[port1-1] = NULL;
				usb_spin_unlock_irq(&device_state_lock);
			}
		}

		mutex_unlock (&udev->mutex);
		if (status)
			goto loop_disable;

		hub_power_remaining(hub);

		return;

loop_disable:
		hub_port_disable(hub, port1, 1);
loop:
		ep0_reinit(udev);
		release_address(udev);
		usb_free_dev(udev);
		if (status == -ENOTCONN)
			break;
	}
 
done:
	hub_port_disable(hub, port1, 1);
}

static void hub_events(void)
{
	struct list_head *tmp;
	struct usb_device *hdev;
	struct usb_interface *intf;
	struct usb_hub *hub;
	u16 hubstatus;
	u16 hubchange;
	u16 portstatus;
	u16 portchange;
	int i, ret;
	int connect_change;
	int event;

	/*
	 *  We restart the list every time to avoid a deadlock with
	 * deleting hubs downstream from this one. This should be
	 * safe since we delete the hub from the event list.
	 * Not the most efficient, but avoids deadlocks.
	 */
	while (1) {

		/* Grab the first entry at the beginning of the list */
		usb_spin_lock_irq(&hub_event_lock);
		if (list_empty(&hub_event_list)) {
			usb_spin_unlock_irq(&hub_event_lock);
			break;
		}

		tmp = hub_event_list.next;
		list_del_init(tmp);

		hub = list_entry(tmp, struct usb_hub, event_list);
		hdev = hub->hdev;
		intf = hub->intf;

		usb_get_intf(intf);
		usb_spin_unlock_irq(&hub_event_lock);

		/* Lock the device, then check to see if we were
		 * disconnected while waiting for the lock to succeed. */
		if (locktree(hdev) < 0) {
			usb_put_intf(intf);
			continue;
		}
		if (hub != usb_get_intfdata(intf))
			goto loop;

		/* If the hub has died, clean up after it */
		if (hdev->state == USB_STATE_NOTATTACHED) {
			hub_pre_reset(hub);
			goto loop;
		}

		/* If this is an inactive or suspended hub, do nothing */
		if (hub->quiescing)
			goto loop;

		if (hub->error) {
			ret = usb_reset_device(hdev);
			if (ret)
				goto loop;

			hub->nerrors = 0;
			hub->error = 0;
		}

		/* deal with port status changes */
		for (i = 1; i <= hub->descriptor->bNbrPorts; i++) {
			if (test_bit(i, hub->busy_bits))
				continue;
			connect_change = test_bit(i, hub->change_bits);
			event = test_bit(i, hub->event_bits);
			if (event)
				clear_bit(i, hub->event_bits);

			if (!event && !connect_change && !hub->activating)
				continue;

			ret = hub_port_status(hub, i,
					&portstatus, &portchange);
			if (ret < 0)
				continue;

			if (hub->activating && !hdev->children[i-1] &&
					(portstatus &
						USB_PORT_STAT_CONNECTION))
				connect_change = 1;

			if (portchange & USB_PORT_STAT_C_CONNECTION) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_CONNECTION);
				connect_change = 1;
			}

			if (portchange & USB_PORT_STAT_C_ENABLE) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_ENABLE);

				/*
				 * EM interference sometimes causes badly
				 * shielded USB devices to be shutdown by
				 * the hub, this hack enables them again.
				 * Works at least with mouse driver. 
				 */
				if (!(portstatus & USB_PORT_STAT_ENABLE)
				    && !connect_change
				    && hdev->children[i-1]) {
					printf ("devnum %d: port %i disabled by hub (EMI?), re-enabling...\n",
							hdev->devnum, i);
					connect_change = 1;
				}
			}

			if (portchange & USB_PORT_STAT_C_SUSPEND) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_SUSPEND);
				if (!hdev->children[i-1]) {
					ret = -ENODEV;
					hub_port_disable(hub, i, 1);
				}
			}
			
			if (portchange & USB_PORT_STAT_C_OVERCURRENT) {
				printf ("devnum %d: over-current change on port %d\n", hdev->devnum, i);
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_OVER_CURRENT);
				hub_power_on(hub);
			}

			if (portchange & USB_PORT_STAT_C_RESET) {
				clear_port_feature(hdev, i,
					USB_PORT_FEAT_C_RESET);
			}

			if (connect_change)
				hub_port_connect_change(hub, i,
						portstatus, portchange);
		} /* end for i */

		event = test_bit(i, hub->event_bits);
		if (event)
			clear_bit(i, hub->event_bits);
		/* deal with hub status changes */
		if (event == 0)
			;	/* do nothing */
		else if (hub_hub_status(hub, &hubstatus, &hubchange) < 0)
			printf ("devnum %d: get_hub_status failed\n", hdev->devnum);
		else {
			if (hubchange & HUB_CHANGE_LOCAL_POWER) {
				printf ("devnum %d: power change\n", hdev->devnum);
				clear_hub_feature(hdev, C_HUB_LOCAL_POWER);
			}
			if (hubchange & HUB_CHANGE_OVERCURRENT) {
				printf ("devnum %d: overcurrent change\n", hdev->devnum);
				msleep(500);	/* Cool down */
				clear_hub_feature(hdev, C_HUB_OVER_CURRENT);
                        	hub_power_on(hub);
			}
		}

		hub->activating = 0;

loop:
		usb_unlock_device(hdev);
		usb_put_intf(intf);

        } /* end while (1) */
}

static void hub_thread(void *data)
{
	do {
		hub_events();
		if (list_empty(&hub_event_list))
			thread_waiter_wait(&khubd_wait);
	} while (hub_thread_active);

	thread_waiter_wakeup(&khubd_exited);
}

static struct usb_device_id hub_id_table [] = {
    { .match_flags = USB_DEVICE_ID_MATCH_DEV_CLASS,
      .bDeviceClass = USB_CLASS_HUB},
    { .match_flags = USB_DEVICE_ID_MATCH_INT_CLASS,
      .bInterfaceClass = USB_CLASS_HUB},
    { }						/* Terminating entry */
};

static struct usb_driver hub_driver = {
	.name =		"hub",
	.probe =	hub_probe,
	.disconnect =	hub_disconnect,
	.id_table =	hub_id_table,
};

void usb_hub_init(void)
{
	create_workqueue(&hub_wq);
	usb_register_driver(&hub_driver);
}

void usb_hub_start(void)
{
	hub_thread_active = 1;
	thread_create("usb_hub_thread", 16*1024, hub_thread, NULL);
}

void usb_hub_stop(void)
{
	/* wait the hub_thread exit */
	hub_thread_active = 0;
	thread_waiter_wakeup(&khubd_wait);
	thread_waiter_wait(&khubd_exited);
}

void usb_hub_cleanup(void)
{
	usb_deregister(&hub_driver);
	destroy_workqueue(&hub_wq);
}


static int config_descriptors_changed(struct usb_device *udev)
{
	unsigned			index;
	unsigned			len = 0;
	struct usb_config_descriptor	*buf;

	for (index = 0; index < udev->descriptor.bNumConfigurations; index++) {
		if (len < udev->config[index].desc.wTotalLength)
			len = udev->config[index].desc.wTotalLength;
	}
	buf = cache_align_malloc (len);
	if (buf == NULL) {
		printf("devnum %d: no mem to re-read configs after reset\n", udev->devnum);
		/* assume the worst */
		return 1;
	}
	for (index = 0; index < udev->descriptor.bNumConfigurations; index++) {
		int length;
		int old_length = udev->config[index].desc.wTotalLength;

		length = usb_get_descriptor(udev, USB_DT_CONFIG, index, buf, old_length);
		if (length < old_length)
			break;

		if (memcmp (buf, udev->rawdescriptors[index], old_length) != 0)
			break;

	}
	free(buf);
	return index != udev->descriptor.bNumConfigurations;
}

/**
 * usb_reset_device - perform a USB port reset to reinitialize a device
 * @udev: device to reset (not in SUSPENDED or NOTATTACHED state)
 *
 * WARNING - don't reset any device unless drivers for all of its
 * interfaces are expecting that reset!  Maybe some driver->reset()
 * method should eventually help ensure sufficient cooperation.
 *
 * Do a port reset, reassign the device's address, and establish its
 * former operating configuration.  If the reset fails, or the device's
 * descriptors change from their values before the reset, or the original
 * configuration and altsettings cannot be restored, a flag will be set
 * telling khubd to pretend the device has been disconnected and then
 * re-connected.  All drivers will be unbound, and the device will be
 * re-enumerated and probed all over again.
 *
 * Returns 0 if the reset succeeded, -ENODEV if the device has been
 * flagged for logical disconnection, or some other negative error code
 * if the reset wasn't even attempted.
 *
 * The caller must own the device lock.  For example, it's safe to use
 * this from a driver probe() routine after downloading new firmware.
 * For calls that might not occur during probe(), drivers should lock
 * the device using usb_lock_device_for_reset().
 */
int usb_reset_device(struct usb_device *udev)
{
	struct usb_device		*parent_hdev = udev->parent;
	struct usb_hub			*parent_hub;
	struct usb_device_descriptor	descriptor = udev->descriptor;
	struct usb_hub			*hub = NULL;
	int 				i, ret = 0, port1 = -1;

	if (udev->state == USB_STATE_NOTATTACHED ||
			udev->state == USB_STATE_SUSPENDED) {
		return -EINVAL;
	}

	if (!parent_hdev) {
		return -EISDIR;
	}

	for (i = 0; i < parent_hdev->maxchild; i++)
		if (parent_hdev->children[i] == udev) {
			port1 = i + 1;
			break;
		}

	if (port1 < 0) {
		/* If this ever happens, it's very bad */
		printf("devnum %d: Can't locate device's port!\n", udev->devnum);
		return -ENOENT;
	}
	parent_hub = hdev_to_hub(parent_hdev);

	/* If we're resetting an active hub, take some special actions */
	if (udev->actconfig &&
			udev->actconfig->interface[0]->drvwrap ==
				&hub_driver.drvwrap &&
			(hub = hdev_to_hub(udev)) != NULL) {
		hub_pre_reset(hub);
	}

	set_bit(port1, parent_hub->busy_bits);
	for (i = 0; i < SET_CONFIG_TRIES; ++i) {

		/* ep0 maxpacket size may change; let the HCD know about it.
		 * Other endpoints will be handled by re-enumeration. */
		ep0_reinit(udev);
		ret = hub_port_init(parent_hub, udev, port1, i);
		if (ret >= 0)
			break;
	}
	clear_bit(port1, parent_hub->busy_bits);
	if (ret < 0)
		goto re_enumerate;
 
	/* Device might have changed firmware (DFU or similar) */
	if (memcmp(&udev->descriptor, &descriptor, sizeof descriptor)
			|| config_descriptors_changed (udev)) {
		printf("devnum %d: device firmware changed\n", udev->devnum);
		udev->descriptor = descriptor;	/* for disconnect() calls */
		goto re_enumerate;
  	}
  
	if (!udev->actconfig)
		goto done;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			udev->actconfig->desc.bConfigurationValue, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		printf("devnum %d: can't restore configuration #%d (error=%d)\n",
			udev->devnum, udev->actconfig->desc.bConfigurationValue, ret);
		goto re_enumerate;
  	}
	usb_set_device_state(udev, USB_STATE_CONFIGURED);

	for (i = 0; i < udev->actconfig->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = udev->actconfig->interface[i];
		struct usb_interface_descriptor *desc;

		/* set_interface resets host side even
		 * for altsetting zero.  the interface may have no driver.
		 */
		desc = &intf->cur_altsetting->desc;
		ret = usb_set_interface(udev, desc->bInterfaceNumber,
			desc->bAlternateSetting);
		if (ret < 0) {
			printf("devnum %d: failed to restore interface %d altsetting %d (error=%d)\n",
				udev->devnum, desc->bInterfaceNumber, desc->bAlternateSetting, ret);
			goto re_enumerate;
		}
	}

done:
	if (hub)
		hub_post_reset(hub);
	return 0;
 
re_enumerate:
	hub_port_logical_disconnect(parent_hub, port1);
	return -ENODEV;
}
