#include <common.h>
#include <os.h>
#include <assert.h>
#include <driver/cache.h>

#include "uvc.h"
#include "uvc_video.h"

#define QUEUE_SIZE		8
/* --------------------------------------------------------------------------
 * Function descriptors
 */

/* string IDs are assigned dynamically */

#define UVC_STRING_CONTROL_IDX			0
#define UVC_STRING_STREAMING_IDX		1

static struct usb_string uvc_en_us_strings[] = {
	[UVC_STRING_CONTROL_IDX].s = "UVC Camera",
	[UVC_STRING_STREAMING_IDX].s = "Video Streaming",
	{  }
};

static struct usb_gadget_strings uvc_stringtab = {
	.language = 0x0409,	/* en-us */
	.strings = uvc_en_us_strings,
};

static struct usb_gadget_strings *uvc_function_strings[] = {
	&uvc_stringtab,
	NULL,
};

static struct usb_interface_assoc_descriptor uvc_iad = {
	.bLength		= sizeof(uvc_iad),
	.bDescriptorType	= USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface	= 0,
	.bInterfaceCount	= 2,
	.bFunctionClass		= USB_CLASS_VIDEO,
	.bFunctionSubClass	= UVC_SC_VIDEO_INTERFACE_COLLECTION,
	.bFunctionProtocol	= 0x00,
	.iFunction		= 0,
};

static struct usb_interface_descriptor uvc_control_intf = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_VIDEO,
	.bInterfaceSubClass	= UVC_SC_VIDEOCONTROL,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

static struct usb_interface_descriptor uvc_streaming_intf_alt0 = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bAlternateSetting	= 0,
	.bNumEndpoints		= 0,
	.bInterfaceClass	= USB_CLASS_VIDEO,
	.bInterfaceSubClass	= UVC_SC_VIDEOSTREAMING,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

static struct usb_interface_descriptor uvc_streaming_intf_alt1 = {
	.bLength		= USB_DT_INTERFACE_SIZE,
	.bDescriptorType	= USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bAlternateSetting	= 1,
	.bNumEndpoints		= 1,
	.bInterfaceClass	= USB_CLASS_VIDEO,
	.bInterfaceSubClass	= UVC_SC_VIDEOSTREAMING,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

static struct usb_endpoint_descriptor uvc_fs_streaming_ep = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_ISOC,
	/* The wMaxPacketSize and bInterval values will be initialized from
	 * module parameters.
	 */
};

static struct usb_endpoint_descriptor uvc_hs_streaming_ep = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_SYNC_ASYNC
				| USB_ENDPOINT_XFER_ISOC,
	/* The wMaxPacketSize and bInterval values will be initialized from
	 * module parameters.
	 */
};

static const struct usb_descriptor_header * const uvc_fs_streaming[] = {
	(struct usb_descriptor_header *) &uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *) &uvc_fs_streaming_ep,
	NULL,
};

static const struct usb_descriptor_header * const uvc_hs_streaming[] = {
	(struct usb_descriptor_header *) &uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *) &uvc_hs_streaming_ep,
	NULL,
};

/* --------------------------------------------------------------------------
 * Control requests
 */

