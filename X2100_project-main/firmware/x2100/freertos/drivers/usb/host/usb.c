#include "hcd.h"
#include "usb.h"

struct usb_device_driver usb_generic_driver;

const char *usbcore_name = "usbcore";

/* ==0 unlock; >0 read_lock; <0 write_lock */
static int usb_all_devices_state;
static DEFINE_MUTEX(usb_all_devices_mutex);
static DEFINE_THREADCOND(usb_all_devices_cond);

LIST_HEAD(usb_driver_list);

/* called from driver core with dev locked */
static int usb_probe_device(struct usb_device *udev, void *data)
{
	struct usb_device_driver *udriver = data;
	int ret;

	ret = udriver->probe(udev);
	if (ret)
		return ret;

	udriver->udev = udev;
	return 0;
}

/* called from driver core with dev locked */
static int usb_unbind_device(struct usb_device *udev, void* data)
{
	struct usb_device_driver *udriver = data;

	if (udriver->disconnect)
		udriver->disconnect(udev);

	udriver->udev = NULL;
	return 0;
}

/**
 * usb_register_device_driver - register a USB device (not interface) driver
 * @new_udriver: USB operations for the device driver
 * @owner: module owner of this driver.
 *
 * Registers a USB device driver with the USB core.  The list of
 * unattached devices will be rescanned whenever a new driver is
 * added, allowing the new driver to attach to any recognized devices.
 *
 * Return: A negative error code on failure and 0 on success.
 */
void usb_register_device_driver(struct usb_device_driver *new_udriver)
{
	new_udriver->drvwrap.for_devices = 1;
	new_udriver->drvwrap.name = new_udriver->name;
	new_udriver->drvwrap.device_probe = usb_probe_device;
	new_udriver->drvwrap.device_remove = usb_unbind_device;
	new_udriver->drvwrap.private_data = new_udriver;

	usb_lock_all_devices();
	list_add_tail(&new_udriver->drvwrap.list, &usb_driver_list);
	usb_unlock_all_devices();
}

/**
 * usb_deregister_device_driver - unregister a USB device (not interface) driver
 * @udriver: USB operations of the device driver to unregister
 * Context: must be able to sleep
 *
 * Unlinks the specified driver from the internal USB driver list.
 */
void usb_deregister_device_driver(struct usb_device_driver *udriver)
{
	struct usbdrv_wrap *drvwrap;
	struct usb_device_driver *tmp_udriver;

	usb_lock_all_devices();

	list_for_each_entry(drvwrap, &usb_driver_list, list) {
		tmp_udriver = container_of(drvwrap, struct usb_device_driver, drvwrap);
		if (tmp_udriver == udriver) {
			list_del(&drvwrap->list);

			if (udriver->udev)
				usb_unbind_device(udriver->udev, udriver->drvwrap.private_data);

			usb_unlock_all_devices();
			return;
		}
	}

	usb_unlock_all_devices();

	panic("usb_deregister_device_driver: couldn't find usb_device_driver\n");
}

static int usb_probe_interface(struct usb_interface *intf, void *data)
{
	struct usb_driver *driver = data;
	const struct usb_device_id *id;
	int error = -ENODEV;

	id = usb_match_id (intf, driver->id_table);
	if (!id)
		return error;

	intf->condition = USB_INTERFACE_BINDING;

	error = driver->probe(intf, id);
	if (error)
		goto err;

	intf->condition = USB_INTERFACE_BOUND;
	driver->intf = intf;
	return 0;

 err:
	usb_set_intfdata(intf, NULL);
	intf->condition = USB_INTERFACE_UNBOUND;
	return error;
}

static int usb_unbind_interface(struct usb_interface *intf, void* data)
{
	struct usb_driver *driver = data;

	intf->condition = USB_INTERFACE_UNBINDING;

	/* release all urbs for this interface */
	usb_disable_interface(interface_to_usbdev(intf), intf);

	if (driver && driver->disconnect)
		driver->disconnect(intf);

	/* reset other interface state */
	usb_set_interface(interface_to_usbdev(intf),
			intf->altsetting[0].desc.bInterfaceNumber,
			0);
	usb_set_intfdata(intf, NULL);
	intf->drvwrap = NULL;
	intf->condition = USB_INTERFACE_UNBOUND;

	driver->intf = NULL;
	return 0;
}

/**
 * usb_register_driver - register a USB interface driver
 * @new_driver: USB operations for the interface driver
 * @owner: module owner of this driver.
 * @mod_name: module name string
 *
 * Registers a USB interface driver with the USB core.  The list of
 * unattached interfaces will be rescanned whenever a new driver is
 * added, allowing the new driver to attach to any recognized interfaces.
 *
 * Return: A negative error code on failure and 0 on success.
 *
 */
