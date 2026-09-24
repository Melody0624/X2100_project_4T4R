#include "uvcvideo.h"

unsigned int uvc_no_drop_param = 1;
unsigned int uvc_timeout_param = UVC_CTRL_STREAMING_TIMEOUT;

static struct usb_driver uvc_driver;

/* ------------------------------------------------------------------------
 * Utility functions
 */

struct usb_host_endpoint *uvc_find_endpoint(struct usb_host_interface *alts,
		u8 epaddr)
{
	struct usb_host_endpoint *ep;
	unsigned int i;

	for (i = 0; i < alts->desc.bNumEndpoints; ++i) {
		ep = &alts->endpoint[i];
		if (ep->desc.bEndpointAddress == epaddr)
			return ep;
	}

	return NULL;
}

/* ------------------------------------------------------------------------
 * Terminal and unit management
 */

struct uvc_entity *uvc_entity_by_id(struct uvc_device *dev, int id)
{
	struct uvc_entity *entity;

	list_for_each_entry(entity, &dev->entities, list) {
		if (entity->id == id)
			return entity;
	}

	return NULL;
}

static struct uvc_entity *uvc_entity_by_reference(struct uvc_device *dev,
	int id, struct uvc_entity *entity)
{
	unsigned int i;

	if (entity == NULL)
		entity = list_entry(&dev->entities, struct uvc_entity, list);

	list_for_each_entry_continue(entity, &dev->entities, list) {
		for (i = 0; i < entity->bNrInPins; ++i)
			if (entity->baSourceID[i] == id)
				return entity;
	}

	return NULL;
}

static struct uvc_streaming *uvc_stream_by_id(struct uvc_device *dev, int id)
{
	struct uvc_streaming *stream;

	list_for_each_entry(stream, &dev->streams, list) {
		if (stream->header.bTerminalLink == id)
			return stream;
	}

	return NULL;
}

/* ------------------------------------------------------------------------
 * Streaming Object Management
 */

static void uvc_stream_delete(struct uvc_streaming *stream)
{
	if (stream->formats)
		free(stream->formats);
	
	if (stream->header.bmaControls)
		free(stream->header.bmaControls);

	free(stream);
}

static struct uvc_streaming *uvc_stream_new(struct uvc_device *dev,
					    struct usb_interface *intf)
{
	struct uvc_streaming *stream;

	stream = malloc(sizeof(*stream));
	if (stream == NULL)
		return NULL;
	memset(stream, 0, sizeof(*stream));

	mutex_init(&stream->mutex);

	stream->dev = dev;
	stream->intf = intf;
	stream->intfnum = intf->cur_altsetting->desc.bInterfaceNumber;

	return stream;
}

/* ------------------------------------------------------------------------
 * Video formats
 */

static const struct uvc_format_desc uvc_fmts[] = {
	{
		.guid		= UVC_GUID_FORMAT_YUY2,
		.fcc		= V4L2_PIX_FMT_YUYV,
	},
	{
		.guid		= UVC_GUID_FORMAT_YUY2_ISIGHT,
		.fcc		= V4L2_PIX_FMT_YUYV,
	},
	{
		.guid		= UVC_GUID_FORMAT_NV12,
		.fcc		= V4L2_PIX_FMT_NV12,
	},
	{
		.guid		= UVC_GUID_FORMAT_MJPEG,
		.fcc		= V4L2_PIX_FMT_MJPEG,
	},
	{
		.guid		= UVC_GUID_FORMAT_YV12,
		.fcc		= V4L2_PIX_FMT_YVU420,
	},
	{
		.guid		= UVC_GUID_FORMAT_I420,
		.fcc		= V4L2_PIX_FMT_YUV420,
	},
	{
		.guid		= UVC_GUID_FORMAT_M420,
		.fcc		= V4L2_PIX_FMT_M420,
	},
	{
		.guid		= UVC_GUID_FORMAT_UYVY,
		.fcc		= V4L2_PIX_FMT_UYVY,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y800,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y8,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.guid		= UVC_GUID_FORMAT_D3DFMT_L8,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.guid		= UVC_GUID_FORMAT_KSMEDIA_L8_IR,
		.fcc		= V4L2_PIX_FMT_GREY,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y10,
		.fcc		= V4L2_PIX_FMT_Y10,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y12,
		.fcc		= V4L2_PIX_FMT_Y12,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y16,
		.fcc		= V4L2_PIX_FMT_Y16,
	},
	{
		.guid		= UVC_GUID_FORMAT_BY8,
		.fcc		= V4L2_PIX_FMT_SBGGR8,
	},
	{
		.guid		= UVC_GUID_FORMAT_BA81,
		.fcc		= V4L2_PIX_FMT_SBGGR8,
	},
	{
		.guid		= UVC_GUID_FORMAT_GBRG,
		.fcc		= V4L2_PIX_FMT_SGBRG8,
	},
	{
		.guid		= UVC_GUID_FORMAT_GRBG,
		.fcc		= V4L2_PIX_FMT_SGRBG8,
	},
	{
		.guid		= UVC_GUID_FORMAT_RGGB,
		.fcc		= V4L2_PIX_FMT_SRGGB8,
	},
	{
		.guid		= UVC_GUID_FORMAT_RGBP,
		.fcc		= V4L2_PIX_FMT_RGB565,
	},
	{
		.guid		= UVC_GUID_FORMAT_BGR3,
		.fcc		= V4L2_PIX_FMT_BGR24,
	},
	{
		.guid		= UVC_GUID_FORMAT_BGR4,
		.fcc		= V4L2_PIX_FMT_XBGR32,
	},
	{
		.guid		= UVC_GUID_FORMAT_H264,
		.fcc		= V4L2_PIX_FMT_H264,
	},
	{
		.guid		= UVC_GUID_FORMAT_H265,
		.fcc		= V4L2_PIX_FMT_HEVC,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y8I,
		.fcc		= V4L2_PIX_FMT_Y8I,
	},
	{
		.guid		= UVC_GUID_FORMAT_Y12I,
		.fcc		= V4L2_PIX_FMT_Y12I,
	},
	{
		.guid		= UVC_GUID_FORMAT_Z16,
		.fcc		= V4L2_PIX_FMT_Z16,
	},
	{
		.guid		= UVC_GUID_FORMAT_RW10,
		.fcc		= V4L2_PIX_FMT_SRGGB10P,
	},
	{
		.guid		= UVC_GUID_FORMAT_BG16,
		.fcc		= V4L2_PIX_FMT_SBGGR16,
	},
	{
		.guid		= UVC_GUID_FORMAT_GB16,
		.fcc		= V4L2_PIX_FMT_SGBRG16,
	},
	{
		.guid		= UVC_GUID_FORMAT_RG16,
		.fcc		= V4L2_PIX_FMT_SRGGB16,
	},
	{
		.guid		= UVC_GUID_FORMAT_GR16,
		.fcc		= V4L2_PIX_FMT_SGRBG16,
	},
	{
		.guid		= UVC_GUID_FORMAT_INVZ,
		.fcc		= V4L2_PIX_FMT_Z16,
	},
	{
		.guid		= UVC_GUID_FORMAT_INVI,
		.fcc		= V4L2_PIX_FMT_Y10,
	},
	{
		.guid		= UVC_GUID_FORMAT_INZI,
		.fcc		= V4L2_PIX_FMT_INZI,
	},
	{
		.guid		= UVC_GUID_FORMAT_CNF4,
		.fcc		= V4L2_PIX_FMT_CNF4,
	},
	{
		.guid		= UVC_GUID_FORMAT_HEVC,
		.fcc		= V4L2_PIX_FMT_HEVC,
	},
};