static void
uvc_fill_streaming_control(struct uvc_device *uvc,
			   struct uvc_streaming_control *ctrl,
			   int iformat, int iframe, unsigned int ival)
{
	const struct uvc_function_config_format *format;
	const struct uvc_function_config_frame *frame;
	unsigned int i;

	/*
	 * Restrict the iformat, iframe and ival to valid values. Negative
	 * values for iformat or iframe will result in the maximum valid value
	 * being selected.
	 */
	iformat = clamp((unsigned int)iformat, 1U,
					uvc->uvc_config_stream->num_formats);
	format = &uvc->uvc_config_stream->formats[iformat-1];

	iframe = clamp((unsigned int)iframe, 1U, format->num_frames);
	frame = &format->frames[iframe-1];

	for (i = 0; i < frame->num_intervals; ++i) {
		if (ival <= frame->intervals[i]) {
			ival = frame->intervals[i];
			break;
		}
	}

	if (i == frame->num_intervals)
		ival = frame->intervals[frame->num_intervals-1];

	memset(ctrl, 0, sizeof *ctrl);

	ctrl->bmHint = 1;
	ctrl->bFormatIndex = iformat;
	ctrl->bFrameIndex = iframe;
	ctrl->dwFrameInterval = ival;

	ctrl->dwMaxVideoFrameSize = frame->width * frame->height * format->bpp / 8;
	ctrl->dwMaxPayloadTransferSize = uvc->streaming_maxpacket;
	ctrl->bmFramingInfo = 0;
	ctrl->bPreferedVersion = 5;
	ctrl->bMinVersion = 1;
	ctrl->bMaxVersion = 5;
	ctrl->wCompQuality = 0;
	ctrl->dwClockFrequency = CONFIG_EXTAL_CLOCK;
}

static int uvc_v4l2_streamon(struct uvc_device *uvc)
{

	struct uvc_video *video = &uvc->video;

	return uvcg_video_enable(video, 1);
}

static int uvc_v4l2_streamoff(struct uvc_device *uvc)
{
	struct uvc_video *video = &uvc->video;

	return uvcg_video_enable(video, 0);
}

static void uvc_function_ep0_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uvc_device *uvc = req->context;
	const struct uvc_streaming_control *ctrl = req->buf;
	struct uvc_streaming_control *target = NULL;

	if (uvc->setup_request.event_out) {
		uvc->setup_request.event_out = 0;
		if (uvc->setup_interface == uvc->streaming_intf) {
			switch (uvc->setup_request.control_selector) {
				case UVC_VS_PROBE_CONTROL:
					target = &uvc->probe;
					break;

				case UVC_VS_COMMIT_CONTROL:
					target = &uvc->commit;
					break;

				default:
					printf("setting unknown control, length = %d\n", req->actual);
					return;
			}

			uvc_fill_streaming_control(uvc, target, ctrl->bFormatIndex,
						ctrl->bFrameIndex, ctrl->dwFrameInterval);

			if (uvc->setup_request.control_selector == UVC_VS_COMMIT_CONTROL) {
				const struct uvc_function_config_format *format;
				const struct uvc_function_config_frame *frame;
				unsigned long flags;
				unsigned int fps;

				format = &uvc->uvc_config_stream->formats[target->bFormatIndex-1];
				frame = &format->frames[target->bFrameIndex-1];

				usb_spin_lock_irqsave(&uvc->video.lock, flags);
				uvc->video.format.fcc =  format->fcc;
				uvc->video.format.width = frame->width;
				uvc->video.format.height = frame->height;
				uvc->video.format.bpp = format->bpp;

				/* fps is guaranteed to be non-zero and thus valid. */
				fps = 10000000 / target->dwFrameInterval;
				uvc->video.format.fps = fps;
				usb_spin_unlock_irqrestore(&uvc->video.lock, flags);

				if (uvc->uvc_callback.format_cb)
					uvc->uvc_callback.format_cb(&uvc->video.format);
			}

		} else if (uvc->setup_interface == uvc->control_intf) {
			uvc->setup_request.buf_actual = req->actual;
			if (uvc->uvc_callback.param_cb)
				uvc->uvc_callback.param_cb(&uvc->setup_request);
		}
	}
}

