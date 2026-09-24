#include <common.h>
#include <os.h>
#include "../composite.h"

#include "uvc.h"

/* module parameters specific to the Video streaming endpoint */

/* 1 - 1023 (FS), 1 - 3072 (hs/ss) */
#ifdef CONFIG_USB_FORCE_FULL_SPEED
#define STREAMING_MAXPACKET		1023
#else
#define STREAMING_MAXPACKET		3072
#endif

/* 1 - 16 */
#define STREAMING_INTERVAL			1

#define UVC_GUID_SIZE	16

/* --------------------------------------------------------------------------
 * Device descriptor
 */

static char webcam_product_label[] = "Webcam gadget";
static char webcam_config_label[] = "Video";
static const unsigned char yuy2_guid[UVC_GUID_SIZE] = UVC_GUID_FORMAT_YUY2;
static const unsigned char nv12_guid[UVC_GUID_SIZE] = UVC_GUID_FORMAT_NV12;
static const unsigned char bgr3_guid[UVC_GUID_SIZE] = UVC_GUID_FORMAT_BGR3;
static const unsigned char y8_guid[UVC_GUID_SIZE] = UVC_GUID_FORMAT_Y8;
static const unsigned char raw10_guid[UVC_GUID_SIZE] = UVC_GUID_FORMAT_RW10;
static const unsigned char h264_guid[UVC_GUID_SIZE] = UVC_GUID_FORMAT_H264;

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

static struct usb_string webcam_strings[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
	[USB_GADGET_PRODUCT_IDX].s = webcam_product_label,
	[USB_GADGET_SERIAL_IDX].s = "ingenic",
	[STRING_DESCRIPTION_IDX].s = webcam_config_label,
	{  }
};

static struct usb_gadget_strings webcam_stringtab = {
	.language = 0x0409,	/* en-us */
	.strings = webcam_strings,
};

static struct usb_gadget_strings *webcam_device_strings[] = {
	&webcam_stringtab,
	NULL,
};

static struct usb_device_descriptor webcam_device_descriptor = {
	.bLength		= USB_DT_DEVICE_SIZE,
	.bDescriptorType	= USB_DT_DEVICE,
	.bcdUSB = 0 , /* dynamic */
	.bDeviceClass		= USB_CLASS_MISC,
	.bDeviceSubClass	= 0x02,
	.bDeviceProtocol	= 0x01,
	.bMaxPacketSize0	= 0, /* dynamic */
	.idVendor		= 0, /* dynamic */
	.idProduct		= 0, /* dynamic */
	.iManufacturer		= 0, /* dynamic */
	.iProduct		= 0, /* dynamic */
	.iSerialNumber		= 0, /* dynamic */
	.bNumConfigurations	= 0, /* dynamic */
};

DECLARE_UVC_HEADER_DESCRIPTOR(1);

static const struct UVC_HEADER_DESCRIPTOR(1) uvc_control_header = {
	.bLength		= UVC_DT_HEADER_SIZE(1),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= UVC_VC_HEADER,
	.bcdUVC			= 0x0110,
	.wTotalLength		= 0, /* dynamic */
	.dwClockFrequency	= CONFIG_EXTAL_CLOCK,
	.bInCollection		= 0, /* dynamic */
	.baInterfaceNr[0]	= 0, /* dynamic */
};

static struct uvc_camera_terminal_descriptor uvc_camera_terminal = {
	.bLength		= UVC_DT_CAMERA_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= UVC_VC_INPUT_TERMINAL,
	.bTerminalID		= UVC_ENTITY_CAMERA_TERMINAL_ID,
	.wTerminalType		= UVC_ITT_CAMERA,
	.bAssocTerminal		= 0,
	.iTerminal		= 0,
	.wObjectiveFocalLengthMin	= 0,
	.wObjectiveFocalLengthMax	= 0,
	.wOcularFocalLength		= 0,
	.bControlSize		= 3,
	.bmControls[0]		= 0,
	.bmControls[1]		= 0,
	.bmControls[2]		= 0,
};

static struct uvc_processing_unit_descriptor uvc_processing = {
	.bLength		= UVC_DT_PROCESSING_UNIT_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= UVC_VC_PROCESSING_UNIT,
	.bUnitID		= UVC_ENTITY_PROCESS_UNIT_ID,
	.bSourceID		= UVC_ENTITY_CAMERA_TERMINAL_ID,
	.wMaxMultiplier		= 0,
	.bControlSize		= 3,
	.bmControls[0]		= 0,
	.bmControls[1]		= 0,
	.bmControls[2]		= 0,
	.iProcessing		= 0,
	.bmVideoStandards = 0,
};

