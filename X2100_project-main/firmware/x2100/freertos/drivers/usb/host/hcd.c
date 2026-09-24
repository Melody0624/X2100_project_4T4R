#include "usb.h"
#include "hcd.h"
#include "hub.h"

/*-------------------------------------------------------------------------*/

/*
 * USB Host Controller Driver framework
 *
 * Plugs into usbcore (usb_bus) and lets HCDs share code, minimizing
 * HCD-specific behaviors/bugs.
 *
 * This does error checks, tracks devices and urbs, and delegates to a
 * "hc_driver" only for code (and data) that really needs to know about
 * hardware differences.  That includes root hub registers, i/o queues,
 * and so on ... but as little else as possible.
 *
 * Shared code includes most of the "root hub" code (these are emulated,
 * though each HC's hardware works differently) and PCI glue, plus request
 * tracking overhead.  The HCD code should only block on spinlocks or on
 * hardware handshaking; blocking on software events (such as other kernel
 * threads releasing resources, or completing actions) is all generic.
 *
 * Happens the USB 2.0 spec says this would be invisible inside the "USBD",
 * and includes mostly a "HCDI" (HCD Interface) along with some APIs used
 * only by the hub driver ... and that neither should be seen or used by
 * usb client device drivers.
 *
 * Contributors of ideas or unattributed patches include: David Brownell,
 * Roman Weissgaerber, Rory Bolt, Greg Kroah-Hartman, ...
 *
 * HISTORY:
 * 2002-02-21	Pull in most of the usb_bus support from usb.c; some
 *		associated cleanup.  "usb_hcd" still != "usb_bus".
 * 2001-12-12	Initial patch version for Linux 2.5.1 kernel.
 */

/*-------------------------------------------------------------------------*/

/* host controllers we manage */
LIST_HEAD (usb_bus_list);

/* used when allocating bus numbers */
#define USB_MAXBUS		64
struct usb_busmap {
	unsigned int busmap [USB_MAXBUS / (8*sizeof (unsigned int))];
};
static struct usb_busmap busmap;

/* used when updating list of hcds */
DEFINE_MUTEX (usb_bus_list_lock);

/* used for controlling access to virtual root hubs */
static DEFINE_SPINLOCK(hcd_root_hub_lock);

/* used when updating hcd data */
static DEFINE_SPINLOCK(hcd_data_lock);

/* wait queue for synchronous unlinks */
DEFINE_THREAD_WAITER(usb_kill_urb_queue);

/*-------------------------------------------------------------------------*/

/* usb 2.0 root hub device descriptor */
static const u8 usb2_rh_dev_descriptor [18] = {
	0x12,       /*  u8  bLength; */
	0x01,       /*  u8  bDescriptorType; Device */
	0x00, 0x02, /*  u16 bcdUSB; v2.0 */

	0x09,	    /*  u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  u8  bDeviceSubClass; */
	0x01,       /*  u8  bDeviceProtocol; [ usb 2.0 single TT ]*/
	0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

	0x00, 0x00, /*  u16 idVendor; */
 	0x00, 0x00, /*  u16 idProduct; */
	0x00, 0x00, /*  u16 bcdDevice */

	0x03,       /*  u8  iManufacturer; */
	0x02,       /*  u8  iProduct; */
	0x01,       /*  u8  iSerialNumber; */
	0x01        /*  u8  bNumConfigurations; */
};

/* no usb 2.0 root hub "device qualifier" descriptor: one speed only */

/* usb 1.1 root hub device descriptor */
static const u8 usb11_rh_dev_descriptor [18] = {
	0x12,       /*  u8  bLength; */
	0x01,       /*  u8  bDescriptorType; Device */
	0x10, 0x01, /*  u16 bcdUSB; v1.1 */

	0x09,	    /*  u8  bDeviceClass; HUB_CLASSCODE */
	0x00,	    /*  u8  bDeviceSubClass; */
	0x00,       /*  u8  bDeviceProtocol; [ low/full speeds only ] */
	0x40,       /*  __u8  bMaxPacketSize0; 64 Bytes */

	0x00, 0x00, /*  u16 idVendor; */
 	0x00, 0x00, /*  u16 idProduct; */
	0x00, 0x00, /*  u16 bcdDevice */

	0x03,       /*  u8  iManufacturer; */
	0x02,       /*  u8  iProduct; */
	0x01,       /*  u8  iSerialNumber; */
	0x01        /*  u8  bNumConfigurations; */
};


/*-------------------------------------------------------------------------*/

/* Configuration descriptors for our root hubs */

static const u8 fs_rh_config_descriptor [] = {

	/* one configuration */
	0x09,       /*  u8  bLength; */
	0x02,       /*  u8  bDescriptorType; Configuration */
	0x19, 0x00, /*  u16 wTotalLength; */
	0x01,       /*  u8  bNumInterfaces; (1) */
	0x01,       /*  u8  bConfigurationValue; */
	0x00,       /*  u8  iConfiguration; */
	0xc0,       /*  u8  bmAttributes; 
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
	0x00,       /*  u8  MaxPower; */
      
	/* USB 1.1:
	 * USB 2.0, single TT organization (mandatory):
	 *	one interface, protocol 0
	 *
	 * USB 2.0, multiple TT organization (optional):
	 *	two interfaces, protocols 1 (like single TT)
	 *	and 2 (multiple TT mode) ... config is
	 *	sometimes settable
	 *	NOT IMPLEMENTED
	 */

	/* one interface */
	0x09,       /*  u8  if_bLength; */
	0x04,       /*  u8  if_bDescriptorType; Interface */
	0x00,       /*  u8  if_bInterfaceNumber; */
	0x00,       /*  u8  if_bAlternateSetting; */
	0x01,       /*  u8  if_bNumEndpoints; */
	0x09,       /*  u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,       /*  u8  if_bInterfaceSubClass; */
	0x00,       /*  u8  if_bInterfaceProtocol; [usb1.1 or single tt] */
	0x00,       /*  u8  if_iInterface; */
     
	/* one endpoint (status change endpoint) */
	0x07,       /*  u8  ep_bLength; */
	0x05,       /*  u8  ep_bDescriptorType; Endpoint */
	0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
 	0x03,       /*  u8  ep_bmAttributes; Interrupt */
 	0x02, 0x00, /*  u16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8) */
	0xff        /*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */
};