static int uvc_streaming_setup(struct uvc_device *uvc)
{
	int len;
	short *data = uvc->setup_request.buf;

	len = min_t(uint32_t, uvc->setup_request.request_len, sizeof(struct uvc_streaming_control));

	if (UVC_VS_PROBE_CONTROL == uvc->setup_request.control_selector) {
		switch (uvc->setup_request.request) {
			case UVC_SET_CUR:
				break;
			case UVC_GET_CUR:
				memcpy(uvc->setup_request.buf, &uvc->probe, len);
				break;
			case UVC_GET_MAX:
				uvc_fill_streaming_control(uvc, uvc->setup_request.buf, -1, -1, 0xFFFFFFFF);
				break;
			case UVC_GET_MIN:
			case UVC_GET_DEF:
				uvc_fill_streaming_control(uvc, uvc->setup_request.buf, 1, 1, 0);
				break;
			case UVC_GET_RES:
				memset(uvc->setup_request.buf, 0, len);
				break;
			case UVC_GET_LEN:
				len = min_t(uint32_t, uvc->setup_request.request_len, 2);
				*data = sizeof(struct uvc_streaming_control);
				break;
			case UVC_GET_INFO:
				len = min_t(uint32_t, uvc->setup_request.request_len, 1);
				((u8 *) uvc->setup_request.buf)[0] = 0x03;
				break;
			default:
				len = -EOPNOTSUPP;
				break;
		}
	} else if (UVC_VS_COMMIT_CONTROL == uvc->setup_request.control_selector) {
		switch (uvc->setup_request.request) {
			case UVC_SET_CUR:
				break;
			case UVC_GET_CUR:
				memcpy(uvc->setup_request.buf, &uvc->commit, len);
				break;
			case UVC_GET_LEN:
				len = min_t(uint32_t, uvc->setup_request.request_len, 2);
				*data = sizeof(struct uvc_streaming_control);
				break;
			case UVC_GET_INFO:
				len = min_t(uint32_t, uvc->setup_request.request_len, 1);
				((u8 *) uvc->setup_request.buf)[0] = 0x03;
				break;
			default:
				len = -EOPNOTSUPP;
				break;
		}
	} else {
		len = -EOPNOTSUPP;
	}

	return len;
}

static int uvc_function_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	int value = -EOPNOTSUPP;
	struct uvc_device *uvc = to_uvc(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request *req = uvc->control_req;

	if ((ctrl->bRequestType & USB_TYPE_MASK) != USB_TYPE_CLASS) {
		printf("gadget uvc invalid request type\n");
		return -EINVAL;
	}

	/* Stall too big requests. */
	if (ctrl->wLength > UVC_MAX_REQUEST_SIZE)
		return -EINVAL;

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		printf("gadget uvc standard request\n");
		break;

	case USB_TYPE_CLASS:
		if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
			break;

		uvc->setup_interface = ctrl->wIndex & 0xff;
		uvc->setup_request.event_out = !(ctrl->bRequestType & USB_DIR_IN);
		uvc->setup_request.control_selector = ctrl->wValue >> 8;
		uvc->setup_request.entity_id = ctrl->wIndex >> 8;
		uvc->setup_request.request = ctrl->bRequest;
		uvc->setup_request.request_len = ctrl->wLength;
		uvc->setup_request.buf = req->buf;
		uvc->setup_request.buf_actual = 0;

		if (uvc->setup_interface == uvc->streaming_intf) {
			value = uvc_streaming_setup(uvc);
		} else if (uvc->setup_interface == uvc->control_intf) {
			if (uvc->uvc_callback.param_cb) {
				if (uvc->setup_request.event_out == 0)
					value = uvc->uvc_callback.param_cb(&uvc->setup_request);
				else
					value = uvc->setup_request.request_len;
			} else {
				printf("param_callblack is null,no support control interface request\n");
			}

		} else {
			printf("invalid interface req%02x.%02x v%04x i%04x l%d\n",
			 ctrl->bRequestType, ctrl->bRequest, ctrl->wValue, ctrl->wIndex, ctrl->wLength);
		}
		break;

	default:
		printf("invalid control req%02x.%02x v%04x i%04x l%d\n",
			 ctrl->bRequestType, ctrl->bRequest, ctrl->wValue, ctrl->wIndex, ctrl->wLength);
		break;
	}

	if (value >= 0) {
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req);
		if (value < 0)
			printf("usb_ep_queue error on ep0 %d\n", value);
	}

	return value;
}