static const struct uvc_output_terminal_descriptor uvc_output_terminal = {
	.bLength		= UVC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubType	= UVC_VC_OUTPUT_TERMINAL,
	.bTerminalID		= UVC_ENTITY_OUTPUT_TERMINAL_ID,
	.wTerminalType		= UVC_TT_STREAMING,
	.bAssocTerminal		= 0,
	.bSourceID		= UVC_ENTITY_PROCESS_UNIT_ID,
	.iTerminal		= 0,
};

static const struct uvc_descriptor_header * const uvc_fs_control_cls[] = {
	(const struct uvc_descriptor_header *) &uvc_control_header,
	(const struct uvc_descriptor_header *) &uvc_camera_terminal,
	(const struct uvc_descriptor_header *) &uvc_processing,
	(const struct uvc_descriptor_header *) &uvc_output_terminal,
	NULL,
};

static struct uvc_input_header_descriptor* uvc_input_header_malloc(u8 num_formats)
{
	struct uvc_input_header_descriptor *uvc_header;

	assert(num_formats > 0);
	uvc_header = malloc(UVC_DT_INPUT_HEADER_SIZE(num_formats));
	assert(uvc_header);

	memset(uvc_header, 0, UVC_DT_INPUT_HEADER_SIZE(num_formats));
	uvc_header->bLength = UVC_DT_INPUT_HEADER_SIZE(num_formats);
	uvc_header->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_header->bDescriptorSubType = UVC_VS_INPUT_HEADER;
	uvc_header->bNumFormats = num_formats;
	uvc_header->wTotalLength = 0; /* dynamic */
	uvc_header->bEndpointAddress = 0; /* dynamic */
	uvc_header->bmInfo = 0;
	uvc_header->bTerminalLink = UVC_ENTITY_OUTPUT_TERMINAL_ID;
	uvc_header->bStillCaptureMethod = 0;
	uvc_header->bTriggerSupport = 0;
	uvc_header->bTriggerUsage = 0;
	uvc_header->bControlSize = 1;

	return uvc_header;
}

static struct uvc_format_uncompressed*
uvc_format_uncompressed_malloc(struct uvc_function_config_format *formats)
{
	struct uvc_format_uncompressed *uvc_format;

	uvc_format = malloc(UVC_DT_FORMAT_UNCOMPRESSED_SIZE);
	assert(uvc_format);

	memset(uvc_format, 0, UVC_DT_FORMAT_UNCOMPRESSED_SIZE);
	uvc_format->bLength = UVC_DT_FORMAT_UNCOMPRESSED_SIZE;
	uvc_format->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_format->bDescriptorSubType = UVC_VS_FORMAT_UNCOMPRESSED;
	uvc_format->bFormatIndex = formats->index;
	uvc_format->bNumFrameDescriptors = formats->num_frames;
	uvc_format->bBitsPerPixel = formats->bpp;
	uvc_format->bDefaultFrameIndex = 1;
	uvc_format->bAspectRatioX = 0;
	uvc_format->bAspectRatioY = 0;
	uvc_format->bmInterfaceFlags = 0;
	uvc_format->bCopyProtect = 0;

	return uvc_format;
}

static struct uvc_frame_uncompressed*
uvc_frame_uncompressed_malloc(struct uvc_function_config_frame *frame, unsigned int bpp)
{
	int i;
	struct uvc_frame_uncompressed *uvc_frame;

	assert(frame->num_intervals > 0);
	uvc_frame = malloc(UVC_DT_FRAME_UNCOMPRESSED_SIZE(frame->num_intervals));
	assert(uvc_frame);

	memset(uvc_frame, 0, UVC_DT_FRAME_UNCOMPRESSED_SIZE(frame->num_intervals));
	uvc_frame->bLength = UVC_DT_FRAME_UNCOMPRESSED_SIZE(frame->num_intervals);
	uvc_frame->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_frame->bDescriptorSubType = UVC_VS_FRAME_UNCOMPRESSED;
	uvc_frame->bFrameIndex = frame->index;
	uvc_frame->bmCapabilities = 0;
	uvc_frame->wWidth = frame->width;
	uvc_frame->wHeight = frame->height;
	uvc_frame->dwMaxVideoFrameBufferSize = frame->width * frame->height * bpp / 8;
	uvc_frame->dwDefaultFrameInterval = frame->intervals[0];
	uvc_frame->bFrameIntervalType = frame->num_intervals;

	uvc_frame->dwMinBitRate = (10000000 / frame->intervals[frame->num_intervals - 1]) * uvc_frame->dwMaxVideoFrameBufferSize * 8;
	uvc_frame->dwMaxBitRate = (10000000 / frame->intervals[0]) * uvc_frame->dwMaxVideoFrameBufferSize * 8;

	for (i = 0; i < frame->num_intervals; i++)
		uvc_frame->dwFrameInterval[i] = frame->intervals[i];

	return uvc_frame;
}