void usb_register_driver(struct usb_driver *new_driver)
{
	new_driver->drvwrap.for_devices = 0;
	new_driver->drvwrap.name = new_driver->name;
	new_driver->drvwrap.interface_probe = usb_probe_interface;
	new_driver->drvwrap.interface_remove = usb_unbind_interface;
	new_driver->drvwrap.private_data = new_driver;

	usb_lock_all_devices();
	list_add_tail(&new_driver->drvwrap.list, &usb_driver_list);
	usb_unlock_all_devices();
}

/**
 * usb_deregister - unregister a USB driver
 * @driver: USB operations of the driver to unregister
 * Context: must be able to sleep
 *
 * Unlinks the specified driver from the internal USB driver list.
 * 
 * NOTE: If you called usb_register_dev(), you still need to call
 * usb_deregister_dev() to clean up your driver's allocated minor numbers,
 * this * call will no longer do it for you.
 */
void usb_deregister(struct usb_driver *driver)
{
	struct usbdrv_wrap *drvwrap;
	struct usb_driver *tmp_driver;

	usb_lock_all_devices();

	list_for_each_entry(drvwrap, &usb_driver_list, list) {
		tmp_driver = container_of(drvwrap, struct usb_driver, drvwrap);
		if (tmp_driver == driver) {
			list_del(&drvwrap->list);

			if (driver->intf)
				usb_unbind_interface(driver->intf, driver);

			usb_unlock_all_devices();
			return;
		}
	}

	usb_unlock_all_devices();

	panic("usb_deregister: couldn't find usb_driver\n");
}

/**
 * usb_ifnum_to_if - get the interface object with a given interface number
 * @dev: the device whose current configuration is considered
 * @ifnum: the desired interface
 *
 * This walks the device descriptor for the currently active configuration
 * and returns a pointer to the interface with that particular interface
 * number, or null.
 *
 * Note that configuration descriptors are not required to assign interface
 * numbers sequentially, so that it would be incorrect to assume that
 * the first interface in that descriptor corresponds to interface zero.
 * This routine helps device drivers avoid such mistakes.
 * However, you should make sure that you do the right thing with any
 * alternate settings available for this interfaces.
 *
 * Don't call this function unless you are bound to one of the interfaces
 * on this device or you have locked the device!
 */
struct usb_interface *usb_ifnum_to_if(struct usb_device *dev, unsigned ifnum)
{
	struct usb_host_config *config = dev->actconfig;
	int i;

	if (!config)
		return NULL;
	for (i = 0; i < config->desc.bNumInterfaces; i++)
		if (config->interface[i]->altsetting[0]
				.desc.bInterfaceNumber == ifnum)
			return config->interface[i];

	return NULL;
}

/**
 * usb_altnum_to_altsetting - get the altsetting structure with a given
 *	alternate setting number.
 * @intf: the interface containing the altsetting in question
 * @altnum: the desired alternate setting number
 *
 * This searches the altsetting array of the specified interface for
 * an entry with the correct bAlternateSetting value and returns a pointer
 * to that entry, or null.
 *
 * Note that altsettings need not be stored sequentially by number, so
 * it would be incorrect to assume that the first altsetting entry in
 * the array corresponds to altsetting zero.  This routine helps device
 * drivers avoid such mistakes.
 *
 * Don't call this function unless you are bound to the intf interface
 * or you have locked the device!
 */
struct usb_host_interface *usb_altnum_to_altsetting(struct usb_interface *intf,
		unsigned int altnum)
{
	int i;

	for (i = 0; i < intf->num_altsetting; i++) {
		if (intf->altsetting[i].desc.bAlternateSetting == altnum)
			return &intf->altsetting[i];
	}
	return NULL;
}

/**
 * usb_driver_claim_interface - bind a driver to an interface
 * @driver: the driver to be bound
 * @iface: the interface to which it will be bound; must be in the
 *	usb device's active configuration
 * @priv: driver data associated with that interface
 *
 * This is used by usb device drivers that need to claim more than one
 * interface on a device when probing (audio and acm are current examples).
 * No device driver should directly modify internal usb_interface or
 * usb_device structure members.
 *
 * Few drivers should need to use this routine, since the most natural
 * way to bind to an interface is to return the private data from
 * the driver's probe() method.
 *
 * Callers must own the device lock and the driver model's usb_bus_type.subsys
 * writelock.  So driver probe() entries don't need extra locking,
 * but other call contexts may need to explicitly claim those locks.
 */