const struct uvc_format_desc *uvc_format_by_guid(const u8 guid[16])
{
	unsigned int len = ARRAY_SIZE(uvc_fmts);
	unsigned int i;

	for (i = 0; i < len; ++i) {
		if (memcmp(guid, uvc_fmts[i].guid, 16) == 0)
			return &uvc_fmts[i];
	}

	return NULL;
}

/* ------------------------------------------------------------------------
 * Descriptors parsing
 */

static int uvc_parse_format(struct uvc_device *dev,
	struct uvc_streaming *streaming, struct uvc_host_format *format,
	struct uvc_host_frame *frames, u32 **intervals, const unsigned char *buffer,
	int buflen)
{
	struct usb_interface *intf = streaming->intf;
	struct usb_host_interface *alts = intf->cur_altsetting;
	const struct uvc_format_desc *fmtdesc;
	struct uvc_host_frame *frame;
	const unsigned char *start = buffer;
	unsigned int width_multiplier = 1;
	unsigned int interval;
	unsigned int i, n;
	u8 ftype;
	char *fcc_name;

	format->type = buffer[2];
	format->index = buffer[3];
	format->frames = frames;

	switch (buffer[2]) {
	case UVC_VS_FORMAT_UNCOMPRESSED:
	case UVC_VS_FORMAT_FRAME_BASED:
		n = buffer[2] == UVC_VS_FORMAT_UNCOMPRESSED ? 27 : 28;
		if (buflen < n) {
			printf("%s: device %d videostreaming interface %d FORMAT error\n",
				__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/* Find the format descriptor from its GUID. */
		fmtdesc = uvc_format_by_guid(&buffer[5]);
		if (!fmtdesc) {
			/*
			 * Unknown video formats are not fatal errors, the
			 * caller will skip this descriptor.
			 */
			printf("%s: Unknown video format %pUl\n", __func__, &buffer[5]);
			return 0;
		}

		format->fcc = fmtdesc->fcc;
		format->bpp = buffer[21];

		if (buffer[2] == UVC_VS_FORMAT_UNCOMPRESSED) {
			ftype = UVC_VS_FRAME_UNCOMPRESSED;
		} else {
			ftype = UVC_VS_FRAME_FRAME_BASED;
			if (buffer[27])
				format->flags = UVC_FMT_FLAG_COMPRESSED;
		}
		break;

	case UVC_VS_FORMAT_MJPEG:
		if (buflen < 11) {
			printf("%s: device %d videostreaming interface %d FORMAT error\n",
				__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		format->fcc = V4L2_PIX_FMT_MJPEG;
		format->flags = UVC_FMT_FLAG_COMPRESSED;
		format->bpp = 0;
		ftype = UVC_VS_FRAME_MJPEG;
		break;

	case UVC_VS_FORMAT_DV:
	case UVC_VS_FORMAT_MPEG2TS:
	case UVC_VS_FORMAT_STREAM_BASED:
		/* Not supported yet. */
	default:
		printf("%s: device %d videostreaming interface %d unsupported format %u\n",
			__func__, dev->udev->devnum, alts->desc.bInterfaceNumber, buffer[2]);
		return -EINVAL;
	}

	fcc_name = (char *)&format->fcc;
	printf("%s: Found format %c%c%c%c\n", __func__, fcc_name[0], fcc_name[1], fcc_name[2], fcc_name[3]);

	buflen -= buffer[0];
	buffer += buffer[0];

	/*
	 * Parse the frame descriptors. Only uncompressed, MJPEG and frame
	 * based formats have frame descriptors.
	 */
	while (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE &&
	       buffer[2] == ftype) {
		unsigned int maxIntervalIndex;

		frame = &frames[format->nframes];
		if (ftype != UVC_VS_FRAME_FRAME_BASED)
			n = buflen > 25 ? buffer[25] : 0;
		else
			n = buflen > 21 ? buffer[21] : 0;

		n = n ? n : 3;

		if (buflen < 26 + 4*n) {
			printf("%s: device %d videostreaming interface %d FRAME error\n",
				__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		frame->bFrameIndex = buffer[3];
		frame->bmCapabilities = buffer[4];
		frame->wWidth = get_unaligned_le16(&buffer[5])
			      * width_multiplier;
		frame->wHeight = get_unaligned_le16(&buffer[7]);
		frame->dwMinBitRate = get_unaligned_le32(&buffer[9]);
		frame->dwMaxBitRate = get_unaligned_le32(&buffer[13]);
		if (ftype != UVC_VS_FRAME_FRAME_BASED) {
			frame->dwMaxVideoFrameBufferSize =
				get_unaligned_le32(&buffer[17]);
			frame->dwDefaultFrameInterval =
				get_unaligned_le32(&buffer[21]);
			frame->bFrameIntervalType = buffer[25];
		} else {
			frame->dwMaxVideoFrameBufferSize = 0;
			frame->dwDefaultFrameInterval =
				get_unaligned_le32(&buffer[17]);
			frame->bFrameIntervalType = buffer[21];
		}

		/*
		 * Copy the frame intervals.
		 *
		 * Some bogus devices report dwMinFrameInterval equal to
		 * dwMaxFrameInterval and have dwFrameIntervalStep set to
		 * zero. Setting all null intervals to 1 fixes the problem and
		 * some other divisions by zero that could happen.
		 */
		frame->dwFrameInterval = *intervals;

		for (i = 0; i < n; ++i) {
			interval = get_unaligned_le32(&buffer[26+4*i]);
			(*intervals)[i] = interval ? interval : 1;
		}

		/*
		 * Apply more fixes, quirks and workarounds to handle incorrect
		 * or broken descriptors.
		 */

		/*
		 * Several UVC chipsets screw up dwMaxVideoFrameBufferSize
		 * completely. Observed behaviours range from setting the
		 * value to 1.1x the actual frame size to hardwiring the
		 * 16 low bits to 0. This results in a higher than necessary
		 * memory usage as well as a wrong image size information. For
		 * uncompressed formats this can be fixed by computing the
		 * value from the frame size.
		 */
		if (!(format->flags & UVC_FMT_FLAG_COMPRESSED))
			frame->dwMaxVideoFrameBufferSize = format->bpp
				* frame->wWidth * frame->wHeight / 8;

		/*
		 * Clamp the default frame interval to the boundaries. A zero
		 * bFrameIntervalType value indicates a continuous frame
		 * interval range, with dwFrameInterval[0] storing the minimum
		 * value and dwFrameInterval[1] storing the maximum value.
		 */
		maxIntervalIndex = frame->bFrameIntervalType ? n - 1 : 1;
		frame->dwDefaultFrameInterval =
			clamp(frame->dwDefaultFrameInterval,
			      frame->dwFrameInterval[0],
			      frame->dwFrameInterval[maxIntervalIndex]);

		printf("- %ux%u (%u.%u fps)\n",
			frame->wWidth, frame->wHeight,
			10000000 / frame->dwDefaultFrameInterval,
			(100000000 / frame->dwDefaultFrameInterval) % 10);

		format->nframes++;
		*intervals += n;

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE &&
	    buffer[2] == UVC_VS_STILL_IMAGE_FRAME) {
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE &&
	    buffer[2] == UVC_VS_COLORFORMAT) {
		if (buflen < 6) {
			printf("%s: device %d videostreaming interface %d COLORFORMAT error\n",
				__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	return buffer - start;
}

static int uvc_parse_streaming(struct uvc_device *dev,
	struct usb_interface *intf)
{
	struct uvc_streaming *streaming = NULL;
	struct uvc_host_format *format;
	struct uvc_host_frame *frame;
	struct usb_host_interface *alts = &intf->altsetting[0];
	const unsigned char *_buffer, *buffer = alts->extra;
	int _buflen, buflen = alts->extralen;
	unsigned int nformats = 0, nframes = 0, nintervals = 0;
	unsigned int size, i, n, p;
	u32 *interval;
	u16 psize;
	int ret = -EINVAL;

	if (intf->cur_altsetting->desc.bInterfaceSubClass != UVC_SC_VIDEOSTREAMING)
		return -EINVAL;

	if (usb_driver_claim_interface(&uvc_driver, intf, dev))
		return -EINVAL;

	streaming = uvc_stream_new(dev, intf);
	if (streaming == NULL) {
		usb_driver_release_interface(&uvc_driver, intf);
		return -ENOMEM;
	}

	/*
	 * The Pico iMage webcam has its class-specific interface descriptors
	 * after the endpoint descriptors.
	 */
	if (buflen == 0) {
		for (i = 0; i < alts->desc.bNumEndpoints; ++i) {
			struct usb_host_endpoint *ep = &alts->endpoint[i];

			if (ep->extralen == 0)
				continue;

			if (ep->extralen > 2 && ep->extra[1] == USB_DT_CS_INTERFACE) {
				buffer = alts->endpoint[i].extra;
				buflen = alts->endpoint[i].extralen;
				break;
			}
		}
	}

	/* Skip the standard interface descriptors. */
	while (buflen > 2 && buffer[1] != USB_DT_CS_INTERFACE) {
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (buflen <= 2) {
		printf("%s: no class-specific streaming interface descriptors found\n", __func__);
		goto error;
	}

	/* Parse the header descriptor. */
	switch (buffer[2]) {
	case UVC_VS_OUTPUT_HEADER:
		streaming->type = UVC_STREAM_TYPE_VIDEO_OUTPUT;
		size = 9;
		break;

	case UVC_VS_INPUT_HEADER:
		streaming->type = UVC_STREAM_TYPE_VIDEO_CAPTURE;
		size = 13;
		break;

	default:
		printf("%s: device %d videostreaming interface %d HEADER descriptor not found\n",
			__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	p = buflen >= 4 ? buffer[3] : 0;
	n = buflen >= size ? buffer[size-1] : 0;

	if (buflen < size + p*n) {
		printf("%s: device %d videostreaming interface %d HEADER descriptor is invalid\n",
			__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	streaming->header.bNumFormats = p;
	streaming->header.bEndpointAddress = buffer[6];
	if (buffer[2] == UVC_VS_INPUT_HEADER) {
		streaming->header.bmInfo = buffer[7];
		streaming->header.bTerminalLink = buffer[8];
		streaming->header.bStillCaptureMethod = buffer[9];
		streaming->header.bTriggerSupport = buffer[10];
		streaming->header.bTriggerUsage = buffer[11];
	} else {
		streaming->header.bTerminalLink = buffer[7];
	}
	streaming->header.bControlSize = n;

	streaming->header.bmaControls = malloc(p * n);
	if (streaming->header.bmaControls == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	memcpy(streaming->header.bmaControls, &buffer[size], p * n);

	buflen -= buffer[0];
	buffer += buffer[0];

	_buffer = buffer;
	_buflen = buflen;

	/* Count the format and frame descriptors. */
	while (_buflen > 2 && _buffer[1] == USB_DT_CS_INTERFACE) {
		switch (_buffer[2]) {
		case UVC_VS_FORMAT_UNCOMPRESSED:
		case UVC_VS_FORMAT_MJPEG:
		case UVC_VS_FORMAT_FRAME_BASED:
			nformats++;
			break;

		case UVC_VS_FORMAT_DV:
		case UVC_VS_FORMAT_MPEG2TS:
		case UVC_VS_FORMAT_STREAM_BASED:
			printf("%s: device %d videostreaming interface %d FORMAT %u is not supported\n",
				__func__, dev->udev->devnum, alts->desc.bInterfaceNumber, _buffer[2]);
			break;

		case UVC_VS_FRAME_UNCOMPRESSED:
		case UVC_VS_FRAME_MJPEG:
			nframes++;
			if (_buflen > 25)
				nintervals += _buffer[25] ? _buffer[25] : 3;
			break;

		case UVC_VS_FRAME_FRAME_BASED:
			nframes++;
			if (_buflen > 21)
				nintervals += _buffer[21] ? _buffer[21] : 3;
			break;
		}

		_buflen -= _buffer[0];
		_buffer += _buffer[0];
	}

	if (nformats == 0) {
		printf("%s: device %d videostreaming interface %d has no supported formats defined\n",
			__func__, dev->udev->devnum, alts->desc.bInterfaceNumber);
		goto error;
	}

	size = nformats * sizeof(*format) + nframes * sizeof(*frame)
	     + nintervals * sizeof(*interval);
	format = malloc(size);
	if (format == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	memset(format, 0, size);

	frame = (struct uvc_host_frame *)&format[nformats];
	interval = (u32 *)&frame[nframes];

	streaming->formats = format;
	streaming->nformats = 0;

	/* Parse the format descriptors. */
	while (buflen > 2 && buffer[1] == USB_DT_CS_INTERFACE) {
		switch (buffer[2]) {
		case UVC_VS_FORMAT_UNCOMPRESSED:
		case UVC_VS_FORMAT_MJPEG:
		case UVC_VS_FORMAT_FRAME_BASED:
			ret = uvc_parse_format(dev, streaming, format, frame,
				&interval, buffer, buflen);
			if (ret < 0)
				goto error;
			if (!ret)
				break;

			streaming->nformats++;
			frame += format->nframes;
			format++;

			buflen -= ret;
			buffer += ret;
			continue;

		default:
			break;
		}

		buflen -= buffer[0];
		buffer += buffer[0];
	}

	/* Parse the alternate settings to find the maximum bandwidth. */
	for (i = 0; i < intf->num_altsetting; ++i) {
		struct usb_host_endpoint *ep;

		alts = &intf->altsetting[i];
		ep = uvc_find_endpoint(alts,
				streaming->header.bEndpointAddress);
		if (ep == NULL)
			continue;
		psize = uvc_endpoint_max_bpi(dev->udev, ep);
		if (psize > streaming->maxpsize)
			streaming->maxpsize = psize;
	}

	list_add_tail(&streaming->list, &dev->streams);
	return 0;

error:
	usb_driver_release_interface(&uvc_driver, intf);
	uvc_stream_delete(streaming);
	return ret;
}

static struct uvc_entity *uvc_alloc_entity(u16 type, u16 id,
		unsigned int num_inputs, unsigned int extra_size)
{
	struct uvc_entity *entity;
	unsigned int size;

	size = sizeof(*entity) + extra_size + num_inputs;
	entity = malloc(size);
	if (entity == NULL)
		return NULL;
	memset(entity, 0, size);

	entity->id = id;
	entity->type = type;
	entity->bNrInPins = num_inputs;
	entity->baSourceID = ((void *)(entity + 1)) + extra_size;

	return entity;
}

static void uvc_entity_set_name(struct uvc_device *dev, struct uvc_entity *entity,
				const char *type_name, u8 string_id)
{
	int ret;

	/*
	 * First attempt to read the entity name from the device. If the entity
	 * has no associated string, or if reading the string fails (most
	 * likely due to a buggy firmware), fall back to default names based on
	 * the entity type.
	 */
	if (string_id) {
		ret = usb_string(dev->udev, string_id, entity->name,
				 sizeof(entity->name));
		if (!ret)
			return;
	}

	sprintf(entity->name, "%s %u", type_name, entity->id);
}

static int uvc_parse_standard_control(struct uvc_device *dev,
	const unsigned char *buffer, int buflen)
{
	struct usb_device *udev = dev->udev;
	struct uvc_entity *unit, *term;
	struct usb_interface *intf;
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	unsigned int i, n, p, len;
	const char *type_name;
	u16 type;

	switch (buffer[2]) {
	case UVC_VC_HEADER:
		n = buflen >= 12 ? buffer[11] : 0;

		if (buflen < 12 + n) {
			printf("%s: device %d videocontrol interface %d HEADER error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		dev->uvc_version = get_unaligned_le16(&buffer[3]);
		dev->clock_frequency = get_unaligned_le32(&buffer[7]);

		/* Parse all USB Video Streaming interfaces. */
		for (i = 0; i < n; ++i) {
			intf = usb_ifnum_to_if(udev, buffer[12+i]);
			if (intf == NULL)
				continue;

			uvc_parse_streaming(dev, intf);
		}
		break;

	case UVC_VC_INPUT_TERMINAL:
		if (buflen < 8) {
			printf("%s: device %d videocontrol interface %d INPUT_TERMINAL error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/*
		 * Reject invalid terminal types that would cause issues:
		 *
		 * - The high byte must be non-zero, otherwise it would be
		 *   confused with a unit.
		 *
		 * - Bit 15 must be 0, as we use it internally as a terminal
		 *   direction flag.
		 *
		 * Other unknown types are accepted.
		 */
		type = get_unaligned_le16(&buffer[4]);
		if ((type & 0x7f00) == 0 || (type & 0x8000) != 0) {
			printf("%s: device %d videocontrol interface %d INPUT_TERMINAL %d has invalid type 0x%04x, skipping\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber, buffer[3], type);
			return 0;
		}

		n = 0;
		p = 0;
		len = 8;

		if (type == UVC_ITT_CAMERA) {
			n = buflen >= 15 ? buffer[14] : 0;
			len = 15;

		} else if (type == UVC_ITT_MEDIA_TRANSPORT_INPUT) {
			n = buflen >= 9 ? buffer[8] : 0;
			p = buflen >= 10 + n ? buffer[9+n] : 0;
			len = 10;
		}

		if (buflen < len + n + p) {
			printf("%s: device %d videocontrol interface %d INPUT_TERMINAL error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		term = uvc_alloc_entity(type | UVC_TERM_INPUT, buffer[3],
					1, n + p);
		if (term == NULL)
			return -ENOMEM;

		if (UVC_ENTITY_TYPE(term) == UVC_ITT_CAMERA) {
			term->camera.bControlSize = n;
			term->camera.bmControls = (u8 *)term + sizeof(*term);
			term->camera.wObjectiveFocalLengthMin =
				get_unaligned_le16(&buffer[8]);
			term->camera.wObjectiveFocalLengthMax =
				get_unaligned_le16(&buffer[10]);
			term->camera.wOcularFocalLength =
				get_unaligned_le16(&buffer[12]);
			memcpy(term->camera.bmControls, &buffer[15], n);
		} else if (UVC_ENTITY_TYPE(term) ==
			   UVC_ITT_MEDIA_TRANSPORT_INPUT) {
			term->media.bControlSize = n;
			term->media.bmControls = (u8 *)term + sizeof(*term);
			term->media.bTransportModeSize = p;
			term->media.bmTransportModes = (u8 *)term
						     + sizeof(*term) + n;
			memcpy(term->media.bmControls, &buffer[9], n);
			memcpy(term->media.bmTransportModes, &buffer[10+n], p);
		}

		if (UVC_ENTITY_TYPE(term) == UVC_ITT_CAMERA)
			type_name = "Camera";
		else if (UVC_ENTITY_TYPE(term) == UVC_ITT_MEDIA_TRANSPORT_INPUT)
			type_name = "Media";
		else
			type_name = "Input";

		uvc_entity_set_name(dev, term, type_name, buffer[7]);

		list_add_tail(&term->list, &dev->entities);
		break;

	case UVC_VC_OUTPUT_TERMINAL:
		if (buflen < 9) {
			printf("%s: device %d videocontrol interface %d OUTPUT_TERMINAL error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		/*
		 * Make sure the terminal type MSB is not null, otherwise it
		 * could be confused with a unit.
		 */
		type = get_unaligned_le16(&buffer[4]);
		if ((type & 0xff00) == 0) {
			printf("%s: device %d videocontrol interface %d OUTPUT_TERMINAL %d has invalid type 0x%04x, skipping\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber, buffer[3], type);
			return 0;
		}

		term = uvc_alloc_entity(type | UVC_TERM_OUTPUT, buffer[3], 1, 0);
		if (term == NULL)
			return -ENOMEM;

		memcpy(term->baSourceID, &buffer[7], 1);

		uvc_entity_set_name(dev, term, "Output", buffer[8]);

		list_add_tail(&term->list, &dev->entities);
		break;

	case UVC_VC_SELECTOR_UNIT:
		p = buflen >= 5 ? buffer[4] : 0;

		if (buflen < 5 || buflen < 6 + p) {
			printf("%s: device %d videocontrol interface %d SELECTOR_UNIT error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = uvc_alloc_entity(buffer[2], buffer[3], p, 0);
		if (unit == NULL)
			return -ENOMEM;

		memcpy(unit->baSourceID, &buffer[5], p);

		uvc_entity_set_name(dev, unit, "Selector", buffer[5+p]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	case UVC_VC_PROCESSING_UNIT:
		n = buflen >= 8 ? buffer[7] : 0;
		p = dev->uvc_version >= 0x0110 ? 10 : 9;

		if (buflen < p + n) {
			printf("%s: device %d videocontrol interface %d PROCESSING_UNIT error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = uvc_alloc_entity(buffer[2], buffer[3], 1, n);
		if (unit == NULL)
			return -ENOMEM;

		memcpy(unit->baSourceID, &buffer[4], 1);
		unit->processing.wMaxMultiplier = get_unaligned_le16(&buffer[5]);
		unit->processing.bControlSize = buffer[7];
		unit->processing.bmControls = (u8 *)unit + sizeof(*unit);
		memcpy(unit->processing.bmControls, &buffer[8], n);
		if (dev->uvc_version >= 0x0110)
			unit->processing.bmVideoStandards = buffer[9+n];

		uvc_entity_set_name(dev, unit, "Processing", buffer[8+n]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	case UVC_VC_EXTENSION_UNIT:
		p = buflen >= 22 ? buffer[21] : 0;
		n = buflen >= 24 + p ? buffer[22+p] : 0;

		if (buflen < 24 + p + n) {
			printf("%s: device %d videocontrol interface %d EXTENSION_UNIT error\n",
				__func__, udev->devnum, alts->desc.bInterfaceNumber);
			return -EINVAL;
		}

		unit = uvc_alloc_entity(buffer[2], buffer[3], p, n);
		if (unit == NULL)
			return -ENOMEM;

		unit->extension.bNumControls = buffer[20];
		memcpy(unit->baSourceID, &buffer[22], p);
		unit->extension.bControlSize = buffer[22+p];
		unit->extension.bmControls = (u8 *)unit + sizeof(*unit);
		memcpy(unit->extension.bmControls, &buffer[23+p], n);

		uvc_entity_set_name(dev, unit, "Extension", buffer[23+p+n]);

		list_add_tail(&unit->list, &dev->entities);
		break;

	default:
		printf("%s: Found an unknown CS_INTERFACE descriptor (%u)\n",
			__func__, buffer[2]);
		break;
	}

	return 0;
}

static int uvc_parse_control(struct uvc_device *dev)
{
	struct usb_host_interface *alts = dev->intf->cur_altsetting;
	const unsigned char *buffer = alts->extra;
	int buflen = alts->extralen;
	int ret;

	/*
	 * Parse the default alternate setting only, as the UVC specification
	 * defines a single alternate setting, the default alternate setting
	 * zero.
	 */

	while (buflen > 2) {
		if (buffer[1] != USB_DT_CS_INTERFACE)
			goto next_descriptor;

		ret = uvc_parse_standard_control(dev, buffer, buflen);
		if (ret < 0)
			return ret;

next_descriptor:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (alts->desc.bNumEndpoints == 1) {
		struct usb_host_endpoint *ep = &alts->endpoint[0];
		struct usb_endpoint_descriptor *desc = &ep->desc;

		if (usb_endpoint_is_int_in(desc) &&
		    desc->wMaxPacketSize >= 8 &&
		    desc->bInterval != 0) {
			dev->int_ep = ep;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * UVC device scan
 */

/*
 * Scan the UVC descriptors to locate a chain starting at an Output Terminal
 * and containing the following units:
 *
 * - one or more Output Terminals (USB Streaming or Display)
 * - zero or one Processing Unit
 * - zero, one or more single-input Selector Units
 * - zero or one multiple-input Selector Units, provided all inputs are
 *   connected to input terminals
 * - zero, one or mode single-input Extension Units
 * - one or more Input Terminals (Camera, External or USB Streaming)
 *
 * The terminal and units must match on of the following structures:
 *
 * ITT_*(0) -> +---------+    +---------+    +---------+ -> TT_STREAMING(0)
 * ...         | SU{0,1} | -> | PU{0,1} | -> | XU{0,n} |    ...
 * ITT_*(n) -> +---------+    +---------+    +---------+ -> TT_STREAMING(n)
 *
 *                 +---------+    +---------+ -> OTT_*(0)
 * TT_STREAMING -> | PU{0,1} | -> | XU{0,n} |    ...
 *                 +---------+    +---------+ -> OTT_*(n)
 *
 * The Processing Unit and Extension Units can be in any order. Additional
 * Extension Units connected to the main chain as single-unit branches are
 * also supported. Single-input Selector Units are ignored.
 */
static int uvc_scan_chain_entity(struct uvc_video_chain *chain,
	struct uvc_entity *entity)
{
	switch (UVC_ENTITY_TYPE(entity)) {
	case UVC_VC_EXTENSION_UNIT:
		if (entity->bNrInPins != 1) {
			printf("%s: Extension unit %d has more than 1 input pin\n",
				__func__, entity->id);
			return -1;
		}

		break;

	case UVC_VC_PROCESSING_UNIT:
		if (chain->processing != NULL) {
			printf("%s: Found multiple Processing Units in chain\n", __func__);
			return -1;
		}

		chain->processing = entity;
		break;

	case UVC_VC_SELECTOR_UNIT:
		/* Single-input selector units are ignored. */
		if (entity->bNrInPins == 1)
			break;

		if (chain->selector != NULL) {
			printf("%s: Found multiple Selector Units in chain\n", __func__);
			return -1;
		}

		chain->selector = entity;
		break;

	case UVC_ITT_VENDOR_SPECIFIC:
	case UVC_ITT_CAMERA:
	case UVC_ITT_MEDIA_TRANSPORT_INPUT:
		break;

	case UVC_OTT_VENDOR_SPECIFIC:
	case UVC_OTT_DISPLAY:
	case UVC_OTT_MEDIA_TRANSPORT_OUTPUT:
		break;

	case UVC_TT_STREAMING:
		break;

	default:
		printf("%s: Unsupported entity type 0x%04x found in chain\n",
			__func__, UVC_ENTITY_TYPE(entity));
		return -1;
	}

	list_add_tail(&entity->chain, &chain->entities);
	return 0;
}

static int uvc_scan_chain_forward(struct uvc_video_chain *chain,
	struct uvc_entity *entity, struct uvc_entity *prev)
{
	struct uvc_entity *forward;

	/* Forward scan */
	forward = NULL;

	while (1) {
		forward = uvc_entity_by_reference(chain->dev, entity->id,
			forward);
		if (forward == NULL)
			break;
		if (forward == prev)
			continue;
		if (forward->chain.next || forward->chain.prev) {
			printf("%s: Found reference to entity %d already in chain\n",
				__func__, forward->id);
			return -EINVAL;
		}

		switch (UVC_ENTITY_TYPE(forward)) {
		case UVC_VC_EXTENSION_UNIT:
			if (forward->bNrInPins != 1) {
				printf("%s: Extension unit %d has more than 1 input pin\n",
					__func__, forward->id);
				return -EINVAL;
			}

			/*
			 * Some devices reference an output terminal as the
			 * source of extension units. This is incorrect, as
			 * output terminals only have an input pin, and thus
			 * can't be connected to any entity in the forward
			 * direction. The resulting topology would cause issues
			 * when registering the media controller graph. To
			 * avoid this problem, connect the extension unit to
			 * the source of the output terminal instead.
			 */
			if (UVC_ENTITY_IS_OTERM(entity)) {
				struct uvc_entity *source;

				source = uvc_entity_by_id(chain->dev,
							  entity->baSourceID[0]);
				if (!source) {
					printf("%s: Can't connect extension unit %u in chain\n",
						__func__, forward->id);
					break;
				}

				forward->baSourceID[0] = source->id;
			}

			list_add_tail(&forward->chain, &chain->entities);
			break;

		case UVC_OTT_VENDOR_SPECIFIC:
		case UVC_OTT_DISPLAY:
		case UVC_OTT_MEDIA_TRANSPORT_OUTPUT:
		case UVC_TT_STREAMING:
			if (UVC_ENTITY_IS_ITERM(forward)) {
				printf("%s: Unsupported input terminal %u\n",
					__func__, forward->id);
				return -EINVAL;
			}

			if (UVC_ENTITY_IS_OTERM(entity)) {
				printf("%s: Unsupported connection between output terminals %u and %u\n",
					__func__, entity->id, forward->id);
				break;
			}

			list_add_tail(&forward->chain, &chain->entities);
			break;
		}
	}

	return 0;
}

static int uvc_scan_chain_backward(struct uvc_video_chain *chain,
	struct uvc_entity **_entity)
{
	struct uvc_entity *entity = *_entity;
	struct uvc_entity *term;
	int id = -EINVAL, i;

	switch (UVC_ENTITY_TYPE(entity)) {
	case UVC_VC_EXTENSION_UNIT:
	case UVC_VC_PROCESSING_UNIT:
		id = entity->baSourceID[0];
		break;

	case UVC_VC_SELECTOR_UNIT:
		/* Single-input selector units are ignored. */
		if (entity->bNrInPins == 1) {
			id = entity->baSourceID[0];
			break;
		}

		chain->selector = entity;
		for (i = 0; i < entity->bNrInPins; ++i) {
			id = entity->baSourceID[i];
			term = uvc_entity_by_id(chain->dev, id);
			if (term == NULL || !UVC_ENTITY_IS_ITERM(term)) {
				printf("%s: Selector unit %d input %d isn't connected to an input terminal\n",
					__func__, entity->id, i);
				return -1;
			}

			if (term->chain.next || term->chain.prev) {
				printf("%s: Found reference to entity %d already in chain\n",
					__func__, term->id);
				return -EINVAL;
			}

			list_add_tail(&term->chain, &chain->entities);
			uvc_scan_chain_forward(chain, term, entity);
		}

		id = 0;
		break;

	case UVC_ITT_VENDOR_SPECIFIC:
	case UVC_ITT_CAMERA:
	case UVC_ITT_MEDIA_TRANSPORT_INPUT:
	case UVC_OTT_VENDOR_SPECIFIC:
	case UVC_OTT_DISPLAY:
	case UVC_OTT_MEDIA_TRANSPORT_OUTPUT:
	case UVC_TT_STREAMING:
		id = UVC_ENTITY_IS_OTERM(entity) ? entity->baSourceID[0] : 0;
		break;
	}

	if (id <= 0) {
		*_entity = NULL;
		return id;
	}

	entity = uvc_entity_by_id(chain->dev, id);
	if (entity == NULL) {
		printf("%s: Found reference to unknown entity %d\n", __func__, id);
		return -EINVAL;
	}

	*_entity = entity;
	return 0;
}

static int uvc_scan_chain(struct uvc_video_chain *chain,
			  struct uvc_entity *term)
{
	struct uvc_entity *entity, *prev;

	entity = term;
	prev = NULL;

	while (entity != NULL) {
		/* Entity must not be part of an existing chain */
		if (entity->chain.next || entity->chain.prev) {
			printf("%s: Found reference to entity %d already in chain\n",
				__func__, entity->id);
			return -EINVAL;
		}

		/* Process entity */
		if (uvc_scan_chain_entity(chain, entity) < 0)
			return -EINVAL;

		/* Forward scan */
		if (uvc_scan_chain_forward(chain, entity, prev) < 0)
			return -EINVAL;

		/* Backward scan */
		prev = entity;
		if (uvc_scan_chain_backward(chain, &entity) < 0)
			return -EINVAL;
	}

	return 0;
}

static unsigned int uvc_print_terms(struct list_head *terms, u16 dir,
		char *buffer)
{
	struct uvc_entity *term;
	unsigned int nterms = 0;
	char *p = buffer;

	list_for_each_entry(term, terms, chain) {
		if (!UVC_ENTITY_IS_TERM(term) ||
		    UVC_TERM_DIRECTION(term) != dir)
			continue;

		if (nterms)
			p += sprintf(p, ",");
		if (++nterms >= 4) {
			p += sprintf(p, "...");
			break;
		}
		p += sprintf(p, "%u", term->id);
	}

	return p - buffer;
}

static const char *uvc_print_chain(struct uvc_video_chain *chain)
{
	static char buffer[43];
	char *p = buffer;

	p += uvc_print_terms(&chain->entities, UVC_TERM_INPUT, p);
	p += sprintf(p, " -> ");
	uvc_print_terms(&chain->entities, UVC_TERM_OUTPUT, p);

	return buffer;
}

static struct uvc_video_chain *uvc_alloc_chain(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;

	chain = malloc(sizeof(*chain));
	if (chain == NULL)
		return NULL;
	memset(chain, 0, sizeof(*chain));

	INIT_LIST_HEAD(&chain->entities);
	chain->dev = dev;

	return chain;
}

/*
 * Scan the device for video chains and register video devices.
 *
 * Chains are scanned starting at their output terminals and walked backwards.
 */
static int uvc_scan_device(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;
	struct uvc_entity *term;

	list_for_each_entry(term, &dev->entities, list) {
		if (!UVC_ENTITY_IS_OTERM(term))
			continue;

		/*
		 * If the terminal is already included in a chain, skip it.
		 * This can happen for chains that have multiple output
		 * terminals, where all output terminals beside the first one
		 * will be inserted in the chain in forward scans.
		 */
		if (term->chain.next || term->chain.prev)
			continue;

		chain = uvc_alloc_chain(dev);
		if (chain == NULL)
			return -ENOMEM;

		if (uvc_scan_chain(chain, term) < 0) {
			free(chain);
			continue;
		}

		printf("Found a valid video chain (%s)\n", uvc_print_chain(chain));
		list_add_tail(&chain->list, &dev->chains);
	}

	if (list_empty(&dev->chains)) {
		printf("No valid video chain found.\n");
		return -1;
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * Video device registration and unregistration
 */

void uvc_dev_free(struct uvc_device *dev)
{
	struct list_head *p, *n;

	uvc_status_cleanup(dev);

	list_for_each_safe(p, n, &dev->chains) {
		struct uvc_video_chain *chain;

		chain = list_entry(p, struct uvc_video_chain, list);
		free(chain);
	}

	list_for_each_safe(p, n, &dev->entities) {
		struct uvc_entity *entity;

		entity = list_entry(p, struct uvc_entity, list);
		free(entity);
	}

	list_for_each_safe(p, n, &dev->streams) {
		struct uvc_streaming *streaming;

		streaming = list_entry(p, struct uvc_streaming, list);
		uvc_stream_delete(streaming);
	}

	free(dev);
}

void uvc_delete(struct uvc_device *dev)
{
	struct list_head *p, *n;

	list_for_each_safe(p, n, &dev->streams) {
		struct uvc_streaming *streaming;
		streaming = list_entry(p, struct uvc_streaming, list);
		usb_driver_release_interface(&uvc_driver, streaming->intf);
		streaming->dev = NULL;
		streaming->intf = NULL;
		mutex_lock(&streaming->mutex);
		uvc_video_stop_streaming(streaming);
		mutex_unlock(&streaming->mutex);
	}
}

static int uvc_register_video(struct uvc_device *dev,
		struct uvc_streaming *stream)
{
	int ret;

	/* Initialize the streaming interface with default parameters. */
	ret = uvc_video_init(stream);
	if (ret < 0) {
		printf("%s: Failed to initialize the device (%d).\n", __func__, ret);
		return ret;
	}

	/* Initialize the video buffers queue. */
	return uvc_queue_init(&stream->queue, !uvc_no_drop_param);
}

/*
 * Register all video devices in all chains.
 */
static int uvc_register_terms(struct uvc_device *dev,
	struct uvc_video_chain *chain)
{
	struct uvc_streaming *stream;
	struct uvc_entity *term;
	int ret;

	list_for_each_entry(term, &chain->entities, chain) {
		if (UVC_ENTITY_TYPE(term) != UVC_TT_STREAMING)
			continue;

		stream = uvc_stream_by_id(dev, term->id);
		if (stream == NULL) {
			printf("%s: No streaming interface found for terminal %u.\n",
				 __func__, term->id);
			continue;
		}

		stream->chain = chain;
		ret = uvc_register_video(dev, stream);
		if (ret < 0) {
			stream->chain = NULL;
			return ret;
		}

		dev->active_streams++;
	}

	return 0;
}

static int uvc_register_chains(struct uvc_device *dev)
{
	struct uvc_video_chain *chain;
	int ret;

	list_for_each_entry(chain, &dev->chains, list) {
		ret = uvc_register_terms(dev, chain);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* ------------------------------------------------------------------------
 * USB probe, disconnect
 */

static int uvc_probe(struct usb_interface *intf,
		     const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct uvc_device *dev;
	int function;
	int minor;
	int ret;

	for (minor = 0; minor < UVC_MINORS && uvc_table[minor]; minor++);

	if (minor == UVC_MINORS) {
		printf("%s: no more free uvc devices\n", __func__);
		return -ENODEV;
	}

	/* Allocate memory for the device and initialize it. */
	dev = malloc(sizeof(*dev));
	if (dev == NULL)
		return -ENOMEM;
	memset(dev, 0 , sizeof(*dev));

	INIT_LIST_HEAD(&dev->entities);
	INIT_LIST_HEAD(&dev->chains);
	INIT_LIST_HEAD(&dev->streams);

	dev->minor = minor;
	dev->udev = udev;
	dev->intf = intf;
	dev->intfnum = intf->cur_altsetting->desc.bInterfaceNumber;

	if (udev->product != NULL)
		strncpy(dev->name, udev->product, sizeof(dev->name));
	else
		snprintf(dev->name, sizeof(dev->name), "UVC Camera (%04x:%04x)",
			 udev->descriptor.idVendor, udev->descriptor.idProduct);

	/*
	 * Add iFunction or iInterface to names when available as additional
	 * distinguishers between interfaces. iFunction is prioritized over
	 * iInterface which matches Windows behavior at the point of writing.
	 */
	if (intf->intf_assoc && intf->intf_assoc->iFunction != 0)
		function = intf->intf_assoc->iFunction;
	else
		function = intf->cur_altsetting->desc.iInterface;
	if (function != 0) {
		size_t len;

		strlcat(dev->name, ": ", sizeof(dev->name));
		len = strlen(dev->name);
		usb_string(udev, function, dev->name + len, sizeof(dev->name) - len);
	}

	/* Parse the Video Class control descriptor. */
	if (uvc_parse_control(dev) < 0) {
		printf("%s: Unable to parse UVC descriptors\n", __func__);
		goto error;
	}

	printf("Found UVC %u.%02x device %s (%04x:%04x)\n", dev->uvc_version >> 8, dev->uvc_version & 0xff,
		 udev->product ? udev->product : "<unnamed>", udev->descriptor.idVendor, udev->descriptor.idProduct);

	/* Scan the device for video chains. */
	if (uvc_scan_device(dev) < 0)
		goto error;

	/* Register video device nodes. */
	if (uvc_register_chains(dev) < 0)
		goto error;

	/* Save our data pointer in the interface data. */
	usb_set_intfdata(intf, dev);

	/* Initialize the interrupt URB. */
	ret = uvc_status_init(dev);
	if (ret < 0)
		printf("Unable to initialize the status endpoint (%d), status interrupt will not be supported.\n", ret);

	uvc_table[minor] = dev;

	printf("UVC device initialized\n");

	update_devices_bit();

	return 0;

error:
	uvc_delete(dev);
	uvc_dev_free(dev);
	return -ENODEV;
}

static void uvc_disconnect(struct usb_interface *intf)
{
	u32 id;
	struct uvc_device *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (intf->cur_altsetting->desc.bInterfaceSubClass == UVC_SC_VIDEOSTREAMING)
		return;

	if (!dev || !dev->udev)
		return;

	id = dev->minor;
	assert(id < UVC_MINORS);

	mutex_lock(&device_lock[id]);
	dev->udev = NULL;
	uvc_status_unregister(dev);
	uvc_delete(dev);

	if (!dev->opened) {
		uvc_table[dev->minor] = NULL;
		uvc_dev_free(dev);
	}
	mutex_unlock(&device_lock[id]);

	printf("USB UVC device disconnect [%d]\n", id);

	update_devices_bit();
}

static const struct usb_device_id uvc_ids[] = {
	/* Generic USB Video Class */
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, UVC_PC_PROTOCOL_UNDEFINED) },
	{ USB_INTERFACE_INFO(USB_CLASS_VIDEO, 1, UVC_PC_PROTOCOL_15) },
	{}
};

static struct usb_driver uvc_driver = {
	.name		= "uvcvideo",
	.probe		= uvc_probe,
	.disconnect	= uvc_disconnect,
	.id_table	= uvc_ids,
};

void usb_uvc_driver_register(void)
{
	int i;

	for (i = 0; i < UVC_MINORS; i++) {
		mutex_init(&device_lock[i]);
		thread_cond_init(&free_cond[i]);
	}

	usb_register_driver(&uvc_driver);
}

void usb_uvc_driver_deregister(void)
{
	usb_deregister(&uvc_driver);
}