static struct uvc_format_based*
uvc_format_based_malloc(struct uvc_function_config_format *formats)
{
	struct uvc_format_based *uvc_format;

	uvc_format = malloc(UVC_DT_FORMAT_BASED_SIZE);
	assert(uvc_format);

	memset(uvc_format, 0, UVC_DT_FORMAT_BASED_SIZE);
	uvc_format->bLength = UVC_DT_FORMAT_BASED_SIZE;
	uvc_format->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_format->bDescriptorSubType = UVC_VS_FORMAT_FRAME_BASED;
	uvc_format->bFormatIndex = formats->index;
	uvc_format->bNumFrameDescriptors = formats->num_frames;
	uvc_format->bBitsPerPixel = formats->bpp;
	uvc_format->bDefaultFrameIndex = 1;
	uvc_format->bAspectRatioX = 0;
	uvc_format->bAspectRatioY = 0;
	uvc_format->bmInterfaceFlags = 0;
	uvc_format->bCopyProtect = 0;
	uvc_format->bVariableSize = 1;

	return uvc_format;
}

static struct uvc_frame_based*
uvc_frame_based_malloc(struct uvc_function_config_frame *frame, unsigned int bpp)
{
	int i;
	struct uvc_frame_based *uvc_frame;

	assert(frame->num_intervals > 0);
	uvc_frame = malloc(UVC_DT_FRAME_BASED_SIZE(frame->num_intervals));
	assert(uvc_frame);

	memset(uvc_frame, 0, UVC_DT_FRAME_BASED_SIZE(frame->num_intervals));
	uvc_frame->bLength = UVC_DT_FRAME_BASED_SIZE(frame->num_intervals);
	uvc_frame->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_frame->bDescriptorSubType = UVC_VS_FRAME_FRAME_BASED;
	uvc_frame->bFrameIndex = frame->index;
	uvc_frame->bmCapabilities = 0;
	uvc_frame->wWidth = frame->width;
	uvc_frame->wHeight = frame->height;
	uvc_frame->dwDefaultFrameInterval = frame->intervals[0];
	uvc_frame->bFrameIntervalType = frame->num_intervals;
	uvc_frame->dwBytesPerLine = 0;

	uvc_frame->dwMinBitRate = (10000000 / frame->intervals[frame->num_intervals - 1]) * frame->width * frame->height * bpp;
	uvc_frame->dwMaxBitRate = (10000000 / frame->intervals[0]) * frame->width * frame->height * bpp;

	for (i = 0; i < frame->num_intervals; i++)
		uvc_frame->dwFrameInterval[i] = frame->intervals[i];

	return uvc_frame;
}

static struct uvc_format_mjpeg* uvc_format_mjpeg_malloc(struct uvc_function_config_format *formats)
{
	struct uvc_format_mjpeg *uvc_format;

	uvc_format = malloc(UVC_DT_FORMAT_MJPEG_SIZE);
	assert(uvc_format);

	memset(uvc_format, 0, UVC_DT_FORMAT_MJPEG_SIZE);
	uvc_format->bLength = UVC_DT_FORMAT_MJPEG_SIZE;
	uvc_format->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_format->bDescriptorSubType = UVC_VS_FORMAT_MJPEG;
	uvc_format->bFormatIndex = formats->index;
	uvc_format->bNumFrameDescriptors = formats->num_frames;
	uvc_format->bmFlags = 0;
	uvc_format->bDefaultFrameIndex = 1;
	uvc_format->bAspectRatioX = 0;
	uvc_format->bAspectRatioY = 0;
	uvc_format->bmInterfaceFlags = 0;
	uvc_format->bCopyProtect = 0;

