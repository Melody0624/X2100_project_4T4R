#ifndef __HOST_USB_H
#define __HOST_USB_H

#include "../usb.h"

extern void *usb_alloc_coherent(struct usb_device *dev, size_t size, dma_addr_t *dma);
extern void usb_free_coherent(struct usb_device *dev, size_t size, void *addr);

extern void usb_disable_endpoint (struct usb_device *dev, unsigned int epaddr);
extern void usb_disable_interface (struct usb_device *dev,
		struct usb_interface *intf);
extern void usb_disable_device (struct usb_device *dev, int skip_ep0);

extern int usb_get_device_descriptor(struct usb_device *dev,
		unsigned int size);
extern int usb_set_configuration(struct usb_device *dev, int configuration);

extern void usb_lock_all_devices(void);
extern void usb_unlock_all_devices(void);

extern void usb_kick_khubd(struct usb_device *dev);
extern void usb_resume_root_hub(struct usb_device *dev);

extern int usb_device_match(struct usb_device *udev, struct usbdrv_wrap *drvwrap);
extern int usb_interface_match(struct usb_interface *intf, struct usbdrv_wrap *drvwrap);

extern void usb_release_dev(struct usb_device *udev);

extern struct list_head usb_driver_list;

extern void usb_api_blocking_completion(struct urb *urb);
extern int usb_start_wait_urb(struct urb *urb, int timeout, int* actual_length);

/* for labeling diagnostics */
extern const char *usbcore_name;

#endif