int usb_driver_claim_interface(struct usb_driver *driver,
				struct usb_interface *iface, void* priv)
{
	if (!iface)
		return -ENODEV;

	if (iface->drvwrap)
		return -EBUSY;

	iface->drvwrap = &driver->drvwrap;
	usb_set_intfdata(iface, priv);
	iface->condition = USB_INTERFACE_BOUND;

	return 0;
}

/**
 * usb_driver_release_interface - unbind a driver from an interface
 * @driver: the driver to be unbound
 * @iface: the interface from which it will be unbound
 *
 * This can be used by drivers to release an interface without waiting
 * for their disconnect() methods to be called.  In typical cases this
 * also causes the driver disconnect() method to be called.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 * Callers must own the device lock and the driver model's usb_bus_type.subsys
 * writelock.  So driver disconnect() entries don't need extra locking,
 * but other call contexts may need to explicitly claim those locks.
 */
void usb_driver_release_interface(struct usb_driver *driver,
					struct usb_interface *iface)
{
	/* this should never happen, don't release something that's not ours */
	if (!iface->drvwrap || iface->drvwrap != &driver->drvwrap)
		return;

	/* don't release from within disconnect() */
	if (iface->condition != USB_INTERFACE_BOUND)
		return;

	usb_set_intfdata(iface, NULL);
	iface->drvwrap = NULL;
	iface->condition = USB_INTERFACE_UNBOUND;
}

/* returns 0 if no match, 1 if match */
int usb_match_device(struct usb_device *dev, const struct usb_device_id *id)
{
	if ((id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
	    id->idVendor != dev->descriptor.idVendor)
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_PRODUCT) &&
	    id->idProduct != dev->descriptor.idProduct)
		return 0;

	/* No need to test id->bcdDevice_lo != 0, since 0 is never
	   greater than any unsigned number. */
	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_LO) &&
	    (id->bcdDevice_lo > dev->descriptor.bcdDevice))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_HI) &&
	    (id->bcdDevice_hi < dev->descriptor.bcdDevice))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_CLASS) &&
	    (id->bDeviceClass != dev->descriptor.bDeviceClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_SUBCLASS) &&
	    (id->bDeviceSubClass != dev->descriptor.bDeviceSubClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_DEV_PROTOCOL) &&
	    (id->bDeviceProtocol != dev->descriptor.bDeviceProtocol))
		return 0;

	return 1;
}

/* returns 0 if no match, 1 if match */
int usb_match_one_id_intf(struct usb_device *dev,
			  struct usb_host_interface *intf,
			  const struct usb_device_id *id)
{
	/* The interface class, subclass, protocol and number should never be
	 * checked for a match if the device class is Vendor Specific,
	 * unless the match record specifies the Vendor ID. */
	if (dev->descriptor.bDeviceClass == USB_CLASS_VENDOR_SPEC &&
			!(id->match_flags & USB_DEVICE_ID_MATCH_VENDOR) &&
			(id->match_flags & (USB_DEVICE_ID_MATCH_INT_CLASS |
				USB_DEVICE_ID_MATCH_INT_SUBCLASS |
				USB_DEVICE_ID_MATCH_INT_PROTOCOL |
				USB_DEVICE_ID_MATCH_INT_NUMBER)))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_CLASS) &&
	    (id->bInterfaceClass != intf->desc.bInterfaceClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_SUBCLASS) &&
	    (id->bInterfaceSubClass != intf->desc.bInterfaceSubClass))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_PROTOCOL) &&
	    (id->bInterfaceProtocol != intf->desc.bInterfaceProtocol))
		return 0;

	if ((id->match_flags & USB_DEVICE_ID_MATCH_INT_NUMBER) &&
	    (id->bInterfaceNumber != intf->desc.bInterfaceNumber))
		return 0;

	return 1;
}

/* returns 0 if no match, 1 if match */
int usb_match_one_id(struct usb_interface *interface,
		     const struct usb_device_id *id)
{
	struct usb_host_interface *intf;
	struct usb_device *dev;

	/* proc_connectinfo in devio.c may call us with id == NULL. */
	if (id == NULL)
		return 0;

	intf = interface->cur_altsetting;
	dev = interface->usb_dev;

	if (!usb_match_device(dev, id))
		return 0;

	return usb_match_one_id_intf(dev, intf, id);
}

const struct usb_device_id *usb_match_id(struct usb_interface *interface,
					 const struct usb_device_id *id)
{
	/* proc_connectinfo in devio.c may call us with id == NULL. */
	if (id == NULL)
		return NULL;

	/* It is important to check that id->driver_info is nonzero,
	   since an entry that is all zeroes except for a nonzero
	   id->driver_info is the way to create an entry that
	   indicates that the driver want to examine every
	   device and interface. */
	for (; id->idVendor || id->idProduct || id->bDeviceClass ||
	       id->bInterfaceClass || id->driver_info; id++) {
		if (usb_match_one_id(interface, id))
			return id;
	}

	return NULL;
}