	return uvc_format;
}

static struct uvc_frame_mjpeg* uvc_frame_mjpeg_malloc(struct uvc_function_config_frame *frame, unsigned int bpp)
{
	int i;
	struct uvc_frame_mjpeg *uvc_frame;

	uvc_frame = malloc(UVC_DT_FRAME_MJPEG_SIZE(frame->num_intervals));
	assert(uvc_frame);

	memset(uvc_frame, 0, UVC_DT_FRAME_MJPEG_SIZE(frame->num_intervals));
	uvc_frame->bLength = UVC_DT_FRAME_MJPEG_SIZE(frame->num_intervals);
	uvc_frame->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_frame->bDescriptorSubType = UVC_VS_FRAME_MJPEG;
	uvc_frame->bFrameIndex = frame->index;
	uvc_frame->bmCapabilities = 0;
	uvc_frame->wWidth = frame->width;
	uvc_frame->wHeight = frame->height;
	uvc_frame->dwMaxVideoFrameBufferSize = frame->width * frame->height * bpp / 8;
	uvc_frame->dwDefaultFrameInterval = frame->intervals[0];
	uvc_frame->bFrameIntervalType = frame->num_intervals;

	uvc_frame->dwMinBitRate = (10000000 / frame->intervals[frame->num_intervals - 1]) * uvc_frame->dwMaxVideoFrameBufferSize * 8;
	uvc_frame->dwMaxBitRate = (10000000 / frame->intervals[0]) * uvc_frame->dwMaxVideoFrameBufferSize * 8;

	for (i = 0; i < frame->num_intervals; i++)
		uvc_frame->dwFrameInterval[i] = frame->intervals[i];

	return uvc_frame;
}

static struct uvc_color_matching_descriptor* uvc_color_matching_malloc(void)
{
	struct uvc_color_matching_descriptor *uvc_color;

	uvc_color = malloc(UVC_DT_COLOR_MATCHING_SIZE);
	assert(uvc_color);

	memset(uvc_color, 0, UVC_DT_COLOR_MATCHING_SIZE);
	uvc_color->bLength = UVC_DT_COLOR_MATCHING_SIZE;
	uvc_color->bDescriptorType = USB_DT_CS_INTERFACE;
	uvc_color->bDescriptorSubType = UVC_VS_COLORFORMAT;
	uvc_color->bColorPrimaries = 1;
	uvc_color->bTransferCharacteristics = 1;
	uvc_color->bMatrixCoefficients = 4;

	return uvc_color;
}

struct uvc_descriptor_header **uvc_descriptor_header_malloc(struct uvc_function_config_streaming *stream)
{
	int i ,j;
	void *header;
	int header_des_num;
	unsigned int count = 0;
	struct uvc_function_config_frame *frames;
	struct uvc_function_config_format *formats;
	struct uvc_input_header_descriptor *input_header;
	struct uvc_descriptor_header ** uvc_streaming_cls;

	const unsigned char *guid;
	struct uvc_format_uncompressed* uncompressed_format;
	struct uvc_format_based* based_format;

	assert(stream);

	formats = stream->formats;

	header_des_num = stream->num_formats;
	for (i = 0; i < stream->num_formats; i++) {
		header_des_num += formats[i].num_frames;
	}
	header_des_num += 3;

	uvc_streaming_cls = malloc(header_des_num * sizeof(struct uvc_descriptor_header *));