static int uvc_function_get_alt(struct usb_function *f, unsigned interface)
{
	struct uvc_device *uvc = to_uvc(f);

	if (interface == uvc->control_intf)
		return 0;
	else if (interface != uvc->streaming_intf)
		return -EINVAL;
	else
		return uvc->video.ep->enabled ? 1 : 0;
}

static int uvc_function_set_alt(struct usb_function *f, unsigned interface, unsigned alt)
{
	struct uvc_device *uvc = to_uvc(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	if (interface == uvc->control_intf) {
		if (alt)
			return -EINVAL;

		if (uvc->state == UVC_STATE_DISCONNECTED) {
			uvc->state = UVC_STATE_CONNECTED;
			if (uvc->uvc_callback.connect_cb)
				uvc->uvc_callback.connect_cb(1);
			thread_waiter_wakeup(&uvc->connect_wait);
		}

		return 0;
	}

	if (interface != uvc->streaming_intf)
		return -EINVAL;

	switch (alt) {
	case 0:
		if (uvc->state != UVC_STATE_STREAMING)
			return 0;

		if (uvc->uvc_callback.stream_cb) {
			ret = uvc->uvc_callback.stream_cb(0);
			if (ret)
				printf("%s: stop video stream error %d.\n", __func__, ret);
		}

		if (uvc->video.ep)
			usb_ep_disable(uvc->video.ep);


		ret = uvc_v4l2_streamoff(uvc);
		uvc->state = UVC_STATE_CONNECTED;
		thread_waiter_wakeup(&uvc->write_wait);
		printf("%s: stop video stream %d.\n", __func__, ret);
		return ret;

	case 1:
		if (uvc->state != UVC_STATE_CONNECTED)
			return 0;

		if (!uvc->video.ep)
			return -EINVAL;

		if (uvc->uvc_callback.stream_cb) {
			ret = uvc->uvc_callback.stream_cb(1);
			if (ret) {
				printf("%s: starting video stream error %d.\n", __func__, ret);
				return ret;
			}
		}

		usb_ep_disable(uvc->video.ep);

		ret = config_ep_by_speed(cdev->gadget, &(uvc->func), uvc->video.ep);
		if (ret) {
			printf("%s: starting video stream error %d.\n", __func__, ret);
			return ret;
		}

		usb_ep_enable(uvc->video.ep);

		ret = uvc_v4l2_streamon(uvc);
		if (ret == 0) {
			uvc->state = UVC_STATE_STREAMING;
			thread_waiter_wakeup(&uvc->stream_wait);
		}

		printf("%s: starting video stream %d.\n", __func__, ret);
		return ret;

	default:
		return -EINVAL;
	}
}

static void uvc_function_disable(struct usb_function *f)
{
	struct uvc_device *uvc = to_uvc(f);

	if (uvc->state == UVC_STATE_STREAMING)
		uvc_v4l2_streamoff(uvc);

	uvc->state = UVC_STATE_DISCONNECTED;
	if (uvc->uvc_callback.connect_cb)
		uvc->uvc_callback.connect_cb(0);

	usb_ep_disable(uvc->video.ep);

	thread_waiter_wakeup(&uvc->write_wait);
}

/* --------------------------------------------------------------------------
 * Connection / disconnection
 */

#define UVC_COPY_DESCRIPTOR(mem, dst, desc) \
	do { \
		memcpy(mem, desc, (desc)->bLength); \
		*(dst)++ = mem; \
		mem += (desc)->bLength; \
	} while (0);

#define UVC_COPY_DESCRIPTORS(mem, dst, src) \
	do { \
		const struct usb_descriptor_header * const *__src; \
		for (__src = src; *__src; ++__src) { \
			memcpy(mem, *__src, (*__src)->bLength); \
			*dst++ = mem; \
			mem += (*__src)->bLength; \
		} \
	} while (0)

static struct usb_descriptor_header **
uvc_copy_descriptors(struct uvc_device *uvc, enum usb_device_speed speed)
{
	struct uvc_input_header_descriptor *uvc_streaming_header;
	struct uvc_header_descriptor *uvc_control_header;
	const struct uvc_descriptor_header * const *uvc_control_desc;
	const struct uvc_descriptor_header * const *uvc_streaming_cls;
	const struct usb_descriptor_header * const *uvc_streaming_std;
	const struct usb_descriptor_header * const *src;
	struct usb_descriptor_header **dst;
	struct usb_descriptor_header **hdr;
	unsigned int control_size;
	unsigned int streaming_size;
	unsigned int n_desc;
	unsigned int bytes;
	void *mem;

	switch (speed) {
	case USB_SPEED_HIGH:
		uvc_control_desc = uvc->desc.fs_control;
		uvc_streaming_cls = uvc->desc.hs_streaming;
		uvc_streaming_std = uvc_hs_streaming;
		break;

	case USB_SPEED_FULL:
	default:
		uvc_control_desc = uvc->desc.fs_control;
		uvc_streaming_cls = uvc->desc.fs_streaming;
		uvc_streaming_std = uvc_fs_streaming;
		break;
	}

	if (!uvc_control_desc || !uvc_streaming_cls)
		return ERR_PTR(-ENODEV);

	/* Descriptors layout
	 *
	 * uvc_iad
	 * uvc_control_intf
	 * Class-specific UVC control descriptors
	 * uvc_streaming_intf_alt0
	 * Class-specific UVC streaming descriptors
	 * uvc_{fs|hs}_streaming
	 */

	/* Count descriptors and compute their size. */
	control_size = 0;
	streaming_size = 0;
	bytes = uvc_iad.bLength + uvc_control_intf.bLength
	      + uvc_streaming_intf_alt0.bLength;
	n_desc = 5;

	for (src = (const struct usb_descriptor_header **)uvc_control_desc; *src; ++src) {
		control_size += (*src)->bLength;
		bytes += (*src)->bLength;
		n_desc++;
	}
	for (src = (const struct usb_descriptor_header **)uvc_streaming_cls; *src; ++src) {
		streaming_size += (*src)->bLength;
		bytes += (*src)->bLength;
		n_desc++;
	}
	for (src = uvc_streaming_std; *src; ++src) {
		bytes += (*src)->bLength;
		n_desc++;
	}

	mem = malloc((n_desc + 1) * sizeof(*src) + bytes);
	if (mem == NULL)
		return NULL;

	hdr = mem;
	dst = mem;
	mem += (n_desc + 1) * sizeof(*src);

	/* Copy the descriptors. */
	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_iad);
	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_control_intf);

	uvc_control_header = mem;
	UVC_COPY_DESCRIPTORS(mem, dst, (const struct usb_descriptor_header **)uvc_control_desc);
	uvc_control_header->wTotalLength = control_size;
	uvc_control_header->bInCollection = 1;
	uvc_control_header->baInterfaceNr[0] = uvc->streaming_intf;

	UVC_COPY_DESCRIPTOR(mem, dst, &uvc_streaming_intf_alt0);

	uvc_streaming_header = mem;
	UVC_COPY_DESCRIPTORS(mem, dst, (const struct usb_descriptor_header**)uvc_streaming_cls);
	uvc_streaming_header->wTotalLength = streaming_size;
	uvc_streaming_header->bEndpointAddress = uvc->video.ep->address;

	UVC_COPY_DESCRIPTORS(mem, dst, uvc_streaming_std);

	*dst = NULL;
	return hdr;
}