static const u8 hs_rh_config_descriptor [] = {

	/* one configuration */
	0x09,       /*  u8  bLength; */
	0x02,       /*  u8  bDescriptorType; Configuration */
	0x19, 0x00, /*  u16 wTotalLength; */
	0x01,       /*  u8  bNumInterfaces; (1) */
	0x01,       /*  u8  bConfigurationValue; */
	0x00,       /*  u8  iConfiguration; */
	0xc0,       /*  u8  bmAttributes; 
				 Bit 7: must be set,
				     6: Self-powered,
				     5: Remote wakeup,
				     4..0: resvd */
	0x00,       /*  u8  MaxPower; */
      
	/* USB 1.1:
	 * USB 2.0, single TT organization (mandatory):
	 *	one interface, protocol 0
	 *
	 * USB 2.0, multiple TT organization (optional):
	 *	two interfaces, protocols 1 (like single TT)
	 *	and 2 (multiple TT mode) ... config is
	 *	sometimes settable
	 *	NOT IMPLEMENTED
	 */

	/* one interface */
	0x09,       /*  u8  if_bLength; */
	0x04,       /*  u8  if_bDescriptorType; Interface */
	0x00,       /*  u8  if_bInterfaceNumber; */
	0x00,       /*  u8  if_bAlternateSetting; */
	0x01,       /*  u8  if_bNumEndpoints; */
	0x09,       /*  u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,       /*  u8  if_bInterfaceSubClass; */
	0x00,       /*  u8  if_bInterfaceProtocol; [usb1.1 or single tt] */
	0x00,       /*  u8  if_iInterface; */
     
	/* one endpoint (status change endpoint) */
	0x07,       /*  u8  ep_bLength; */
	0x05,       /*  u8  ep_bDescriptorType; Endpoint */
	0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
 	0x03,       /*  u8  ep_bmAttributes; Interrupt */
 	0x02, 0x00, /*  u16 ep_wMaxPacketSize; 1 + (MAX_ROOT_PORTS / 8) */
	0x0c        /*  u8  ep_bInterval; (256ms -- usb 2.0 spec) */
};

/*-------------------------------------------------------------------------*/

/*
 * helper routine for returning string descriptors in UTF-16LE
 * input can actually be ISO-8859-1; ASCII is its 7-bit subset
 */
static int ascii2utf (char *s, u8 *utf, int utfmax)
{
	int retval;

	for (retval = 0; *s && utfmax > 1; utfmax -= 2, retval += 2) {
		*utf++ = *s++;
		*utf++ = 0;
	}
	if (utfmax > 0) {
		*utf = *s;
		++retval;
	}
	return retval;
}

/*
 * rh_string - provides manufacturer, product and serial strings for root hub
 * @id: the string ID number (1: serial number, 2: product, 3: vendor)
 * @hcd: the host controller for this root hub
 * @type: string describing our driver 
 * @data: return packet in UTF-16 LE
 * @len: length of the return packet
 *
 * Produces either a manufacturer, product or serial number string for the
 * virtual root hub device.
 */
static int rh_string (
	int		id,
	struct usb_hcd	*hcd,
	u8		*data,
	int		len
) {
	char buf [100];

	// language ids
	if (id == 0) {
		buf[0] = 4;    buf[1] = 3;	/* 4 bytes string data */
		buf[2] = 0x09; buf[3] = 0x04;	/* MSFT-speak for "en-us" */
		len = min (len, 4);
		memcpy (data, buf, len);
		return len;

	// serial number
	} else if (id == 1) {
		strlcpy (buf, hcd->self.bus_name, sizeof buf);

	// product description
	} else if (id == 2) {
		strlcpy (buf, hcd->product_desc, sizeof buf);

 	// id 3 == vendor description
	} else if (id == 3) {
		snprintf (buf, sizeof buf, "Ingenic %s", hcd->driver->description);

	// unsupported IDs --> "protocol stall"
	} else
		return -EPIPE;

	switch (len) {		/* All cases fall through */
	default:
		len = 2 + ascii2utf (buf, data + 2, len - 2);
	case 2:
		data [1] = 3;	/* type == string */
	case 1:
		data [0] = 2 * (strlen (buf) + 1);
	case 0:
		;		/* Compiler wants a statement here */
	}
	return len;
}