const struct usb_device_id *usb_device_match_id(struct usb_device *udev,
				const struct usb_device_id *id)
{
	if (!id)
		return NULL;

	for (; id->idVendor || id->idProduct ; id++) {
		if (usb_match_device(udev, id))
			return id;
	}

	return NULL;
}

bool usb_driver_applicable(struct usb_device *udev,
			   struct usb_device_driver *udrv)
{
	if (udrv->id_table && udrv->match)
		return usb_device_match_id(udev, udrv->id_table) != NULL &&
		       udrv->match(udev);

	if (udrv->id_table)
		return usb_device_match_id(udev, udrv->id_table) != NULL;

	if (udrv->match)
		return udrv->match(udev);

	return false;
}

int usb_device_match (struct usb_device *udev, struct usbdrv_wrap *drvwrap)
{
	struct usb_device_driver *udrv;

	/* interface drivers never match devices */
	if (!drvwrap->for_devices)
		return 0;

	udrv = container_of(drvwrap, struct usb_device_driver, drvwrap);

	/* If the device driver under consideration does not have a
		* id_table or a match function, then let the driver's probe
		* function decide.
		*/
	if (!udrv->id_table && !udrv->match)
		return 1;

	return usb_driver_applicable(udev, udrv);
}

int usb_interface_match(struct usb_interface *intf, struct usbdrv_wrap *drvwrap)
{
	struct usb_driver *usb_drv;
	const struct usb_device_id *id;

	/* device drivers never match interfaces */
	if (drvwrap->for_devices)
		return 0;

	usb_drv = container_of(drvwrap, struct usb_driver, drvwrap);

	id = usb_match_id(intf, usb_drv->id_table);
	if (id)
		return 1;

	return 0;
}

/**
 * usb_release_dev - free a usb device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this usb device are
 * done.
 */
void usb_release_dev(struct usb_device *udev)
{
	usb_destroy_configuration(udev);
	usb_bus_put(udev->bus);

	if (udev->ref)
		panic("ERROR: usb device reference counter non-zero %d\n", udev->ref);

	free(udev->product);
	free(udev->manufacturer);
	free(udev->serial);
	free(udev);
}

/**
 * usb_alloc_dev - usb device constructor (usbcore-internal)
 * @parent: hub to which device is connected; null to allocate a root hub
 * @bus: bus used to access the device
 * @port1: one-based index of port; ignored for root hubs
 * Context: !in_interrupt ()
 *
 * Only hub drivers (including virtual root hub drivers for host
 * controllers) should ever call this.
 *
 * This call may not be used in a non-sleeping context.
 */
struct usb_device *
usb_alloc_dev(struct usb_device *parent, struct usb_bus *bus, unsigned port1)
{
	struct usb_device *dev;

	dev = malloc(sizeof(*dev));
	if (!dev)
		return NULL;

	memset(dev, 0, sizeof(*dev));

	bus = usb_bus_get(bus);
	if (!bus) {
		free(dev);
		return NULL;
	}

	dev->state = USB_STATE_ATTACHED;

	INIT_LIST_HEAD(&dev->ep0.urb_list);
	dev->ep0.desc.bLength = USB_DT_ENDPOINT_SIZE;
	dev->ep0.desc.bDescriptorType = USB_DT_ENDPOINT;
	/* ep0 maxpacket comes later, from device descriptor */
	dev->ep_in[0] = dev->ep_out[0] = &dev->ep0;
	dev->ep0.enabled = 1;

	/* Save readable and stable topology id, distinguishing devices
	 * by location for diagnostics, tools, driver model, etc.  The
	 * string is a path along hub ports, from the root.  Each device's
	 * dev->devpath will be stable until USB is re-cabled, and hubs
	 * are often labeled with these port numbers.  The bus_id isn't
	 * as stable:  bus->busnum changes easily from modprobe order,
	 * cardbus or pci hotplugging, and so on.
	 */
	if (unlikely (!parent)) {
		dev->devpath [0] = '0';

	} else {
		/* match any labeling on the hubs; it's one-based */
		if (parent->devpath [0] == '0')
			snprintf (dev->devpath, sizeof dev->devpath,
				"%d", port1);
		else
			snprintf (dev->devpath, sizeof dev->devpath,
				"%s.%d", parent->devpath, port1);

		/* hub driver sets up TT records */
	}

	dev->portnum = port1;
	dev->bus = bus;
	dev->parent = parent;

	mutex_init(&dev->mutex);

	return dev;
}