static int uvc_function_bind(struct usb_configuration *c, struct usb_function *f)
{
	const struct uvc_function_config_format *format;
	const struct uvc_function_config_frame *frame;
	unsigned int fps;

	struct usb_composite_dev *cdev = c->cdev;
	struct uvc_device *uvc = to_uvc(f);
	struct usb_string *us;
	unsigned int max_packet_mult;
	unsigned int max_packet_size;
	struct usb_ep *ep;
	int ret = -EINVAL;

	/* Sanity check the streaming endpoint module parameters.
	 */
	uvc->streaming_interval = clamp(uvc->streaming_interval, 1U, 16U);
	uvc->streaming_maxpacket = clamp(uvc->streaming_maxpacket, 1U, 3072U);

	/* Fill in the FS/HS/SS Video Streaming specific descriptors from the
	 * module parameters.
	 *
	 * NOTE: We assume that the user knows what they are doing and won't
	 * give parameters that their UDC doesn't support.
	 */
	if (uvc->streaming_maxpacket <= 1024) {
		max_packet_mult = 1;
		max_packet_size = uvc->streaming_maxpacket;
	} else if (uvc->streaming_maxpacket <= 2048) {
		max_packet_mult = 2;
		max_packet_size = uvc->streaming_maxpacket / 2;
	} else {
		max_packet_mult = 3;
		max_packet_size = uvc->streaming_maxpacket / 3;
	}

	uvc_fill_streaming_control(uvc, &uvc->probe, 1, 1, 0);
	uvc_fill_streaming_control(uvc, &uvc->commit, 1, 1, 0);

	format = &uvc->uvc_config_stream->formats[uvc->commit.bFormatIndex-1];
	frame = &format->frames[uvc->commit.bFrameIndex-1];

	uvc->video.format.fcc =  format->fcc;
	uvc->video.format.width = frame->width;
	uvc->video.format.height = frame->height;
	uvc->video.format.bpp = format->bpp;

	/* fps is guaranteed to be non-zero and thus valid. */
	fps = 10000000 / uvc->commit.dwFrameInterval;
	uvc->video.format.fps = fps;

	uvc_fs_streaming_ep.wMaxPacketSize = min(uvc->streaming_maxpacket, 1023U);
	uvc_fs_streaming_ep.bInterval = uvc->streaming_interval;

	uvc_hs_streaming_ep.wMaxPacketSize = max_packet_size | ((max_packet_mult - 1) << 11);
	uvc_hs_streaming_ep.bInterval = uvc->streaming_interval;

	/* A high-bandwidth endpoint must specify a bInterval value of 1 */
	if (max_packet_mult > 1)
		uvc_hs_streaming_ep.bInterval = 1;
	else
		uvc_hs_streaming_ep.bInterval = uvc->streaming_interval;

	/* Allocate endpoints. */
	if (gadget_is_dualspeed(cdev->gadget))
		ep = usb_ep_autoconfig(cdev->gadget, &uvc_hs_streaming_ep);
	else
		ep = usb_ep_autoconfig(cdev->gadget, &uvc_fs_streaming_ep);

	if (!ep) {
		printf("Unable to allocate streaming EP\n");
		goto error;
	}
	uvc->video.ep = ep;

	uvc_fs_streaming_ep.bEndpointAddress = uvc->video.ep->address;
	uvc_hs_streaming_ep.bEndpointAddress = uvc->video.ep->address;

	us = usb_gstrings_attach(cdev, uvc_function_strings, ARRAY_SIZE(uvc_en_us_strings));
	if (IS_ERR(us)) {
		ret = PTR_ERR(us);
		goto error;
	}
	uvc_iad.iFunction = us[UVC_STRING_CONTROL_IDX].id;
	uvc_control_intf.iInterface = us[UVC_STRING_CONTROL_IDX].id;
	ret = us[UVC_STRING_STREAMING_IDX].id;
	uvc_streaming_intf_alt0.iInterface = ret;
	uvc_streaming_intf_alt1.iInterface = ret;

	/* Allocate interface IDs. */
	if ((ret = usb_interface_id(c, f)) < 0)
		goto error;
	uvc_iad.bFirstInterface = ret;
	uvc_control_intf.bInterfaceNumber = ret;
	uvc->control_intf = ret;

	if ((ret = usb_interface_id(c, f)) < 0)
		goto error;
	uvc_streaming_intf_alt0.bInterfaceNumber = ret;
	uvc_streaming_intf_alt1.bInterfaceNumber = ret;
	uvc->streaming_intf = ret;

	/* Copy descriptors */
	f->fs_descriptors = uvc_copy_descriptors(uvc, USB_SPEED_FULL);
	if (IS_ERR(f->fs_descriptors)) {
		ret = PTR_ERR(f->fs_descriptors);
		f->fs_descriptors = NULL;
		goto error;
	}
	if (gadget_is_dualspeed(cdev->gadget)) {
		f->hs_descriptors = uvc_copy_descriptors(uvc, USB_SPEED_HIGH);
		if (IS_ERR(f->hs_descriptors)) {
			ret = PTR_ERR(f->hs_descriptors);
			f->hs_descriptors = NULL;
			goto error;
		}
	}

	/* Preallocate control endpoint request. */
	uvc->control_req = usb_ep_alloc_request(cdev->gadget->ep0);
	uvc->control_buf = cache_align_malloc(UVC_MAX_REQUEST_SIZE);
	if (uvc->control_req == NULL || uvc->control_buf == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	uvc->control_req->buf = uvc->control_buf;
	uvc->control_req->complete = uvc_function_ep0_complete;
	uvc->control_req->context = uvc;

	return 0;

error:
	if (uvc->control_req)
		usb_ep_free_request(cdev->gadget->ep0, uvc->control_req);

	free(uvc->control_buf);
	uvc->control_buf = NULL;
	usb_free_all_descriptors(f);
	return ret;
}

static void uvc_function_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct uvc_device *uvc = to_uvc(f);

	usb_ep_free_request(cdev->gadget->ep0, uvc->control_req);
	free(uvc->control_buf);
	uvc->control_buf = NULL;
	usb_free_all_descriptors(f);
}