	input_header = uvc_input_header_malloc(stream->num_formats);
	uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)input_header;

	for (i = 0; i < stream->num_formats; i++) {
		frames = formats[i].frames;
		/* compressed formats */
		if (V4L2_PIX_FMT_MJPEG == formats[i].fcc) {
			header = uvc_format_mjpeg_malloc(&formats[i]);
			uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)header;

			for (j = 0; j < formats[i].num_frames; j++) {
				header = uvc_frame_mjpeg_malloc(&frames[j], formats[i].bpp);
				uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)header;
			}
		} else if (V4L2_PIX_FMT_H264 == formats[i].fcc) {
			based_format = uvc_format_based_malloc(&formats[i]);
			memcpy(based_format->guidFormat, h264_guid, UVC_GUID_SIZE);

			uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)based_format;

			for (j = 0; j < formats[i].num_frames; j++) {
				header = uvc_frame_based_malloc(&frames[j], formats[i].bpp);
				uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)header;
			}
		} else {
			/* uncompressed formats */
			if (V4L2_PIX_FMT_YUYV == formats[i].fcc)
				guid = yuy2_guid;
			else if (V4L2_PIX_FMT_NV12 == formats[i].fcc)
				guid = nv12_guid;
			else if (V4L2_PIX_FMT_BGR24 == formats[i].fcc)
				guid = bgr3_guid;
			else if (V4L2_PIX_FMT_GREY == formats[i].fcc)
				guid = y8_guid;
			else if (V4L2_PIX_FMT_SBGGR10P == formats[i].fcc)
				guid = raw10_guid;
			else
				panic("ERROR: gadget uvc not support fcc 0x%x\n", formats[i].fcc);

			uncompressed_format = uvc_format_uncompressed_malloc(&formats[i]);
			memcpy(uncompressed_format->guidFormat, guid, UVC_GUID_SIZE);
			uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)uncompressed_format;

			for (j = 0; j < formats[i].num_frames; j++) {
				header = uvc_frame_uncompressed_malloc(&frames[j], formats[i].bpp);
				uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)header;
			}

		}
	}

	header = uvc_color_matching_malloc();
	uvc_streaming_cls[count++] = (struct uvc_descriptor_header *)header;

	uvc_streaming_cls[count++] = NULL;

	return uvc_streaming_cls;
}

void uvc_descriptor_header_free(struct uvc_descriptor_header **uvc_streaming_cls, struct uvc_function_config_streaming *stream)
{
	int i;
	int header_des_num;

	header_des_num = stream->num_formats;
	for (i = 0; i < stream->num_formats; i++)
		header_des_num += stream->formats[i].num_frames;

	header_des_num += 3;


	for (i = 0; i < header_des_num; i++) {
		free(uvc_streaming_cls[i]);
		uvc_streaming_cls[i] = NULL;
	}

	free(uvc_streaming_cls);
}


/* --------------------------------------------------------------------------
 * USB configuration
 */
static const struct uvc_callback *uvc_callback_p;
static struct uvc_function_config_streaming *uvc_config_stream;
static struct uvc_descriptor_header ** uvc_fs_streaming_cls;

static struct usb_configuration webcam_config_driver = {
	.label			= webcam_config_label,
	.bConfigurationValue	= 1,
	.iConfiguration		= 0, /* dynamic */
};

static int webcam_unbind(struct usb_composite_dev *cdev)
{
	uvc_free();

	return 0;
}

static int webcam_config_bind(struct usb_configuration *c)
{
	int status = 0;
	struct usb_function *f_uvc;
	struct uvc_device *uvc;

	f_uvc = uvc_alloc();
	if (IS_ERR(f_uvc))
		return PTR_ERR(f_uvc);

	uvc = to_uvc(f_uvc);

	uvc->uvc_config_stream = uvc_config_stream;

	uvc->desc.fs_control = uvc_fs_control_cls;
	uvc->desc.fs_streaming = (const struct uvc_descriptor_header **)uvc_fs_streaming_cls;
	uvc->desc.hs_streaming = (const struct uvc_descriptor_header **)uvc_fs_streaming_cls;

	if (uvc_callback_p)
		uvc->uvc_callback = *uvc_callback_p;

	uvc->streaming_interval = STREAMING_INTERVAL;
	uvc->streaming_maxpacket = STREAMING_MAXPACKET;

	status = usb_add_function(c, f_uvc);
	if (status < 0)
		uvc_free();

	return status;
}

static int webcam_bind(struct usb_composite_dev *cdev)
{
	int ret;

	/* Allocate string descriptor numbers ... note that string contents
	 * can be overridden by the composite_dev glue.
	 */
	ret = usb_string_ids_tab(cdev, webcam_strings);
	if (ret < 0)
		return  ret;

	webcam_device_descriptor.iManufacturer = webcam_strings[USB_GADGET_MANUFACTURER_IDX].id;
	webcam_device_descriptor.iProduct = webcam_strings[USB_GADGET_PRODUCT_IDX].id;
	webcam_device_descriptor.iSerialNumber = webcam_strings[USB_GADGET_SERIAL_IDX].id;
	webcam_config_driver.iConfiguration = webcam_strings[STRING_DESCRIPTION_IDX].id;

	/* Register our configuration. */
	if ((ret = usb_add_config(cdev, &webcam_config_driver, webcam_config_bind)) < 0)
		return ret;

	printf("Webcam Video Gadget\n");
	return 0;
}