void usb_free_dev(struct usb_device *dev)
{
	if (dev->ref)
		panic("ERROR: usb dev reference counter non-zero %d\n", dev->ref);

	usb_bus_put(dev->bus);

	free(dev);
}

/**
 * usb_get_dev - increments the reference count of the usb device structure
 * @dev: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for USB interfaces should normally record such references in
 * their probe() methods, when they bind to an interface, and release
 * them by calling usb_put_dev(), in their disconnect() methods.
 *
 * A pointer to the device with the incremented reference counter is returned.
 */
struct usb_device *usb_get_dev(struct usb_device *dev)
{
	if (dev)
		dev->ref++;
	return dev;
}

/**
 * usb_put_dev - release a use of the usb device structure
 * @dev: device that's been disconnected
 *
 * Must be called when a user of a device is finished with it.  When the last
 * user of the device calls this function, the memory of the device is freed.
 */
void usb_put_dev(struct usb_device *dev)
{
	if (dev)
		dev->ref--;
}

/**
 * usb_get_intf - increments the reference count of the usb interface structure
 * @intf: the interface being referenced
 *
 * Each live reference to a interface must be refcounted.
 *
 * Drivers for USB interfaces should normally record such references in
 * their probe() methods, when they bind to an interface, and release
 * them by calling usb_put_intf(), in their disconnect() methods.
 *
 * A pointer to the interface with the incremented reference counter is
 * returned.
 */
struct usb_interface *usb_get_intf(struct usb_interface *intf)
{
	if (intf)
		intf->ref++;
	return intf;
}

/**
 * usb_put_intf - release a use of the usb interface structure
 * @intf: interface that's been decremented
 *
 * Must be called when a user of an interface is finished with it.  When the
 * last user of the interface calls this function, the memory of the interface
 * is freed.
 */
void usb_put_intf(struct usb_interface *intf)
{
	if (intf)
		intf->ref--;
}

/**
 * usb_lock_device - acquire the lock for a usb device structure
 * @udev: device that's being locked
 *
 * Use this routine when you don't hold any other device locks;
 * to acquire nested inner locks call mutex_lock(&udev->mutex) directly.
 * This is necessary for proper interaction with usb_lock_all_devices().
 */
void usb_lock_device(struct usb_device *udev)
{
	/* check usb_lock_all_devices */
	mutex_lock(&usb_all_devices_mutex);

	while (usb_all_devices_state < 0)
		thread_cond_wait(&usb_all_devices_cond, &usb_all_devices_mutex);

	++usb_all_devices_state;
	mutex_unlock(&usb_all_devices_mutex);

	/* usb_device lock */
	mutex_lock(&udev->mutex);
}

/**
 * usb_trylock_device - attempt to acquire the lock for a usb device structure
 * @udev: device that's being locked
 *
 * Don't use this routine if you already hold a device lock;
 * use mutex_try_lock(&udev->mutex) instead.
 * This is necessary for proper interaction with usb_lock_all_devices().
 *
 * Returns 1 if successful, 0 if contention.
 */
int usb_trylock_device(struct usb_device *udev)
{
	mutex_lock(&usb_all_devices_mutex);

	if (usb_all_devices_state < 0) {
		mutex_unlock(&usb_all_devices_mutex);
		return 0;
	}

	if (!mutex_try_lock(&udev->mutex)) {
		mutex_unlock(&usb_all_devices_mutex);
		return 0;
	}

	++usb_all_devices_state;
	mutex_unlock(&usb_all_devices_mutex);

	return 1;
}

/**
 * usb_lock_device_for_reset - cautiously acquire the lock for a
 *	usb device structure
 * @udev: device that's being locked
 * @iface: interface bound to the driver making the request (optional)
 *
 * Attempts to acquire the device lock, but fails if the device is
 * NOTATTACHED or SUSPENDED, or if iface is specified and the interface
 * is neither BINDING nor BOUND.  Rather than sleeping to wait for the
 * lock, the routine polls repeatedly.  This is to prevent deadlock with
 * disconnect; in some drivers (such as usb-storage) the disconnect()
 * callback will block waiting for a device reset to complete.
 *
 * Returns a negative error code for failure, otherwise 1 or 0 to indicate
 * that the device will or will not have to be unlocked.  (0 can be
 * returned when an interface is given and is BINDING, because in that
 * case the driver already owns the device lock.)
 */
