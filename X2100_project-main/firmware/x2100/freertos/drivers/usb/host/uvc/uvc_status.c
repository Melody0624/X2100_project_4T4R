#include "uvcvideo.h"

static void uvc_status_complete(struct urb *urb)
{
	struct uvc_device *dev = urb->context;
	int len, ret;

	switch (urb->status) {
	case 0:
		break;

	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -EPROTO:
		return;

	default:
		printf("Non-zero status (%d) in status completion handler.\n", urb->status);
		return;
	}

	len = urb->actual_length;
	if (len > 0) {
		if (dev->notify_callback)
			dev->notify_callback(dev->minor, dev->status, len);
	}

	/* Resubmit the URB. */
	urb->interval = dev->int_ep->desc.bInterval;
	ret = usb_submit_urb(urb);
	if (ret < 0)
		printf("Failed to resubmit status URB (%d).\n", ret);
}

int uvc_status_init(struct uvc_device *dev)
{
	struct usb_host_endpoint *ep = dev->int_ep;

	if (ep == NULL)
		return 0;

	dev->status = cache_align_malloc(sizeof(*dev->status));
	if (!dev->status)
		return -ENOMEM;
	memset(dev->status, 0, sizeof(*dev->status));

	dev->int_urb = usb_alloc_urb(0);
	if (!dev->int_urb) {
		free(dev->status);
		dev->status = NULL;
		return -ENOMEM;
	}

	/*
	 * For high-speed interrupt endpoints, the bInterval value is used as
	 * an exponent of two. Some developers forgot about it.
	 */
	usb_fill_int_urb(dev->int_urb, dev->udev, usb_rcvintpipe(dev->udev, ep->desc.bEndpointAddress),
		dev->status, sizeof(*dev->status), uvc_status_complete,
		dev, ep->desc.bInterval);

	return 0;
}

void uvc_status_unregister(struct uvc_device *dev)
{
	if (dev->int_urb)
		usb_kill_urb(dev->int_urb);
}

void uvc_status_cleanup(struct uvc_device *dev)
{
	if (dev->int_urb) {
		usb_free_urb(dev->int_urb);
		free(dev->status);
	}
}

int uvc_status_start(struct uvc_device *dev)
{
	if (dev->int_urb == NULL)
		return 0;

	return usb_submit_urb(dev->int_urb);
}

void uvc_status_stop(struct uvc_device *dev)
{
	if (dev->int_urb)
		usb_kill_urb(dev->int_urb);
}