/* Root hub control transfers execute synchronously */
static int rh_call_control (struct usb_hcd *hcd, struct urb *urb)
{
	struct usb_ctrlrequest *cmd;
 	u16		typeReq, wValue, wIndex, wLength;
	u8		*ubuf = urb->transfer_buffer;
	u8		tbuf [sizeof (struct usb_hub_descriptor)];
	const u8	*bufp = tbuf;
	int		len = 0;
	unsigned long	flags;
	int		status = 0;
	int		n;

	cmd = (struct usb_ctrlrequest *) urb->setup_packet;
	typeReq  = (cmd->bRequestType << 8) | cmd->bRequest;
	wValue   = cmd->wValue;
	wIndex   = cmd->wIndex;
	wLength  = cmd->wLength;

	if (wLength > urb->transfer_buffer_length)
		goto error;

	urb->actual_length = 0;
	switch (typeReq) {

	/* DEVICE REQUESTS */

	case DeviceRequest | USB_REQ_GET_STATUS:
		tbuf [0] = (0 << USB_DEVICE_REMOTE_WAKEUP)
				| (1 << USB_DEVICE_SELF_POWERED);
		tbuf [1] = 0;
		len = 2;
		break;
	case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
		if (wValue != USB_DEVICE_REMOTE_WAKEUP)
			goto error;
		break;
	case DeviceOutRequest | USB_REQ_SET_FEATURE:
		goto error;
	case DeviceRequest | USB_REQ_GET_CONFIGURATION:
		tbuf [0] = 1;
		len = 1;
			/* FALLTHROUGH */
	case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
		break;
	case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
		switch (wValue & 0xff00) {
		case USB_DT_DEVICE << 8:
			if (hcd->driver->flags & HCD_USB2)
				bufp = usb2_rh_dev_descriptor;
			else if (hcd->driver->flags & HCD_USB11)
				bufp = usb11_rh_dev_descriptor;
			else
				goto error;
			len = 18;
			break;
		case USB_DT_CONFIG << 8:
			if (hcd->driver->flags & HCD_USB2) {
				bufp = hs_rh_config_descriptor;
				len = sizeof hs_rh_config_descriptor;
			} else {
				bufp = fs_rh_config_descriptor;
				len = sizeof fs_rh_config_descriptor;
			}
			break;
		case USB_DT_STRING << 8:
			n = rh_string (wValue & 0xff, hcd, ubuf, wLength);
			if (n < 0)
				goto error;
			urb->actual_length = n;
			break;
		default:
			goto error;
		}
		break;
	case DeviceRequest | USB_REQ_GET_INTERFACE:
		tbuf [0] = 0;
		len = 1;
			/* FALLTHROUGH */
	case DeviceOutRequest | USB_REQ_SET_INTERFACE:
		break;
	case DeviceOutRequest | USB_REQ_SET_ADDRESS:
		break;

	/* INTERFACE REQUESTS (no defined feature/status flags) */

	/* ENDPOINT REQUESTS */

	case EndpointRequest | USB_REQ_GET_STATUS:
		// ENDPOINT_HALT flag
		tbuf [0] = 0;
		tbuf [1] = 0;
		len = 2;
			/* FALLTHROUGH */
	case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
	case EndpointOutRequest | USB_REQ_SET_FEATURE:
		break;

	/* CLASS REQUESTS (and errors) */

	default:
		/* non-generic request */
		if (HC_IS_SUSPENDED (hcd->state))
			status = -EAGAIN;
		else {
			switch (typeReq) {
			case GetHubStatus:
			case GetPortStatus:
				len = 4;
				break;
			case GetHubDescriptor:
				len = sizeof (struct usb_hub_descriptor);
				break;
			}
			status = hcd->driver->hub_control (hcd,
				typeReq, wValue, wIndex,
				(char *)tbuf, wLength);
		}
		break;
error:
		/* "protocol stall" on error */
		status = -EPIPE;
	}

	if (status) {
		len = 0;
	}
	if (len) {
		if (urb->transfer_buffer_length < len)
			len = urb->transfer_buffer_length;
		urb->actual_length = len;
		// always USB_DIR_IN, toward host
		memcpy (ubuf, bufp, len);
	}

	/* any errors get returned through the urb completion */
	local_irq_save (flags);
	spin_lock (&urb->lock);
	if (urb->status == -EINPROGRESS)
		urb->status = status;
	spin_unlock (&urb->lock);
	usb_hcd_giveback_urb (hcd, urb);
	local_irq_restore (flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

/*
 * Root Hub interrupt transfers are synthesized with a timer.
 * Completions are called in_interrupt() but not in_irq().
 *
 * Note: some root hubs (including common UHCI based designs) can't
 * correctly issue port change IRQs.  They're the ones that _need_ a
 * timer; most other root hubs don't.  Some systems could save a
 * lot of battery power by eliminating these root hub timer IRQs.
 */
static int rh_status_urb (struct usb_hcd *hcd, struct urb *urb) 
{
	unsigned long	flags;
	int	len = 1 + (urb->dev->maxchild / 8);

	usb_spin_lock_irqsave (&hcd_data_lock, flags);
	if (hcd->status_urb || urb->transfer_buffer_length < len) {
		usb_spin_unlock_irqrestore (&hcd_data_lock, flags);
		return -EINVAL;
	}

	hcd->status_urb = urb;
	urb->hcpriv = hcd;	/* nonzero to indicate it's queued */
	usb_spin_unlock_irqrestore (&hcd_data_lock, flags);

	if (hcd->rh_state_changed)
		usb_hcd_report_rh_status(hcd);

	return 0;
}

void usb_hcd_report_rh_status (struct usb_hcd *hcd)
{
	struct urb	*urb;
	int		length;
	unsigned long	flags;
	char		buffer[6];	/* Any root hubs with > 31 ports? */

	local_irq_save (flags);
	length = hcd->driver->hub_status_data(hcd, buffer);
	if (length > 0) {
		spin_lock (&hcd_data_lock);
		if (hcd->status_urb) {
			hcd->rh_state_changed = 0;
			urb = hcd->status_urb;
			hcd->status_urb = NULL;
			spin_unlock (&hcd_data_lock);

			spin_lock (&urb->lock);
			urb->actual_length = length;
			urb->status = 0;
			urb->hcpriv = NULL;
			memcpy(urb->transfer_buffer, buffer, length);
			spin_unlock (&urb->lock);
			usb_hcd_giveback_urb(hcd, urb);
		} else {
			hcd->rh_state_changed = 1;
			spin_unlock (&hcd_data_lock);
		}
	}

	local_irq_restore (flags);
}


/*-------------------------------------------------------------------------*/

static int rh_urb_enqueue (struct usb_hcd *hcd, struct urb *urb)
{
	if (usb_pipeint (urb->pipe))
		return rh_status_urb (hcd, urb);
	else if (usb_pipecontrol (urb->pipe))
		return rh_call_control (hcd, urb);
	else
		return -EINVAL;
}

/*-------------------------------------------------------------------------*/

static int usb_rh_urb_dequeue (struct usb_hcd *hcd, struct urb *urb)
{
	unsigned long	flags;

	usb_spin_lock_irqsave (&hcd_data_lock, flags);
	if (urb == hcd->status_urb) {
		hcd->status_urb = NULL;
		usb_spin_unlock_irqrestore (&hcd_data_lock, flags);

		local_irq_save (flags);
		urb->hcpriv = NULL;
		usb_hcd_giveback_urb (hcd, urb);
		local_irq_restore (flags);
		return 0;
	}
	usb_spin_unlock_irqrestore (&hcd_data_lock, flags);

	if (usb_pipeendpoint(urb->pipe) == 0) {
		usb_spin_lock_irq(&urb->lock);	/* from usb_kill_urb */
		++urb->reject;
		usb_spin_unlock_irq(&urb->lock);

		while (urb->use_count)
			thread_waiter_wait(&usb_kill_urb_queue);	

		usb_spin_lock_irq(&urb->lock);
		--urb->reject;
		usb_spin_unlock_irq(&urb->lock);
	} else
		return -EINVAL;

	return 0;
}

/*-------------------------------------------------------------------------*/

/* exported only within usbcore */
struct usb_bus *usb_bus_get (struct usb_bus *bus)
{
	if (!bus)
		return NULL;

	bus->ref++;
	return bus;
}

/* exported only within usbcore */
void usb_bus_put (struct usb_bus *bus)
{
	if (bus)
		bus->ref--;
}

/**
 * usb_bus_init - shared initialization code
 * @bus: the bus structure being initialized
 *
 * This code is used to initialize a usb_bus structure, memory for which is
 * separately managed.
 */
static void usb_bus_init (struct usb_bus *bus)
{
	memset (&bus->devmap, 0, sizeof(struct usb_devmap));

	bus->devnum_next = 1;

	bus->root_hub = NULL;
	bus->hcpriv = NULL;
	bus->busnum = -1;
	bus->bandwidth_allocated = 0;
	bus->bandwidth_int_reqs  = 0;
	bus->bandwidth_isoc_reqs = 0;

	INIT_LIST_HEAD (&bus->bus_list);
}

/*-------------------------------------------------------------------------*/

/**
 * usb_register_bus - registers the USB host controller with the usb core
 * @bus: pointer to the bus to register
 * Context: !in_interrupt()
 *
 * Assigns a bus number, and links the controller into usbcore data
 * structures so that it can be seen by scanning the bus list.
 */
static int usb_register_bus(struct usb_bus *bus)
{
	int busnum;

	mutex_lock (&usb_bus_list_lock);
	busnum = find_next_zero_bit (busmap.busmap, USB_MAXBUS, 1);
	if (busnum < USB_MAXBUS) {
		set_bit (busnum, busmap.busmap);
		bus->busnum = busnum;
	} else {
		printf ("%s: too many buses\n", usbcore_name);
		mutex_unlock(&usb_bus_list_lock);
		return -E2BIG;
	}

	/* Add it to the local list of buses */
	list_add (&bus->bus_list, &usb_bus_list);
	mutex_unlock (&usb_bus_list_lock);

	printf ("new USB bus registered, assigned bus number %d\n", bus->busnum);
	return 0;
}

/**
 * usb_deregister_bus - deregisters the USB host controller
 * @bus: pointer to the bus to deregister
 * Context: !in_interrupt()
 *
 * Recycles the bus number, and unlinks the controller from usbcore data
 * structures so that it won't be seen by scanning the bus list.
 */
static void usb_deregister_bus (struct usb_bus *bus)
{
	printf ("USB bus %d deregistered\n", bus->busnum);

	/*
	 * NOTE: make sure that all the devices are removed by the
	 * controller code, as well as having it call this when cleaning
	 * itself up
	 */
	mutex_lock (&usb_bus_list_lock);
	list_del (&bus->bus_list);
	mutex_unlock (&usb_bus_list_lock);

	clear_bit (bus->busnum, busmap.busmap);

	if (bus->ref)
		panic("ERROR: usb hub reference counter non-zero %d\n", bus->ref);
}

/**
 * register_root_hub - called by usb_add_hcd() to register a root hub
 * @hcd: host controller for this root hub
 *
 * This function registers the root hub with the USB subsystem.  It sets up
 * the device properly in the device tree and then calls usb_new_device()
 * to register the usb device.  It also assigns the root hub's USB address
 * (always 1).
 *
 * Return: 0 if successful. A negative error code otherwise.
 */
static int register_root_hub (struct usb_hcd *hcd)
{
	struct usb_device *usb_dev = hcd->self.root_hub;
	const int devnum = 1;
	int retval;

	usb_dev->devnum = devnum;
	usb_dev->bus->devnum_next = devnum + 1;
	memset (&usb_dev->bus->devmap.devicemap, 0,
			sizeof usb_dev->bus->devmap.devicemap);
	set_bit (devnum, usb_dev->bus->devmap.devicemap);
	usb_set_device_state(usb_dev, USB_STATE_ADDRESS);

	mutex_lock (&usb_bus_list_lock);

	usb_dev->ep0.desc.wMaxPacketSize = 64;
	retval = usb_get_device_descriptor(usb_dev, USB_DT_DEVICE_SIZE);
	if (retval != sizeof usb_dev->descriptor) {
		mutex_unlock (&usb_bus_list_lock);
		printf("%s: can't read device descriptor %d\n", __func__, retval);
		return (retval < 0) ? retval : -EMSGSIZE;
	}

	usb_lock_device (usb_dev);
	retval = usb_new_device (usb_dev);
	usb_unlock_device (usb_dev);
	if (retval)
		printf ("%s: can't register root hub, %d\n", __func__, retval);
	mutex_unlock (&usb_bus_list_lock);

	if (retval == 0) {
		usb_spin_lock_irq (&hcd_root_hub_lock);
		hcd->rh_registered = 1;
		usb_spin_unlock_irq (&hcd_root_hub_lock);

		/* Did the HC die before the root hub was registered? */
		if (hcd->state == HC_STATE_HALT)
			usb_hc_died (hcd);	/* This time clean up */
	}

	return retval;
}

/*-------------------------------------------------------------------------*/

/**
 * usb_calc_bus_time - approximate periodic transaction time in nanoseconds
 * @speed: from dev->speed; USB_SPEED_{LOW,FULL,HIGH}
 * @is_input: true iff the transaction sends data to the host
 * @isoc: true for isochronous transactions, false for interrupt ones
 * @bytecount: how many bytes in the transaction.
 *
 * Returns approximate bus time in nanoseconds for a periodic transaction.
 * See USB 2.0 spec section 5.11.3; only periodic transfers need to be
 * scheduled in software, this function is only used for such scheduling.
 */
long usb_calc_bus_time (int speed, int is_input, int isoc, int bytecount)
{
	unsigned long	tmp;

	switch (speed) {
	case USB_SPEED_LOW: 	/* INTR only */
		if (is_input) {
			tmp = (67667L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (64060L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp);
		} else {
			tmp = (66700L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (64107L + (2 * BW_HUB_LS_SETUP) + BW_HOST_DELAY + tmp);
		}
	case USB_SPEED_FULL:	/* ISOC or INTR */
		if (isoc) {
			tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (((is_input) ? 7268L : 6265L) + BW_HOST_DELAY + tmp);
		} else {
			tmp = (8354L * (31L + 10L * BitTime (bytecount))) / 1000L;
			return (9107L + BW_HOST_DELAY + tmp);
		}
	case USB_SPEED_HIGH:	/* ISOC or INTR */
		// FIXME adjust for input vs output
		if (isoc)
			tmp = HS_NSECS_ISO (bytecount);
		else
			tmp = HS_NSECS (bytecount);
		return tmp;
	default:
		printf ("%s: bogus device speed!\n", usbcore_name);
		return -1;
	}
}

/*-------------------------------------------------------------------------*/

/*
 * Generic HC operations.
 */

/*-------------------------------------------------------------------------*/

static void urb_unlink (struct usb_hcd *hcd, struct urb *urb)
{
	unsigned long flags;
	unsigned int length;
	int at_root_hub = (urb->dev == hcd->self.root_hub);

	/* clear all state linking urb to this dev (and hcd) */
	usb_spin_lock_irqsave (&hcd_data_lock, flags);
	list_del_init (&urb->urb_list_entry);
	usb_put_dev (urb->dev);
	usb_spin_unlock_irqrestore (&hcd_data_lock, flags);

	if (hcd_uses_dma(hcd) && !at_root_hub) {
		if (usb_pipecontrol (urb->pipe)
			&& !(urb->transfer_flags & URB_NO_SETUP_DMA_MAP))
				urb->setup_dma = 0;

		if (urb->transfer_buffer_length != 0
			&& !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)) {
				if (usb_pipein (urb->pipe)) {
					length = ALIGN(urb->transfer_buffer_length, cache_line_size());
					invalidate_dcache((unsigned long)urb->transfer_buffer, length);
				}
				urb->transfer_dma = 0;

				if (urb->saved_transfer_buffer) {
					if (usb_pipein (urb->pipe))
						memcpy(urb->saved_transfer_buffer, urb->transfer_buffer, urb->transfer_buffer_length);

					free(urb->transfer_buffer);
					urb->transfer_buffer = urb->saved_transfer_buffer;
					urb->saved_transfer_buffer = NULL;
				}
			}
	}

}

/* may be called in any context with a valid urb->dev usecount
 * caller surrenders "ownership" of urb
 * expects usb_submit_urb() to have sanity checked and conditioned all
 * inputs in the urb
 */
int usb_hcd_submit_urb (struct urb *urb)
{
	int			status;
	struct usb_hcd		*hcd = urb->dev->bus->hcpriv;
	struct usb_host_endpoint *ep = urb->ep;
	unsigned long		flags;

	if (!hcd)
		return -ENODEV;

	/*
	 * Atomically queue the urb,  first to our records, then to the HCD.
	 * Access to urb->status is controlled by urb->lock ... changes on
	 * i/o completion (normal or fault) or unlinking.
	 */

	// FIXME:  verify that quiescing hc works right (RH cleans up)
	usb_spin_lock_irqsave (&hcd_data_lock, flags);
	if (unlikely (urb->reject))
		status = -EPERM;
	else if (unlikely(!ep->enabled))
		status = -ENOENT;
	else switch (hcd->state) {
	case HC_STATE_RUNNING:
	case HC_STATE_RESUMING:
		urb->submitting++;
		usb_get_dev (urb->dev);
		list_add_tail (&urb->urb_list_entry, &ep->urb_list);
		status = 0;
		break;
	default:
		status = -ESHUTDOWN;
		break;
	}
	usb_spin_unlock_irqrestore (&hcd_data_lock, flags);
	if (status) {
		INIT_LIST_HEAD (&urb->urb_list_entry);
		return status;
	}

	/* increment urb's reference count as part of giving it to the HCD
	 * (which now controls it).  HCD guarantees that it either returns
	 * an error or calls giveback(), but not both.
	 */

	usb_spin_lock_irqsave (&urb->lock, flags);
	usb_get_urb (urb);
	urb->use_count++;
	usb_spin_unlock_irqrestore (&urb->lock, flags);

	if (urb->dev == hcd->self.root_hub) {
		/* NOTE:  requirement on hub callers (usbfs and the hub
		 * driver, for now) that URBs' urb->transfer_buffer be
		 * valid and usb_buffer_{sync,unmap}() not be needed, since
		 * they could clobber root hub response data.
		 */
		status = rh_urb_enqueue (hcd, urb);
		goto done;
	}

	/* lower level hcd code should use *_dma exclusively,
	 * unless it uses pio or talks to another transport.
	 */
	if (hcd_uses_dma(hcd)) {
		unsigned int length;
		void *malloc_ptr;

		if (usb_pipecontrol (urb->pipe)
			&& !(urb->transfer_flags & URB_NO_SETUP_DMA_MAP)) {
				if ((unsigned long)urb->setup_packet % cache_line_size())
					panic("We assume setup_packet is always aligned; warn if not\n");

				length = ALIGN(sizeof(struct usb_ctrlrequest), cache_line_size());
				flush_dcache((unsigned long)urb->setup_packet, length);
				urb->setup_dma = virt_to_phys(urb->setup_packet);
			}

		if (urb->transfer_buffer_length != 0
			&& !(urb->transfer_flags & URB_NO_TRANSFER_DMA_MAP)) {
				if ((unsigned long)urb->transfer_buffer % cache_line_size()) {
					if (urb->saved_transfer_buffer)
						panic("saved_transfer_buffer is use, but transfer_buffer not cache_line aligned\n");

					malloc_ptr = cache_align_malloc(urb->transfer_buffer_length);

					if (usb_pipeout(urb->pipe))
						memcpy(malloc_ptr, urb->transfer_buffer, urb->transfer_buffer_length);

					urb->saved_transfer_buffer = urb->transfer_buffer;
					urb->transfer_buffer = malloc_ptr;
				}

				length = ALIGN(urb->transfer_buffer_length, cache_line_size());
				flush_dcache((unsigned long)urb->transfer_buffer, length);
				urb->transfer_dma = virt_to_phys(urb->transfer_buffer);
			}
	}

	status = hcd->driver->urb_enqueue (hcd, urb);
done:
	if (unlikely (status)) {
		urb_unlink (hcd, urb);
		usb_spin_lock_irqsave (&urb->lock, flags);
		urb->use_count--;
		usb_put_urb (urb);
		if (unlikely (urb->reject))
			thread_waiter_wakeup (&usb_kill_urb_queue);
		usb_spin_unlock_irqrestore (&urb->lock, flags);
	}
	usb_spin_lock_irqsave (&hcd_data_lock, flags);
	urb->submitting--;
	usb_spin_unlock_irqrestore (&hcd_data_lock, flags);

	return status;
}

/**
 * usb_hcd_reset_endpoint - reset host endpoint state
 * @udev: USB device.
 * @ep:   the endpoint to reset.
 *
 * Resets any host endpoint state such as the toggle bit, sequence
 * number and current window.
 */
void usb_hcd_reset_endpoint(struct usb_device *udev,
			    struct usb_host_endpoint *ep)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);

	if (hcd->driver->endpoint_reset)
		hcd->driver->endpoint_reset(hcd, ep);
}

/*-------------------------------------------------------------------------*/

/* called in any context */
int usb_hcd_get_frame_number (struct usb_device *udev)
{
	struct usb_hcd	*hcd = (struct usb_hcd *)udev->bus->hcpriv;
	if (!HC_IS_RUNNING (hcd->state))
		return -ESHUTDOWN;
	return hcd->driver->get_frame_number (hcd);
}

/*-------------------------------------------------------------------------*/

/* this makes the hcd giveback() the urb more quickly, by kicking it
 * off hardware queues (which may take a while) and returning it as
 * soon as practical.  we've already set up the urb's return status,
 * but we can't know if the callback completed already.
 */
static int
unlink1 (struct usb_hcd *hcd, struct urb *urb)
{
	int		value;

	if (urb->dev == hcd->self.root_hub)
		value = usb_rh_urb_dequeue (hcd, urb);
	else {

		/* The only reason an HCD might fail this call is if
		 * it has not yet fully queued the urb to begin with.
		 * Such failures should be harmless. */
		value = hcd->driver->urb_dequeue (hcd, urb);
	}

	return value;
}

/*
 * called in any context
 *
 * caller guarantees urb won't be recycled till both unlink()
 * and the urb's completion function return
 */
int usb_hcd_unlink_urb (struct urb *urb, int status)
{
	struct usb_host_endpoint	*ep;
	struct usb_hcd			*hcd = NULL;
	unsigned long			flags;
	struct list_head		*tmp;
	int				retval;

	if (!urb)
		return -EINVAL;
	if (!urb->dev || !urb->dev->bus)
		return -ENODEV;
	ep = (usb_pipein(urb->pipe) ? urb->dev->ep_in : urb->dev->ep_out)
			[usb_pipeendpoint(urb->pipe)];
	if (!ep)
		return -ENODEV;

	/*
	 * we contend for urb->status with the hcd core,
	 * which changes it while returning the urb.
	 *
	 * Caller guaranteed that the urb pointer hasn't been freed, and
	 * that it was submitted.  But as a rule it can't know whether or
	 * not it's already been unlinked ... so we respect the reversed
	 * lock sequence needed for the usb_hcd_giveback_urb() code paths
	 * (urb lock, then hcd_data_lock) in case some other CPU is now
	 * unlinking it.
	 */
	usb_spin_lock_irqsave (&urb->lock, flags);
	spin_lock (&hcd_data_lock);

	hcd = urb->dev->bus->hcpriv;
	if (hcd == NULL) {
		retval = -ENODEV;
		goto done;
	}

	/* running ~= hc unlink handshake works (irq, timer, etc)
	 * halted ~= no unlink handshake is needed
	 * suspended, resuming == should never happen
	 */
	if (!HC_IS_RUNNING (hcd->state) && hcd->state != HC_STATE_HALT)
		printf("usb hcd state abnormal\n");


	/* insist the urb is still queued */
	list_for_each(tmp, &ep->urb_list) {
		if (tmp == &urb->urb_list_entry)
			break;
	}
	if (tmp != &urb->urb_list_entry) {
		retval = -EIDRM;
		goto done;
	}

	if (urb->submitting)
		panic("urb is submitting\n");

	/* Any status except -EINPROGRESS means something already started to
	 * unlink this URB from the hardware.  So there's no more work to do.
	 */
	if (urb->status != -EINPROGRESS) {
		retval = -EBUSY;
		goto done;
	}

	urb->status = status;

	spin_unlock (&hcd_data_lock);
	usb_spin_unlock_irqrestore (&urb->lock, flags);

	retval = unlink1 (hcd, urb);
	if (retval == 0)
		retval = -EINPROGRESS;
	return retval;

done:
	spin_unlock (&hcd_data_lock);
	usb_spin_unlock_irqrestore (&urb->lock, flags);
	return retval;
}

/*-------------------------------------------------------------------------*/

/* disables the endpoint: cancels any pending urbs, then synchronizes with
 * the hcd to make sure all endpoint state is gone from hardware. use for
 * set_configuration, set_interface, driver removal, physical disconnect.
 *
 * example:  a qh stored in ep->hcpriv, holding state related to endpoint
 * type, maxpacket size, halt status, and scheduling.
 */
void usb_hcd_disable_endpoint (struct usb_device *udev, struct usb_host_endpoint *ep)
{
	struct usb_hcd		*hcd;
	struct urb		*urb;

	hcd = udev->bus->hcpriv;

	assert(hcd);

	if (!HC_IS_RUNNING (hcd->state) && hcd->state != HC_STATE_HALT)
		printf("usb hcd state abnormal\n");

	local_irq_disable ();

	/* FIXME move most of this into message.c as part of its
	 * endpoint disable logic
	 */

	/* ep is already gone from udev->ep_{in,out}[]; no more submits */
rescan:
	spin_lock (&hcd_data_lock);
	list_for_each_entry (urb, &ep->urb_list, urb_list_entry) {
		int	tmp;

		if (urb->submitting) {
			spin_unlock (&hcd_data_lock);
			local_irq_enable ();
			msleep(1);
			local_irq_disable ();
			goto rescan;
		}

		/* another cpu may be in hcd, spinning on hcd_data_lock
		 * to giveback() this urb.  the races here should be
		 * small, but a full fix needs a new "can't submit"
		 * urb state.
		 * FIXME urb->reject should allow that...
		 */
		if (urb->status != -EINPROGRESS)
			continue;

		spin_unlock (&hcd_data_lock);

		spin_lock (&urb->lock);
		usb_get_urb (urb);
		tmp = urb->status;
		if (tmp == -EINPROGRESS)
			urb->status = -ESHUTDOWN;
		spin_unlock (&urb->lock);

		/* kick hcd unless it's already returning this */
		if (tmp == -EINPROGRESS)
			unlink1 (hcd, urb);

		spin_lock (&urb->lock);
		usb_put_urb (urb);
		spin_unlock (&urb->lock);

		/* list contents may have changed */
		goto rescan;
	}
	spin_unlock (&hcd_data_lock);
	local_irq_enable ();

	/* synchronize with the hardware, so old configuration state
	 * clears out immediately (and will be freed).
	 */
	// might_sleep ();
	if (hcd->driver->endpoint_disable)
		hcd->driver->endpoint_disable (hcd, ep);
}

/*-------------------------------------------------------------------------*/

/**
 * usb_hcd_giveback_urb - return URB from HCD to device driver
 * @hcd: host controller returning the URB
 * @urb: urb being returned to the USB device driver.
 * @regs: pt_regs, passed down to the URB completion handler
 * Context: in_interrupt()
 *
 * This hands the URB from HCD to its USB device driver, using its
 * completion function.  The HCD has freed all per-urb resources
 * (and is done using urb->hcpriv).  It also released all HCD locks;
 * the device driver won't cause problems if it frees, modifies,
 * or resubmits this URB.
 */
void usb_hcd_giveback_urb (struct usb_hcd *hcd, struct urb *urb)
{
	unsigned long flags;
	urb_unlink (hcd, urb);

	/* pass ownership to the completion handler */
	urb->complete (urb);
	usb_spin_lock_irqsave (&urb->lock, flags);
	urb->use_count--;
	usb_put_urb (urb);
	if (unlikely (urb->reject))
		thread_waiter_wakeup (&usb_kill_urb_queue);

	usb_spin_unlock_irqrestore (&urb->lock, flags);
}

/*-------------------------------------------------------------------------*/

/**
 * usb_hc_died - report abnormal shutdown of a host controller (bus glue)
 * @hcd: pointer to the HCD representing the controller
 *
 * This is called by bus glue to report a USB host controller that died
 * while operations may still have been pending.  It's called automatically
 * by the PCI glue, so only glue for non-PCI busses should need to call it. 
 */
void usb_hc_died (struct usb_hcd *hcd)
{
	unsigned long flags;

	usb_spin_lock_irqsave (&hcd_root_hub_lock, flags);
	if (hcd->rh_registered) {

		/* make khubd clean up old urbs and devices */
		usb_set_device_state (hcd->self.root_hub,
				USB_STATE_NOTATTACHED);
		usb_kick_khubd (hcd->self.root_hub);
	}
	usb_spin_unlock_irqrestore (&hcd_root_hub_lock, flags);
}

/*-------------------------------------------------------------------------*/

/**
 * usb_create_hcd - create and initialize an HCD structure
 * @driver: HC driver that will use this hcd
 * @dev: device for this HC, stored in hcd->self.controller
 * @bus_name: value to store in hcd->self.bus_name
 * Context: !in_interrupt()
 *
 * Allocate a struct usb_hcd, with extra space at the end for the
 * HC driver's private data.  Initialize the generic members of the
 * hcd structure.
 *
 * If memory is unavailable, returns NULL.
 */
struct usb_hcd *usb_create_hcd (const struct hc_driver *driver, char *bus_name)
{
	struct usb_hcd *hcd;

	hcd = calloc(1, sizeof(*hcd) + driver->hcd_priv_size);
	if (!hcd) {
		return NULL;
	}

	usb_bus_init(&hcd->self);
	hcd->self.hcpriv = hcd;
	hcd->self.bus_name = bus_name;

	hcd->driver = driver;
	hcd->product_desc = (driver->product_desc) ? driver->product_desc :
			"USB Host Controller";

	return hcd;
}

void usb_free_hcd(struct usb_hcd *hcd)
{
	free(hcd);
}

/*
 * Before we free this root hub, flush in-flight peering attempts
 * and disable peer lookups
 */
static void usb_put_invalidate_rhdev(struct usb_hcd *hcd)
{
	struct usb_device *rhdev;

	mutex_lock(&usb_bus_list_lock);
	rhdev = hcd->self.root_hub;
	hcd->self.root_hub = NULL;
	mutex_unlock(&usb_bus_list_lock);
	usb_release_dev(rhdev);
}

/**
 * usb_add_hcd - finish generic HCD structure initialization and register
 * @hcd: the usb_hcd structure to initialize
 *
 * Finish the remaining parts of generic HCD initialization: allocate the
 * buffers of consistent memory, register the bus, request the IRQ line,
 * and call the driver's reset() and start() routines.
 */
int usb_add_hcd(struct usb_hcd *hcd)
{
	int	retval;
	struct usb_device *rhdev;

	retval = usb_register_bus(&hcd->self);
	if (retval < 0)
		goto err_register_bus;

	rhdev = usb_alloc_dev(NULL, &hcd->self, 0);
	if (rhdev == NULL) {
		printf("usb host unable to allocate root hub\n");
		retval = -ENOMEM;
		goto err_allocate_root_hub;
	}

	mutex_lock (&usb_bus_list_lock);
	hcd->self.root_hub = rhdev;
	mutex_unlock (&usb_bus_list_lock);

	if (hcd->driver->flags & HCD_USB2)
		rhdev->speed = USB_SPEED_HIGH;
	else if (hcd->driver->flags & HCD_USB11)
		rhdev->speed = USB_SPEED_FULL;
	else {
		retval = -EINVAL;
		goto err_set_rh_speed;
	}

	/* "reset" is misnamed; its role is now one-time init. the controller
	 * should already have been reset (and boot firmware kicked off etc).
	 */
	if (hcd->driver->reset) {
		retval = hcd->driver->reset(hcd);
		if (retval < 0) {
			printf("usb host can't setup: %d\n", retval);
			goto err_hcd_driver_setup;
		}
	}

	hcd->state = HC_STATE_RUNNING;
	retval = hcd->driver->start(hcd);
	if (retval < 0) {
		printf("usb host startup error %d\n", retval);
		goto err_hcd_driver_start;
	}

	/* starting here, usbcore will pay attention to this root hub */
	retval = register_root_hub(hcd);
	if (retval != 0)
		goto err_register_root_hub;

	return retval;

err_register_root_hub:
	hcd->driver->stop(hcd);
	hcd->state = HC_STATE_HALT;
err_hcd_driver_start:
err_hcd_driver_setup:
err_set_rh_speed:
	usb_put_invalidate_rhdev(hcd);
err_allocate_root_hub:
	usb_deregister_bus(&hcd->self);
err_register_bus:
	return retval;
}

/**
 * usb_remove_hcd - shutdown processing for generic HCDs
 * @hcd: the usb_hcd structure to remove
 * Context: !in_interrupt()
 *
 * Disconnects the root hub, then reverses the effects of usb_add_hcd(),
 * invoking the HCD's stop() method.
 */
void usb_remove_hcd(struct usb_hcd *hcd)
{
	if (HC_IS_RUNNING (hcd->state))
		hcd->state = HC_STATE_QUIESCING;

	usb_spin_lock_irq (&hcd_root_hub_lock);
	hcd->rh_registered = 0;
	usb_spin_unlock_irq (&hcd_root_hub_lock);
	usb_disconnect(&hcd->self.root_hub);

	hcd->driver->stop(hcd);
	hcd->state = HC_STATE_HALT;

	usb_deregister_bus(&hcd->self);
}