/* --------------------------------------------------------------------------
 * USB gadget function
 */

static struct uvc_device *my_uvc;

void uvc_free(void)
{
	os_enter_critical();
	assert(my_uvc);
	assert(!my_uvc->exit_flag);
	my_uvc->exit_flag = 1;
	while (my_uvc->ops_busy) {
		thread_waiter_wakeup(&my_uvc->connect_wait);
		thread_waiter_wakeup(&my_uvc->write_wait);
		thread_waiter_wakeup(&my_uvc->stream_wait);
		os_exit_critical();
		thread_waiter_wait(&my_uvc->exit_wait);
		os_enter_critical();
	}

	free(my_uvc);
	my_uvc = NULL;
	os_exit_critical();
}

struct usb_function *uvc_alloc(void)
{
	struct uvc_device *uvc;

	uvc = malloc(sizeof(*uvc));
	if (!uvc)
		return ERR_PTR(-ENOMEM);

	memset(uvc, 0, sizeof(*uvc));
	uvc->video.uvc = uvc;
	uvc->state = UVC_STATE_DISCONNECTED;

	/* Register the function. */
	uvc->func.name = "uvc";
	uvc->func.bind = uvc_function_bind;
	uvc->func.unbind = uvc_function_unbind;
	uvc->func.get_alt = uvc_function_get_alt;
	uvc->func.set_alt = uvc_function_set_alt;
	uvc->func.disable = uvc_function_disable;
	uvc->func.setup = uvc_function_setup;