/* --------------------------------------------------------------------------
 * Driver
 */

static struct usb_composite_driver webcam_driver = {
	.name		= "g_webcam",
	.dev		= &webcam_device_descriptor,
	.strings	= webcam_device_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= webcam_bind,
	.unbind		= webcam_unbind,
};


static struct uvc_function_config_streaming *uvc_config_format_malloc(const struct uvc_device_config *dev_config)
{
	int i, j, k;
	struct uvc_function_config_frame *frame_function;
	struct uvc_function_config_format *format_function;
	struct uvc_function_config_streaming *stream_function;

	const struct uvc_format_config *format_config;
	const struct uvc_frame_config *frame_config;

	assert(dev_config);

	stream_function = malloc(sizeof(struct uvc_function_config_streaming));
	assert(stream_function);

	stream_function->num_formats = dev_config->format_num;
	assert(stream_function->num_formats > 0);
	format_function = malloc(stream_function->num_formats * sizeof(struct uvc_function_config_format));
	assert(format_function);

	stream_function->formats = format_function;
	format_config = dev_config->formats;

	for (i = 0; i < stream_function->num_formats; i++) {
		format_function[i].fcc = format_config[i].fcc;
		format_function[i].bpp = format_config[i].bpp;
		format_function[i].index = i + 1;
		format_function[i].num_frames = format_config[i].frames_num;

		assert(format_function[i].num_frames > 0);

		frame_function = malloc(format_function[i].num_frames * sizeof(struct uvc_function_config_frame));
		assert(frame_function);
		format_function[i].frames = frame_function;

		frame_config = format_config[i].frames;
		for (j = 0; j < format_function[i].num_frames; j++) {
			frame_function[j].index = j + 1;
			frame_function[j].width = frame_config[j].width;
			frame_function[j].height = frame_config[j].height;
			frame_function[j].num_intervals = frame_config[j].fps_num;

			assert(frame_function[j].num_intervals > 0);

			frame_function[j].intervals = malloc(frame_function[j].num_intervals * sizeof(unsigned int));
			for (k = 0; k < frame_function[j].num_intervals; k++)
				frame_function[j].intervals[k] = 10000000 / frame_config[j].frame_fps[k];
			sort_up_int((int *)frame_function[j].intervals, frame_function[j].num_intervals);
		}
	}

	return stream_function;
}

static void uvc_config_format_free(struct uvc_function_config_streaming *stream)
{
	int i, j;

	if(stream) {
		for (i = 0; i < stream->num_formats; i++) {
			for (j = 0; j < stream->formats[i].num_frames; j++) {
				free(stream->formats[i].frames[j].intervals);
				stream->formats[i].frames[j].intervals = NULL;
			}
			free(stream->formats[i].frames);
			stream->formats[i].frames = NULL;
		}

		free(stream->formats);
		stream->formats = NULL;
		free(stream);
	}

}

void gadget_uvc_init(const struct gadget_id *id, const struct uvc_device_config *dev_config, const struct uvc_callback * callback)
{
	int ret;

	assert(id);
	assert(dev_config);

	assert(uvc_config_stream == NULL);
	assert(uvc_fs_streaming_cls == NULL);
	webcam_device_descriptor.idVendor = id->vendor_id;
	webcam_device_descriptor.idProduct = id->product_id;
	memcpy(uvc_camera_terminal.bmControls, &dev_config->camera_feature_config, 3);
	memcpy(uvc_processing.bmControls, &dev_config->camera_param_config, 3);
	uvc_config_stream = uvc_config_format_malloc(dev_config);
	uvc_fs_streaming_cls = uvc_descriptor_header_malloc(uvc_config_stream);
	uvc_callback_p = callback;

	ret = usb_composite_probe(&webcam_driver);
	assert(!ret);
}

void gadget_uvc_cleanup(void)
{
	usb_composite_unregister(&webcam_driver);

	uvc_descriptor_header_free(uvc_fs_streaming_cls, uvc_config_stream);
	uvc_fs_streaming_cls = NULL;

	uvc_config_format_free(uvc_config_stream);
	uvc_config_stream = NULL;

	uvc_callback_p = NULL;
}
