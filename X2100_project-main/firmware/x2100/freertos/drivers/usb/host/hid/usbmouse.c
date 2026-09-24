#include "../usb.h"
#include <usb/host_mouse.h>

/*
 * USB HID (Human Interface Device) interface class code
 */

#define USB_INTERFACE_CLASS_HID         3

/*
 * USB HID interface subclass and protocol codes
 */

#define USB_INTERFACE_SUBCLASS_BOOT     1
#define USB_INTERFACE_PROTOCOL_MOUSE    2

struct usb_mouse {
	char name[128];
	struct usb_device *usbdev;
	struct urb *irq;

	char *data;
	dma_addr_t data_dma;
};

static mouse_notify_callback_t g_notify_callback;

void usb_mouse_register_callback(mouse_notify_callback_t callback)
{
	g_notify_callback = callback;
}

static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse *mouse = urb->context;
	char *data = mouse->data;
	int status;
	mouse_notify_callback_t notify_callback = g_notify_callback;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
	case -EPROTO:
	case -EPIPE:
		return;
	default:		/* error */
		printf("Error: %s urb->status %d\n", __func__, urb->status);
		goto resubmit;
	}

	if (notify_callback && (urb->actual_length >= 4)) {
		notify_callback((unsigned char)data[0], data[1], data[2], data[3]);
	}

resubmit:
	status = usb_submit_urb (urb);
	if (status)
		printf("can't resubmit intr, in %s:%d, status %d\n", __FILE__, __LINE__, status);
}

static int usb_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = intf->usb_dev;
	struct usb_host_interface *interface = NULL;
	struct usb_endpoint_descriptor *endpoint = NULL;
	struct usb_mouse *mouse = NULL;
	int pipe = 0, maxp = 0;
	int error = -ENOMEM;
	int i = 0;

	interface = intf->cur_altsetting;

	if (interface->desc.bNumEndpoints < 1)
		return -ENODEV;

	for (i = 0; i < interface->desc.bNumEndpoints; i++) {
		endpoint = &interface->endpoint[i].desc;
		if (usb_endpoint_is_int_in(endpoint)) {
			break;
		}
	}

	if (i == interface->desc.bNumEndpoints) {
		return -ENODEV;
	}

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	mouse = malloc(sizeof(struct usb_mouse));
	if (!mouse)
		goto fail1;

	memset(mouse, 0, sizeof(struct usb_mouse));

	mouse->data = usb_alloc_coherent(dev, 8, &mouse->data_dma);
	if (!mouse->data)
		goto fail1;

	mouse->irq = usb_alloc_urb(0);
	if (!mouse->irq)
		goto fail2;

	mouse->usbdev = dev;

	if (dev->manufacturer)
		strlcpy(mouse->name, dev->manufacturer, sizeof(mouse->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(mouse->name, " ", sizeof(mouse->name));
		strlcat(mouse->name, dev->product, sizeof(mouse->name));
	}

	if (!strlen(mouse->name))
		snprintf(mouse->name, sizeof(mouse->name),
			 "USB HIDBP Mouse %04x:%04x",
			 dev->descriptor.idVendor,
			 dev->descriptor.idProduct);

	usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data,
			 (maxp > 8 ? 8 : maxp),
			 usb_mouse_irq, mouse, endpoint->bInterval);
	mouse->irq->transfer_dma = mouse->data_dma;
	mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	intf->private_data = mouse;

	if (usb_submit_urb(mouse->irq))
		return -EIO;

	return 0;

fail2:
	usb_free_coherent(dev, 8, mouse->data);
fail1:
	free(mouse);
	return error;
}

static void usb_mouse_disconnect(struct usb_interface *intf)
{
	struct usb_mouse *mouse = intf->private_data;

	intf->private_data = NULL;
	if (mouse) {
		usb_kill_urb(mouse->irq);
		usb_free_urb(mouse->irq);
		usb_free_coherent(intf->usb_dev, 8, mouse->data);
		free(mouse);
	}
}

static const struct usb_device_id usb_mouse_id_table[] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};


static struct usb_driver usb_mouse_driver = {
	.name		= "usbmouse",
	.probe		= usb_mouse_probe,
	.disconnect	= usb_mouse_disconnect,
	.id_table	= usb_mouse_id_table,
};

void usb_hid_mouse_driver_register(void)
{
	usb_register_driver(&usb_mouse_driver);
}

void usb_hid_mouse_driver_deregister(void)
{
	usb_deregister(&usb_mouse_driver);
}