	spin_lock_init_recursive(&uvc->video.lock);

	INIT_LIST_HEAD(&uvc->video.queue);
	INIT_LIST_HEAD(&uvc->video.req_free);
	thread_waiter_init(&uvc->connect_wait);
	thread_waiter_init(&uvc->write_wait);
	thread_waiter_init(&uvc->stream_wait);
	thread_waiter_init(&uvc->exit_wait);

	os_enter_critical();
	assert(my_uvc == NULL);
	my_uvc = uvc;
	os_exit_critical();

	return &uvc->func;
}

static int check_set_busy(void)
{
	os_enter_critical();

	if (my_uvc == NULL) {
		os_exit_critical();
		printf("%s: device not initialized\n", __func__);
		return -ENODEV;
	}

	my_uvc->ops_busy++;

	os_exit_critical();

	return 0;
}

static void set_no_busy(void)
{
	os_enter_critical();
	if (my_uvc) {
		my_uvc->ops_busy--;
		if (my_uvc->exit_flag)
			thread_waiter_wakeup(&my_uvc->exit_wait);
	}
	os_exit_critical();
}

/*-------------------------------------------------------------------------*/
int gadget_uvc_write(struct uvc_buffer *buf, uint8_t block, uint32_t timeout_ms)
{
	int ret;
	unsigned long flags;

	if (buf == NULL || buf->mem == NULL || buf->length == 0) {
		printf("gadget_uvc_write  invalid argument\n");
		return -EINVAL;
	}

	if (check_set_busy())
		return -ENODEV;

	if (my_uvc->state == UVC_STATE_DISCONNECTED) {
		ret = -ENOLINK;
		goto out;
	}

	if (my_uvc->state == UVC_STATE_CONNECTED) {
		ret = -ENOTCONN;
		goto out;
	}

	while (my_uvc->video.queue_count >= QUEUE_SIZE) {
		usb_spin_lock_irqsave(&my_uvc->video.lock, flags);
		uvcg_video_pump(&my_uvc->video);
		usb_spin_unlock_irqrestore(&my_uvc->video.lock, flags);

		if (!block) {
			ret = -EAGAIN;
			goto out;
		}

		ret = thread_waiter_wait_timeout(&my_uvc->write_wait, timeout_ms);

		if (my_uvc->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (my_uvc->state == UVC_STATE_DISCONNECTED) {
			ret = -ENOLINK;
			goto out;
		}

		if (my_uvc->state == UVC_STATE_CONNECTED) {
			return -ENOTCONN;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	ret = 0;

	buf->state = UVC_BUF_STATE_QUEUED;
	buf->bytesused = 0;

	usb_spin_lock_irqsave(&my_uvc->video.lock, flags);
	list_add_tail(&buf->queue, &my_uvc->video.queue);
	my_uvc->video.queue_count++;
	uvcg_video_pump(&my_uvc->video);
	usb_spin_unlock_irqrestore(&my_uvc->video.lock, flags);

out:
	set_no_busy();
	return ret;
}

int gadget_uvc_get_connect_status(void)
{
	int status;

	os_enter_critical();

	if (my_uvc == NULL) {
		os_exit_critical();
		printf("%s: device not initialized\n", __func__);
		return -ENODEV;
	}

	if (my_uvc->state == UVC_STATE_DISCONNECTED)
		status = 0;
	else
		status = 1;

	os_exit_critical();

	return status;
}

int gadget_uvc_wait_connect(uint32_t timeout_ms)
{
	int ret;

	if (check_set_busy())
		return -ENODEV;

	while (my_uvc->state == UVC_STATE_DISCONNECTED) {
		ret = thread_waiter_wait_timeout(&my_uvc->connect_wait, timeout_ms);

		if (my_uvc->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}
	ret = 0;

out:
	set_no_busy();
	return ret;
}

int gadget_uvc_wait_stream(uint32_t timeout_ms, struct uvc_video_format *format)
{
	int ret;
	unsigned long flags;

	if (check_set_busy())
		return -ENODEV;

	while (my_uvc->state != UVC_STATE_STREAMING) {
		ret = thread_waiter_wait_timeout(&my_uvc->stream_wait, timeout_ms);

		if (my_uvc->exit_flag) {
			ret = -ENODEV;
			goto out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	ret = 0;

	if (format) {
		usb_spin_lock_irqsave(&my_uvc->video.lock, flags);
		format->fcc = my_uvc->video.format.fcc;
		format->width = my_uvc->video.format.width;
		format->height = my_uvc->video.format.height;
		format->bpp = my_uvc->video.format.bpp;
		format->fps = my_uvc->video.format.fps;
		usb_spin_unlock_irqrestore(&my_uvc->video.lock, flags);
	}

out:
	set_no_busy();
	return ret;
}