int usb_lock_device_for_reset(struct usb_device *udev,
		struct usb_interface *iface)
{
	if (udev->state == USB_STATE_NOTATTACHED)
		return -ENODEV;
	if (udev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;
	if (iface) {
		switch (iface->condition) {
		  case USB_INTERFACE_BINDING:
			return 0;
		  case USB_INTERFACE_BOUND:
			break;
		  default:
			return -EINTR;
		}
	}

	while (!usb_trylock_device(udev)) {
		msleep(15);
		if (udev->state == USB_STATE_NOTATTACHED)
			return -ENODEV;
		if (udev->state == USB_STATE_SUSPENDED)
			return -EHOSTUNREACH;
		if (iface && iface->condition != USB_INTERFACE_BOUND)
			return -EINTR;
	}
	return 1;
}

/**
 * usb_unlock_device - release the lock for a usb device structure
 * @udev: device that's being unlocked
 *
 * Use this routine when releasing the only device lock you hold;
 * to release inner nested locks call mutex_unlock(&udev->mutex) directly.
 * This is necessary for proper interaction with usb_lock_all_devices().
 */
void usb_unlock_device(struct usb_device *udev)
{
	/* usb_device unlock */
	mutex_unlock(&udev->mutex);

	/* check usb_lock_all_devices */
	mutex_lock(&usb_all_devices_mutex);

	if (usb_all_devices_state == 0)
		panic("usb_all_devices_state error\n");

	if (--usb_all_devices_state == 0)
		thread_cond_signal(&usb_all_devices_cond);

	mutex_unlock(&usb_all_devices_mutex);
}

/**
 * usb_lock_all_devices - acquire the lock for all usb device structures
 *
 * This is necessary when registering a new driver or probing a bus,
 * since the driver-model core may try to use any usb_device.
 */
void usb_lock_all_devices(void)
{
	mutex_lock(&usb_all_devices_mutex);

	while (usb_all_devices_state != 0)
		thread_cond_wait(&usb_all_devices_cond, &usb_all_devices_mutex);

	usb_all_devices_state = -1;
	mutex_unlock(&usb_all_devices_mutex);
}

/**
 * usb_unlock_all_devices - release the lock for all usb device structures
 */
void usb_unlock_all_devices(void)
{
	mutex_lock(&usb_all_devices_mutex);

	if (usb_all_devices_state >= 0)
		panic("usb_all_devices_state error\n");

	usb_all_devices_state = 0;
	thread_cond_broadcast(&usb_all_devices_cond);

	mutex_unlock(&usb_all_devices_mutex);
}

/**
 * usb_get_current_frame_number - return current bus frame number
 * @dev: the device whose bus is being queried
 *
 * Returns the current frame number for the USB host controller
 * used with the given USB device.  This can be used when scheduling
 * isochronous requests.
 *
 * Note that different kinds of host controller have different
 * "scheduling horizons".  While one type might support scheduling only
 * 32 frames into the future, others could support scheduling up to
 * 1024 frames into the future.
 */
int usb_get_current_frame_number(struct usb_device *dev)
{
	return usb_hcd_get_frame_number(dev);
}

/*-------------------------------------------------------------------*/
/*
 * usb_get_extra_descriptor() finds a descriptor of specific type in the
 * extra field of the interface and endpoint descriptor structs.
 */

int usb_get_extra_descriptor(char *buffer, unsigned size,
			       unsigned char type, void **ptr, size_t minsize)
{
	struct usb_descriptor_header *header;

	while (size >= sizeof(struct usb_descriptor_header)) {
		header = (struct usb_descriptor_header *)buffer;

		if (header->bLength < 2 || header->bLength > size) {
			printf("%s: bogus descriptor, type %d length %d\n",
				usbcore_name, header->bDescriptorType, header->bLength);
			return -1;
		}

		if (header->bDescriptorType == type && header->bLength >= minsize) {
			*ptr = header;
			return 0;
		}

		buffer += header->bLength;
		size -= header->bLength;
	}
	return -1;
}

/**
 * usb_alloc_coherent - allocate dma-consistent buffer for URB_NO_xxx_DMA_MAP
 * @dev: device the buffer will be used with
 * @size: requested buffer size
 * @dma: used to return DMA address of buffer
 *
 * Return: Either null (indicating no buffer could be allocated), or the
 * cpu-space pointer to a buffer that may be used to perform DMA to the
 * specified device.  Such cpu-space buffers are returned along with the DMA
 * address (through the pointer provided).
 *
 * Note:
 * These buffers are used with URB_NO_xxx_DMA_MAP set in urb->transfer_flags
 * to avoid behaviors like using "DMA bounce buffers", or thrashing IOMMU
 * hardware during URB completion/resubmit.  The implementation varies between
 * platforms, depending on details of how DMA will work to this device.
 * Using these buffers also eliminates cacheline sharing problems on
 * architectures where CPU caches are not DMA-coherent.  On systems without
 * bus-snooping caches, these buffers are uncached.
 *
 * When the buffer is no longer used, free it with usb_free_coherent().
 */
void *usb_alloc_coherent(struct usb_device *dev, size_t size, dma_addr_t *dma)
{
	void *data;
	struct usb_hcd		*hcd;

	if (!dev || !dev->bus)
		return NULL;

	if (size == 0)
		return NULL;

	hcd = bus_to_hcd(dev->bus);

	data = cache_align_malloc(size);

	if (dma)
		*dma = virt_to_phys(data);

	if (hcd_uses_dma(hcd))
		data = (void *)KSEG1ADDR(data);

	return data;
}

/**
 * usb_free_coherent - free memory allocated with usb_alloc_coherent()
 * @dev: device the buffer was used with
 * @size: requested buffer size
 * @addr: CPU address of buffer
 *
 * This reclaims an I/O buffer, letting it be reused.  The memory must have
 * been allocated using usb_alloc_coherent(), and the parameters must match
 * those provided in that allocation request.
 */
void usb_free_coherent(struct usb_device *dev, size_t size, void *addr)
{
	struct usb_hcd		*hcd;

	if (!dev || !dev->bus)
		return;

	if (!addr)
		return;

	hcd = bus_to_hcd(dev->bus);

	if (hcd_uses_dma(hcd))
		addr = (void *)KSEG0ADDR(addr);

	free(addr);
}

static int is_rndis(struct usb_interface_descriptor *desc)
{
	return desc->bInterfaceClass == USB_CLASS_COMM
		&& desc->bInterfaceSubClass == 2
		&& desc->bInterfaceProtocol == 0xff;
}

static int is_activesync(struct usb_interface_descriptor *desc)
{
	return desc->bInterfaceClass == USB_CLASS_MISC
		&& desc->bInterfaceSubClass == 1
		&& desc->bInterfaceProtocol == 1;
}

static bool is_audio(struct usb_interface_descriptor *desc)
{
	return desc->bInterfaceClass == USB_CLASS_AUDIO;
}

int usb_choose_configuration(struct usb_device *udev)
{
	int i;
	int num_configs;
	int insufficient_power = 0;
	struct usb_host_config *c, *best;

	best = NULL;
	c = udev->config;
	num_configs = udev->descriptor.bNumConfigurations;
	for (i = 0; i < num_configs; (i++, c++)) {
		struct usb_interface_descriptor	*desc = NULL;

		/* It's possible that a config has no interfaces! */
		if (c->desc.bNumInterfaces > 0)
			desc = &c->intf_cache[0]->altsetting->desc;

		if (desc && is_audio(desc)) {
			if (i == 0)
				best = c;

			/* Unconditional continue, because the rest of the code
			 * in the loop is irrelevant for audio devices, and
			 * because it can reassign best, which for audio devices
			 * we don't want.
			 */
			continue;
		}

		/* When the first config's first interface is one of Microsoft's
		 * pet nonstandard Ethernet-over-USB protocols, ignore it unless
		 * this kernel has enabled the necessary host side driver.
		 * But: Don't ignore it if it's the only config.
		 */
		if (i == 0 && num_configs > 1 && desc &&
				(is_rndis(desc) || is_activesync(desc))) {
			continue;
		}

		/* From the remaining configs, choose the first one whose
		 * first interface is for a non-vendor-specific class.
		 * Reason: Linux is more likely to have a class driver
		 * than a vendor-specific driver. */
		else if (udev->descriptor.bDeviceClass != USB_CLASS_VENDOR_SPEC &&
				(desc && desc->bInterfaceClass != USB_CLASS_VENDOR_SPEC)) {
			best = c;
			break;
		}

		/* If all the remaining configs are vendor-specific,
		 * choose the first one. */
		else if (!best)
			best = c;
	}

	if (insufficient_power > 0)
		printf("rejected %d configuration%s due to insufficient available bus power\n",
			insufficient_power, plural(insufficient_power));

	if (best) {
		i = best->desc.bConfigurationValue;
		printf("configuration #%d chosen from %d choice%s\n",
			i, num_configs, plural(num_configs));
	} else {
		i = -1;
		printf("no configuration chosen from %d choice%s\n",
			num_configs, plural(num_configs));
	}
	return i;
}

int __check_for_non_generic_match(struct usb_device *udev, struct usbdrv_wrap *drvwrap)
{
	struct usb_device_driver *udrv;

	/* interface drivers never match devices */
	if (!drvwrap->for_devices)
		return 0;

	udrv = container_of(drvwrap, struct usb_device_driver, drvwrap);
	if (udrv == &usb_generic_driver)
		return 0;

	return usb_driver_applicable(udev, udrv);
}

static bool usb_generic_driver_match(struct usb_device *udev)
{
	struct usbdrv_wrap *drvwrap;

	list_for_each_entry(drvwrap, &usb_driver_list, list) {
		if (__check_for_non_generic_match(udev, drvwrap)) {
			return false;
		}
	}

	return true;
}

int usb_generic_driver_probe(struct usb_device *udev)
{
	int err, c;

	c = usb_choose_configuration(udev);
	if (c >= 0) {
		err = usb_set_configuration(udev, c);
		if (err) {
			printf("usb generic driver can't set config #%d, error %d\n", c, err);
			return err;
		}
	} else {
		return -ENODEV;
	}

	return 0;
}

void usb_generic_driver_disconnect(struct usb_device *udev)
{
	/* if this is only an unbind, not a physical disconnect, then
	 * unconfigure the device */
	if (udev->actconfig)
		usb_set_configuration(udev, -1);
}

struct usb_device_driver usb_generic_driver = {
	.name =	"usb",
	.match = usb_generic_driver_match,
	.probe = usb_generic_driver_probe,
	.disconnect = usb_generic_driver_disconnect,
};

extern void usb_hub_init(void);
extern void usb_hub_cleanup(void);

#if CONFIG_USB_HOST_HID_MOUSE
extern void usb_hid_mouse_driver_register(void);
extern void usb_hid_mouse_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_HID_KEYBOARD
extern void usb_hid_kbd_driver_register(void);
extern void usb_hid_kbd_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_HID_VENDOR
extern void usb_hid_vendor_driver_register(void);
extern void usb_hid_vendor_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_CDC_ACM
extern void usb_cdc_acm_driver_register(void);
extern void usb_cdc_acm_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_CH341
extern void usb_ch341_driver_register(void);
extern void usb_ch341_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_WWAN
extern void usb_wwan_driver_register(void);
extern void usb_wwan_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_UVC
void usb_uvc_driver_register(void);
void usb_uvc_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_UAC
void usb_uac_driver_register(void);
void usb_uac_driver_deregister(void);
#endif

#if CONFIG_USB_HOST_MASS_STORAGE
extern void usb_mass_storage_driver_register(void);
extern void usb_mass_storage_driver_deregister(void);
#endif

void usb_host_init(void)
{
	usb_hub_init();

	usb_register_device_driver(&usb_generic_driver);

#if CONFIG_USB_HOST_HID_MOUSE
	usb_hid_mouse_driver_register();
#endif

#if CONFIG_USB_HOST_HID_KEYBOARD
	usb_hid_kbd_driver_register();
#endif

#if CONFIG_USB_HOST_HID_VENDOR
	usb_hid_vendor_driver_register();
#endif

#if CONFIG_USB_HOST_CDC_ACM
	usb_cdc_acm_driver_register();
#endif

#if CONFIG_USB_HOST_CH341
	usb_ch341_driver_register();
#endif

#if CONFIG_USB_HOST_WWAN
	usb_wwan_driver_register();
#endif

#if CONFIG_USB_HOST_UVC
	usb_uvc_driver_register();
#endif

#if CONFIG_USB_HOST_UAC
	usb_uac_driver_register();
#endif

#if CONFIG_USB_HOST_MASS_STORAGE
	usb_mass_storage_driver_register();
#endif
}

void usb_host_exit(void)
{
#if CONFIG_USB_HOST_MASS_STORAGE
	usb_mass_storage_driver_deregister();
#endif

#if CONFIG_USB_HOST_UVC
	usb_uvc_driver_deregister();
#endif

#if CONFIG_USB_HOST_UAC
	usb_uac_driver_deregister();
#endif

#if CONFIG_USB_HOST_CDC_ACM
	usb_cdc_acm_driver_deregister();
#endif

#if CONFIG_USB_HOST_CH341
	usb_ch341_driver_deregister();
#endif

#if CONFIG_USB_HOST_WWAN
	usb_wwan_driver_deregister();
#endif

#if CONFIG_USB_HOST_HID_MOUSE
	usb_hid_mouse_driver_deregister();
#endif

#if CONFIG_USB_HOST_HID_KEYBOARD
	usb_hid_kbd_driver_deregister();
#endif

#if CONFIG_USB_HOST_HID_VENDOR
	usb_hid_vendor_driver_deregister();
#endif

	usb_deregister_device_driver(&usb_generic_driver);

	usb_hub_cleanup();
}
