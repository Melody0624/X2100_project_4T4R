#include <common.h>

#include "audio.h"
#include "u_audio.h"

/* UAC1 spec: 3.7.2.3 Audio Channel Cluster Format */
#define UAC1_CHANNEL_MASK 0x0FFF

#define EPIN_EN(_params) ((_params)->c_chmask != 0)
#define EPOUT_EN(_params) ((_params)->p_chmask != 0)

#define EPIN_FEATURE_EN(_params) ((_params)->c_feature != 0)
#define EPOUT_FEATURE_EN(_params) ((_params)->p_feature != 0)


struct f_uac1 {
	struct g_audio g_audio;
	u8 ac_intf, as_in_intf, as_out_intf;
	u8 ac_alt, as_in_alt, as_out_alt;	/* needed for get_alt() */
};

static inline struct f_uac1 *func_to_uac1(struct usb_function *f)
{
	return container_of(f, struct f_uac1, g_audio.func);
}

/*
 * DESCRIPTORS ... most are static, but strings and full
 * configuration descriptors are built on demand.
 */

/*
 * We have three interfaces - one AudioControl and two AudioStreaming
 *
 * The driver implements a simple UAC_1 topology.
 * USB-OUT -> IT_1 -> OT_2 -> ALSA_Capture
 * ALSA_Playback -> IT_3 -> OT_4 -> USB-IN
 */

static struct usb_interface_assoc_descriptor iad_desc = {
	.bLength = sizeof iad_desc,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	/* .bInterfaceCount =	DYNAMIC, */
	.bFunctionClass = USB_CLASS_AUDIO,
	/* .iFunction =		DYNAMIC */
};

/* B.3.1  Standard AC Interface Descriptor */
static struct usb_interface_descriptor ac_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOCONTROL,
};

/* B.3.2  Class-Specific AC Interface Descriptor */
static struct uac1_ac_header_descriptor *ac_header_desc;

static struct uac_input_terminal_descriptor usb_out_it_desc = {
	.bLength =		UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_INPUT_TERMINAL,
	/* .bTerminalID =	DYNAMIC */
	.wTerminalType =	UAC_TERMINAL_STREAMING,
	.bAssocTerminal =	0,
	/* .bNrChannels		= DYNAMIC */
	/* .wChannelConfig		= DYNAMIC */
};

DECLARE_UAC_FEATURE_UNIT_DESCRIPTOR(0);

static struct uac_feature_unit_descriptor_0 sfeature_unit_desc = {
	.bLength		= UAC_DT_FEATURE_UNIT_SIZE(0),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_FEATURE_UNIT,
	/* .bUnitID		= DYNAMIC */
	/* .bSourceID		= DYNAMIC */
	.bControlSize		= 2,
	/* .bmaControls[0]		= DYNAMIC */
};

static struct uac1_output_terminal_descriptor io_out_ot_desc = {
	.bLength		= UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_OUTPUT_TERMINAL,
	/* .bTerminalID =	DYNAMIC */
	.wTerminalType		= UAC_OUTPUT_TERMINAL_SPEAKER,
	.bAssocTerminal		= 0,
	/* .bSourceID =		DYNAMIC */
};

static struct uac_input_terminal_descriptor io_in_it_desc = {
	.bLength		= UAC_DT_INPUT_TERMINAL_SIZE,
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_INPUT_TERMINAL,
	/* .bTerminalID		= DYNAMIC */
	.wTerminalType		= UAC_INPUT_TERMINAL_MICROPHONE,
	.bAssocTerminal		= 0,
	/* .bNrChannels		= DYNAMIC */
	/* .wChannelConfig		= DYNAMIC */
};

static struct uac_feature_unit_descriptor_0 cfeature_unit_desc = {
	.bLength		= UAC_DT_FEATURE_UNIT_SIZE(0),
	.bDescriptorType	= USB_DT_CS_INTERFACE,
	.bDescriptorSubtype	= UAC_FEATURE_UNIT,
	/* .bUnitID		= DYNAMIC */
	/* .bSourceID		= DYNAMIC */
	.bControlSize		= 2,
	/* .bmaControls[0]		= DYNAMIC */
};

static struct uac1_output_terminal_descriptor usb_in_ot_desc = {
	.bLength =		UAC_DT_OUTPUT_TERMINAL_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_OUTPUT_TERMINAL,
	/* .bTerminalID =	DYNAMIC */
	.wTerminalType =	UAC_TERMINAL_STREAMING,
	.bAssocTerminal =	0,
	/* .bSourceID =		DYNAMIC */
};

/* B.4.1  Standard AS Interface Descriptor */
static struct usb_interface_descriptor as_out_interface_alt_0_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_out_interface_alt_1_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	1,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_in_interface_alt_0_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	0,
	.bNumEndpoints =	0,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

static struct usb_interface_descriptor as_in_interface_alt_1_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	.bAlternateSetting =	1,
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_AUDIO,
	.bInterfaceSubClass =	USB_SUBCLASS_AUDIOSTREAMING,
};

/* B.4.2  Class-Specific AS Interface Descriptor */
static struct uac1_as_header_descriptor as_out_header_desc = {
	.bLength =		UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_AS_GENERAL,
	/* .bTerminalLink =	DYNAMIC */
	.bDelay =		0,
	.wFormatTag =		UAC_FORMAT_TYPE_I_PCM,
};

static struct uac1_as_header_descriptor as_in_header_desc = {
	.bLength =		UAC_DT_AS_HEADER_SIZE,
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubtype =	UAC_AS_GENERAL,
	/* .bTerminalLink =	DYNAMIC */
	.bDelay =		0,
	.wFormatTag =		UAC_FORMAT_TYPE_I_PCM,
};

static struct uac_format_type_i_discrete_descriptor *as_out_type_i_desc;

/* Standard ISO OUT Endpoint Descriptor */
static struct usb_endpoint_descriptor as_out_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_SYNC_ASYNC
				| USB_ENDPOINT_XFER_ISOC,
	/* .wMaxPacketSize	=	DYNAMIC */
	.bInterval =		4,
};

static struct usb_endpoint_descriptor as_out_ep_fs_desc;

/* Class-specific AS ISO OUT Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor as_iso_out_desc = {
	.bLength =		UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	UAC_EP_GENERAL,
	.bmAttributes =		UAC_EP_CS_ATTR_SAMPLE_RATE,
	.bLockDelayUnits =	0,
	.wLockDelay =		0,
};

static struct uac_format_type_i_discrete_descriptor *as_in_type_i_desc;

/* Standard ISO OUT Endpoint Descriptor */
static struct usb_endpoint_descriptor as_in_ep_desc = {
	.bLength =		USB_DT_ENDPOINT_AUDIO_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_SYNC_ASYNC
				| USB_ENDPOINT_XFER_ISOC,
	/* .wMaxPacketSize	=	DYNAMIC */
	.bInterval =		4,
};

static struct usb_endpoint_descriptor as_in_ep_fs_desc;

/* Class-specific AS ISO OUT Endpoint Descriptor */
static struct uac_iso_endpoint_descriptor as_iso_in_desc = {
	.bLength =		UAC_ISO_ENDPOINT_DESC_SIZE,
	.bDescriptorType =	USB_DT_CS_ENDPOINT,
	.bDescriptorSubtype =	UAC_EP_GENERAL,
	.bmAttributes =		UAC_EP_CS_ATTR_SAMPLE_RATE,
	.bLockDelayUnits =	0,
	.wLockDelay =		0,
};

static struct usb_descriptor_header *f_audio_fs_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&ac_interface_desc,
	(struct usb_descriptor_header *)&ac_header_desc,

	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&sfeature_unit_desc,
	(struct usb_descriptor_header *)&io_out_ot_desc,
	(struct usb_descriptor_header *)&io_in_it_desc,
	(struct usb_descriptor_header *)&cfeature_unit_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,

	(struct usb_descriptor_header *)&as_out_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_out_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_out_header_desc,

	(struct usb_descriptor_header *)&as_out_type_i_desc,

	(struct usb_descriptor_header *)&as_out_ep_fs_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,

	(struct usb_descriptor_header *)&as_in_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_in_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_in_header_desc,

	(struct usb_descriptor_header *)&as_in_type_i_desc,

	(struct usb_descriptor_header *)&as_in_ep_fs_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

static struct usb_descriptor_header *f_audio_desc[] = {
	(struct usb_descriptor_header *)&iad_desc,
	(struct usb_descriptor_header *)&ac_interface_desc,
	(struct usb_descriptor_header *)&ac_header_desc,

	(struct usb_descriptor_header *)&usb_out_it_desc,
	(struct usb_descriptor_header *)&sfeature_unit_desc,
	(struct usb_descriptor_header *)&io_out_ot_desc,
	(struct usb_descriptor_header *)&io_in_it_desc,
	(struct usb_descriptor_header *)&cfeature_unit_desc,
	(struct usb_descriptor_header *)&usb_in_ot_desc,

	(struct usb_descriptor_header *)&as_out_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_out_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_out_header_desc,

	(struct usb_descriptor_header *)&as_out_type_i_desc,

	(struct usb_descriptor_header *)&as_out_ep_desc,
	(struct usb_descriptor_header *)&as_iso_out_desc,

	(struct usb_descriptor_header *)&as_in_interface_alt_0_desc,
	(struct usb_descriptor_header *)&as_in_interface_alt_1_desc,
	(struct usb_descriptor_header *)&as_in_header_desc,

	(struct usb_descriptor_header *)&as_in_type_i_desc,

	(struct usb_descriptor_header *)&as_in_ep_desc,
	(struct usb_descriptor_header *)&as_iso_in_desc,
	NULL,
};

enum {
	STR_ASSOC,
	STR_AC_IF,
	STR_USB_OUT_IT,
	STR_USB_OUT_IT_CH_NAMES,
	STR_SFEATURE_UNIT,
	STR_IO_OUT_OT,
	STR_IO_IN_IT,
	STR_IO_IN_IT_CH_NAMES,
	STR_CFEATURE_UNIT,
	STR_USB_IN_OT,
	STR_AS_OUT_IF_ALT0,
	STR_AS_OUT_IF_ALT1,
	STR_AS_IN_IF_ALT0,
	STR_AS_IN_IF_ALT1,
};

static struct usb_string strings_uac1[] = {
	[STR_ASSOC].s = "Source/Sink",
	[STR_AC_IF].s = "AC Interface",
	[STR_USB_OUT_IT].s = "Playback Input terminal",
	[STR_USB_OUT_IT_CH_NAMES].s = "Playback Channels",
	[STR_SFEATURE_UNIT].s = "Playback Volume",
	[STR_IO_OUT_OT].s = "Playback Output terminal",
	[STR_IO_IN_IT].s = "Capture Input terminal",
	[STR_IO_IN_IT_CH_NAMES].s = "Capture Channels",
	[STR_CFEATURE_UNIT].s = "Capture Volume",
	[STR_USB_IN_OT].s = "Capture Output terminal",
	[STR_AS_OUT_IF_ALT0].s = "Playback Inactive",
	[STR_AS_OUT_IF_ALT1].s = "Playback Active",
	[STR_AS_IN_IF_ALT0].s = "Capture Inactive",
	[STR_AS_IN_IF_ALT1].s = "Capture Active",
	{ },
};

static struct usb_gadget_strings str_uac1 = {
	.language = 0x0409,	/* en-us */
	.strings = strings_uac1,
};

static struct usb_gadget_strings *uac1_strings[] = {
	&str_uac1,
	NULL,
};

static void f_audio_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct g_audio *audio = req->context;

	switch (req->status) {

	/* normal completion */
	case 0:
		if (audio->feature_cb) {
			audio->feature_cb(audio->control_cs, audio->control_req, req->buf, req->actual);
			audio->feature_cb = NULL;
			audio->control_cs = 0;
			audio->control_req = 0;
		}
		break;

	/* software-driven interface shutdown */
	case -ECONNRESET:        /* unlink */
	case -ESHUTDOWN:        /* disconnect etc */
		break;

	/* for hardware automagic (such as pxa) */
	case -ECONNABORTED:        /* endpoint reset */
		break;

	/* data overrun */
	case -EOVERFLOW:
	default:
		break;
	}
}

static int audio_set_intf_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct g_audio			*audio = func_to_g_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	u8			id = ((ctrl->wIndex >> 8) & 0xFF);
	u8			con_sel = (ctrl->wValue >> 8) & 0xFF;

	if ((ctrl->wIndex & 0xFF) != ac_interface_desc.bInterfaceNumber) {
		printf("%s: Invalid interface id\n", __func__);
		return -EOPNOTSUPP;
	}

	if (audio->params.p_feature && (id == sfeature_unit_desc.bUnitID)) {
		audio->feature_cb = audio->params.p_feature_callback;
	} else if (audio->params.c_feature && (id == cfeature_unit_desc.bUnitID)) {
		audio->feature_cb = audio->params.c_feature_callback;
	}
	else {
		printf("%s: Invalid unit id\n", __func__);
		return -EOPNOTSUPP;
	}

	audio->control_cs = con_sel;
	audio->control_req = ctrl->bRequest;

	req->context = audio;
	req->complete = f_audio_complete;

	return ctrl->wLength;
}

static int audio_get_intf_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct g_audio			*audio = func_to_g_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	u8			id = ((ctrl->wIndex >> 8) & 0xFF);
	u8			con_sel = (ctrl->wValue >> 8) & 0xFF;
	int			value = -EOPNOTSUPP;

	if ((ctrl->wIndex & 0xFF) != ac_interface_desc.bInterfaceNumber) {
		printf("%s: Invalid interface id\n", __func__);
		return -EOPNOTSUPP;
	}

	if (audio->params.p_feature && (id == sfeature_unit_desc.bUnitID)) {
		value = audio->params.p_feature_callback(con_sel, ctrl->bRequest, req->buf, ctrl->wLength);
	} else if (audio->params.c_feature && (id == cfeature_unit_desc.bUnitID)) {
		value = audio->params.c_feature_callback(con_sel, ctrl->bRequest, req->buf, ctrl->wLength);
	} else {
		printf("%s: Invalid unit id\n", __func__);
		return -EOPNOTSUPP;
	}

	audio->feature_cb = NULL;
	audio->control_cs = 0;
	audio->control_req = 0;

	// req->context = audio;
	// req->complete = f_audio_complete;

	return value;
}

static void f_audio_endpoint_complete(struct usb_ep *ep, struct usb_request *req)
{
	u16 len;
	struct g_audio *audio = req->context;

	switch (req->status) {

	/* normal completion */
	case 0:
		if (audio->endpoint_req == UAC_SET_CUR) {
			len = min_t(u16, 3, req->actual);

			if (audio->endpoint & USB_DIR_IN) {
				audio->params.c_srate = 0;
				memcpy(&audio->params.c_srate, req->buf, len);
				u_audio_set_rate_capture(audio);
			} else {
				audio->params.p_srate = 0;
				memcpy(&audio->params.p_srate, req->buf, len);
				u_audio_set_rate_playback(audio);
			}

			audio->endpoint = 0;
			audio->endpoint_req = 0;
		}
		break;

	/* software-driven interface shutdown */
	case -ECONNRESET:        /* unlink */
	case -ESHUTDOWN:        /* disconnect etc */
		break;

	/* for hardware automagic (such as pxa) */
	case -ECONNABORTED:        /* endpoint reset */
		break;

	/* data overrun */
	case -EOVERFLOW:
	default:
		break;
	}
}

static int audio_set_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct g_audio			*audio = func_to_g_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u8			type = ((ctrl->wValue >> 8) & 0xFF);

	if (type != UAC_EP_CS_ATTR_SAMPLE_RATE)
		return -EOPNOTSUPP;

	audio->endpoint_req = ctrl->bRequest;
	audio->endpoint = ctrl->wIndex & 0xFF;

	switch (ctrl->bRequest) {
	case UAC_SET_CUR:
		req->context = audio;
		req->complete = f_audio_endpoint_complete;
		value = ctrl->wLength;
		break;

	case UAC_SET_MIN:
		break;

	case UAC_SET_MAX:
		break;

	case UAC_SET_RES:
		break;

	case UAC_SET_MEM:
		break;

	default:
		break;
	}

	return value;
}

static int audio_get_endpoint_req(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl)
{
	struct g_audio			*audio = func_to_g_audio(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int value = -EOPNOTSUPP;
	u8 type = ((ctrl->wValue >> 8) & 0xFF);
	u16 len;

	if (type != UAC_EP_CS_ATTR_SAMPLE_RATE)
		return -EOPNOTSUPP;

	audio->endpoint_req = ctrl->bRequest;
	audio->endpoint = ctrl->wIndex & 0xFF;

	len = min_t(u16, 3, ctrl->wLength);

	switch (ctrl->bRequest) {
	case UAC_GET_CUR:
		if (audio->endpoint & USB_DIR_IN)
			memcpy(req->buf, &audio->params.c_srate, len);
		else
			memcpy(req->buf, &audio->params.p_srate, len);

		value = len;
		break;
	case UAC_GET_MIN:
		if (audio->endpoint & USB_DIR_IN)
			memcpy(req->buf, &audio->params.c_srates[0], len);
		else
			memcpy(req->buf, &audio->params.p_srates[0], len);

		value = len;
		break;
	case UAC_GET_MAX:
		if (audio->endpoint & USB_DIR_IN)
			memcpy(req->buf, &audio->params.c_srates[audio->params.c_srate_num - 1], len);
		else
			memcpy(req->buf, &audio->params.p_srates[audio->params.p_srate_num - 1], len);

		value = len;
		break;
	case UAC_GET_RES:
		((u8 *) req->buf)[0] = 0x01;
		((u8 *) req->buf)[1] = 0x00;
		((u8 *) req->buf)[2] = 0x00;
		value = len;
		break;
	case UAC_GET_MEM:
		break;
	default:
		break;
	}

	return value;
}

static int f_audio_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = ctrl->wIndex;
	u16			w_value = ctrl->wValue;
	u16			w_length = ctrl->wLength;

	/* composite driver infrastructure handles everything; interface
	 * activation uses set_alt().
	 */
	switch (ctrl->bRequestType) {
	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		value = audio_set_intf_req(f, ctrl);
		break;

	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE:
		value = audio_get_intf_req(f, ctrl);
		break;

	case USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
		value = audio_set_endpoint_req(f, ctrl);
		break;

	case USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_ENDPOINT:
		value = audio_get_endpoint_req(f, ctrl);
		break;

	default:
		printf("%s: invalid control req%02x.%02x v%04x i%04x l%d\n",
			__func__, ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req);
		if (value < 0)
			printf("usb audio response on err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int f_audio_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	// struct usb_composite_dev *cdev = f->config->cdev;
	// struct usb_gadget *gadget = cdev->gadget;

	struct f_uac1 *uac1 = func_to_uac1(f);
	int ret = 0;

	u_audio_connect_status_update(&uac1->g_audio, 1);

	/* No i/f has more than 2 alt settings */
	if (alt > 1) {
		printf("%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (intf == uac1->ac_intf) {
		/* Control I/f has only 1 AltSetting - 0 */
		if (alt) {
			printf("%s:%d Error!\n", __func__, __LINE__);
			return -EINVAL;
		}
		return 0;
	}

	if (intf == uac1->as_out_intf) {
		uac1->as_out_alt = alt;

		if (alt)
			ret = u_audio_start_playback(&uac1->g_audio);
		else
			u_audio_stop_playback(&uac1->g_audio);
	} else if (intf == uac1->as_in_intf) {
		uac1->as_in_alt = alt;

		if (alt)
			ret = u_audio_start_capture(&uac1->g_audio);
		else
			u_audio_stop_capture(&uac1->g_audio);
	} else {
		printf("%s:%d Error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	return ret;
}

static int f_audio_get_alt(struct usb_function *f, unsigned intf)
{
	// struct usb_composite_dev *cdev = f->config->cdev;
	// struct usb_gadget *gadget = cdev->gadget;
	struct f_uac1 *uac1 = func_to_uac1(f);

	if (intf == uac1->ac_intf)
		return uac1->ac_alt;
	else if (intf == uac1->as_out_intf)
		return uac1->as_out_alt;
	else if (intf == uac1->as_in_intf)
		return uac1->as_in_alt;
	else
		printf("%s:%d Invalid Interface %d!\n", __func__, __LINE__, intf);

	return -EINVAL;
}


static void f_audio_disable(struct usb_function *f)
{
	struct f_uac1 *uac1 = func_to_uac1(f);

	uac1->as_out_alt = 0;
	uac1->as_in_alt = 0;

	u_audio_stop_playback(&uac1->g_audio);
	u_audio_stop_capture(&uac1->g_audio);

	u_audio_connect_status_update(&uac1->g_audio, 0);
}

/*-------------------------------------------------------------------------*/

static struct uac1_ac_header_descriptor *build_ac_header_desc(struct uac1_params *params)
{
	struct uac1_ac_header_descriptor *ac_desc;
	int ac_header_desc_size;
	int num_ifaces = 0;

	if (EPOUT_EN(params))
		num_ifaces++;
	if (EPIN_EN(params))
		num_ifaces++;

	ac_header_desc_size = UAC_DT_AC_HEADER_SIZE(num_ifaces);

	ac_desc = malloc(ac_header_desc_size);
	if (!ac_desc)
		return NULL;

	memset(ac_desc, 0, ac_header_desc_size);

	ac_desc->bLength = ac_header_desc_size;
	ac_desc->bDescriptorType = USB_DT_CS_INTERFACE;
	ac_desc->bDescriptorSubtype = UAC_HEADER;
	ac_desc->bcdADC = 0x0100;
	ac_desc->bInCollection = num_ifaces;

	/* wTotalLength and baInterfaceNr will be defined later */

	return ac_desc;
}

static struct uac_format_type_i_discrete_descriptor *build_as_type_i_desc(unsigned int srate_num)
{
	struct uac_format_type_i_discrete_descriptor *as_type_i_desc;
	int as_type_i_desc_size;

	as_type_i_desc_size = UAC_FORMAT_TYPE_I_DISCRETE_DESC_SIZE(srate_num);

	as_type_i_desc = malloc(as_type_i_desc_size);
	if (!as_type_i_desc)
		return NULL;

	memset(as_type_i_desc, 0, as_type_i_desc_size);

	as_type_i_desc->bLength = as_type_i_desc_size;
	as_type_i_desc->bDescriptorType = USB_DT_CS_INTERFACE;
	as_type_i_desc->bDescriptorSubtype = UAC_FORMAT_TYPE;
	as_type_i_desc->bFormatType =		UAC_FORMAT_TYPE_I;
	as_type_i_desc->bSamFreqType =		srate_num;

	return as_type_i_desc;
}

/* Use macro to overcome line length limitation */
#define USBDHDR(p) (struct usb_descriptor_header *)(p)

static void setup_descriptor(struct uac1_params *params)
{
	/* patch descriptors */
	int i = 1; /* ID's start with 1 */

	if (EPOUT_EN(params)) {
		usb_out_it_desc.bTerminalID = i++;
		if (EPOUT_FEATURE_EN(params))
			sfeature_unit_desc.bUnitID = i++;

		io_out_ot_desc.bTerminalID = i++;


		if (EPOUT_FEATURE_EN(params)) {
			sfeature_unit_desc.bSourceID = usb_out_it_desc.bTerminalID;
			io_out_ot_desc.bSourceID = sfeature_unit_desc.bUnitID;
		} else {
			io_out_ot_desc.bSourceID = usb_out_it_desc.bTerminalID;
		}

		as_out_header_desc.bTerminalLink = usb_out_it_desc.bTerminalID;
	}

	if (EPIN_EN(params)) {
		io_in_it_desc.bTerminalID = i++;
		if (EPIN_FEATURE_EN(params))
			cfeature_unit_desc.bUnitID = i++;

		usb_in_ot_desc.bTerminalID = i++;

		if (EPIN_FEATURE_EN(params)) {
			cfeature_unit_desc.bSourceID = io_in_it_desc.bTerminalID;
			usb_in_ot_desc.bSourceID = cfeature_unit_desc.bUnitID;
		} else {
			usb_in_ot_desc.bSourceID = io_in_it_desc.bTerminalID;
		}

		as_in_header_desc.bTerminalLink = usb_in_ot_desc.bTerminalID;
	}

	ac_header_desc->wTotalLength = ac_header_desc->bLength;

	if (EPOUT_EN(params)) {
		u16 len = ac_header_desc->wTotalLength;

		len += sizeof(usb_out_it_desc);
		if (EPOUT_FEATURE_EN(params))
			len += sizeof(sfeature_unit_desc);

		len += sizeof(io_out_ot_desc);
		ac_header_desc->wTotalLength = len;
	}

	if (EPIN_EN(params)) {
		u16 len = ac_header_desc->wTotalLength;

		len += sizeof(usb_in_ot_desc);
		if (EPIN_FEATURE_EN(params))
			len += sizeof(cfeature_unit_desc);

		len += sizeof(io_in_it_desc);
		ac_header_desc->wTotalLength = len;
	}

	i = 0;
	f_audio_desc[i++] = USBDHDR(&iad_desc);
	f_audio_desc[i++] = USBDHDR(&ac_interface_desc);
	f_audio_desc[i++] = USBDHDR(ac_header_desc);

	if (EPOUT_EN(params)) {
		f_audio_desc[i++] = USBDHDR(&usb_out_it_desc);
		if (EPOUT_FEATURE_EN(params))
			f_audio_desc[i++] = USBDHDR(&sfeature_unit_desc);

		f_audio_desc[i++] = USBDHDR(&io_out_ot_desc);
	}

	if (EPIN_EN(params)) {
		f_audio_desc[i++] = USBDHDR(&io_in_it_desc);
		if (EPIN_FEATURE_EN(params))
			f_audio_desc[i++] = USBDHDR(&cfeature_unit_desc);

		f_audio_desc[i++] = USBDHDR(&usb_in_ot_desc);
	}

	if (EPOUT_EN(params)) {
		f_audio_desc[i++] = USBDHDR(&as_out_interface_alt_0_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_interface_alt_1_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_header_desc);
		f_audio_desc[i++] = USBDHDR(as_out_type_i_desc);
		f_audio_desc[i++] = USBDHDR(&as_out_ep_desc);
		f_audio_desc[i++] = USBDHDR(&as_iso_out_desc);
	}
	if (EPIN_EN(params)) {
		f_audio_desc[i++] = USBDHDR(&as_in_interface_alt_0_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_interface_alt_1_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_header_desc);
		f_audio_desc[i++] = USBDHDR(as_in_type_i_desc);
		f_audio_desc[i++] = USBDHDR(&as_in_ep_desc);
		f_audio_desc[i++] = USBDHDR(&as_iso_in_desc);
	}
	f_audio_desc[i] = NULL;


	/* init fs desc */
	memcpy(&as_out_ep_fs_desc, &as_out_ep_desc, sizeof(as_out_ep_desc));
	as_out_ep_fs_desc.bInterval = 1;
	if (as_out_ep_fs_desc.wMaxPacketSize >= 1024)
		as_out_ep_fs_desc.wMaxPacketSize = 1023;

	memcpy(&as_in_ep_fs_desc, &as_in_ep_desc, sizeof(as_in_ep_desc));
	as_in_ep_fs_desc.bInterval = 1;
	if (as_in_ep_fs_desc.wMaxPacketSize >= 1024)
		as_in_ep_fs_desc.wMaxPacketSize = 1023;

	memcpy(&f_audio_fs_desc, &f_audio_desc, sizeof(f_audio_desc));
	for (i = 0; i < ARRAY_SIZE(f_audio_fs_desc); i++) {
		if (f_audio_fs_desc[i] == NULL)
			break;
		else if (f_audio_fs_desc[i] == USBDHDR(&as_out_ep_desc))
			f_audio_fs_desc[i] = USBDHDR(&as_out_ep_fs_desc);
		else if (f_audio_fs_desc[i] == USBDHDR(&as_in_ep_desc))
			f_audio_fs_desc[i] = USBDHDR(&as_in_ep_fs_desc);
	}

}


/* audio function driver setup/binding */
static int f_audio_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev	*cdev = c->cdev;
	struct usb_gadget		*gadget = cdev->gadget;
	struct f_uac1			*uac1 = func_to_uac1(f);
	struct g_audio			*audio = func_to_g_audio(f);
	struct uac1_params *params = &audio->params;
	struct usb_ep			*ep = NULL;
	struct usb_string		*us;
	u8				*sam_freq;
	int				rate;
	int				ba_iface_id;
	int				status;
	int i;

	us = usb_gstrings_attach(cdev, uac1_strings, ARRAY_SIZE(strings_uac1));
	if (IS_ERR(us))
		return PTR_ERR(us);

	ac_header_desc = build_ac_header_desc(params);
	if (!ac_header_desc)
		return -ENOMEM;

	as_out_type_i_desc = build_as_type_i_desc(params->p_srate_num);
	if (!as_out_type_i_desc) {
		status = -ENOMEM;
		goto build_out_type_i_fail;
	}

	as_in_type_i_desc = build_as_type_i_desc(params->c_srate_num);
	if (!as_in_type_i_desc) {
		status = -ENOMEM;
		goto build_in_type_i_fail;
	}

	iad_desc.iFunction = us[STR_ASSOC].id;
	ac_interface_desc.iInterface = us[STR_AC_IF].id;
	usb_out_it_desc.iTerminal = us[STR_USB_OUT_IT].id;
	usb_out_it_desc.iChannelNames = us[STR_USB_OUT_IT_CH_NAMES].id;
	sfeature_unit_desc.iFeature = us[STR_SFEATURE_UNIT].id;
	io_out_ot_desc.iTerminal = us[STR_IO_OUT_OT].id;
	as_out_interface_alt_0_desc.iInterface = us[STR_AS_OUT_IF_ALT0].id;
	as_out_interface_alt_1_desc.iInterface = us[STR_AS_OUT_IF_ALT1].id;
	io_in_it_desc.iTerminal = us[STR_IO_IN_IT].id;
	io_in_it_desc.iChannelNames = us[STR_IO_IN_IT_CH_NAMES].id;
	cfeature_unit_desc.iFeature = us[STR_CFEATURE_UNIT].id;
	usb_in_ot_desc.iTerminal = us[STR_USB_IN_OT].id;
	as_in_interface_alt_0_desc.iInterface = us[STR_AS_IN_IF_ALT0].id;
	as_in_interface_alt_1_desc.iInterface = us[STR_AS_IN_IF_ALT1].id;

	/* Set channel numbers */
	usb_out_it_desc.bNrChannels = num_channels(params->p_chmask);
	usb_out_it_desc.wChannelConfig = params->p_chmask;

	as_out_type_i_desc->bNrChannels = num_channels(params->p_chmask);
	as_out_type_i_desc->bSubframeSize = params->p_ssize;
	as_out_type_i_desc->bBitResolution = params->p_ssize * 8;

	io_in_it_desc.bNrChannels = num_channels(params->c_chmask);
	io_in_it_desc.wChannelConfig = params->c_chmask;

	as_in_type_i_desc->bNrChannels = num_channels(params->c_chmask);
	as_in_type_i_desc->bSubframeSize = params->c_ssize;
	as_in_type_i_desc->bBitResolution = params->c_ssize * 8;

	/* Set sample rates */
	for (i = 0; i < params->p_srate_num; i++) {
		rate = params->p_srates[i];
		sam_freq = as_out_type_i_desc->tSamFreq[i];
		memcpy(sam_freq, &rate, 3);
	}

	for (i = 0; i < params->c_srate_num; i++) {
		rate = params->c_srates[i];
		sam_freq = as_in_type_i_desc->tSamFreq[i];
		memcpy(sam_freq, &rate, 3);
	}

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto bind_fail;

	ac_interface_desc.bInterfaceNumber = status;
	iad_desc.bFirstInterface = status;
	uac1->ac_intf = status;
	uac1->ac_alt = 0;
	iad_desc.bInterfaceCount = 1;

	ba_iface_id = 0;

	if (EPOUT_EN(params)) {
		status = usb_interface_id(c, f);
		if (status < 0)
			goto bind_fail;
		as_out_interface_alt_0_desc.bInterfaceNumber = status;
		as_out_interface_alt_1_desc.bInterfaceNumber = status;
		ac_header_desc->baInterfaceNr[ba_iface_id++] = status;
		uac1->as_out_intf = status;
		uac1->as_out_alt = 0;
		iad_desc.bInterfaceCount++;
	}

	if (EPIN_EN(params)) {
		status = usb_interface_id(c, f);
		if (status < 0)
			goto bind_fail;
		as_in_interface_alt_0_desc.bInterfaceNumber = status;
		as_in_interface_alt_1_desc.bInterfaceNumber = status;
		ac_header_desc->baInterfaceNr[ba_iface_id++] = status;
		uac1->as_in_intf = status;
		uac1->as_in_alt = 0;
		iad_desc.bInterfaceCount++;
	}

	audio->gadget = gadget;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	if (EPOUT_EN(params)) {
		ep = usb_ep_autoconfig(cdev->gadget, &as_out_ep_desc);
		if (!ep)
			goto bind_fail;
		audio->out_ep = ep;
	}

	if (EPIN_EN(params)) {
		ep = usb_ep_autoconfig(cdev->gadget, &as_in_ep_desc);
		if (!ep)
			goto bind_fail;
		audio->in_ep = ep;
	}

	setup_descriptor(params);

	/* copy descriptors, and track endpoint copies */
	status = usb_assign_descriptors(f, f_audio_fs_desc, f_audio_desc);
	if (status)
		goto bind_fail;

	audio->out_ep_maxpsize = as_out_ep_desc.wMaxPacketSize;
	audio->in_ep_maxpsize = as_in_ep_desc.wMaxPacketSize;

	status = g_audio_setup(audio);
	if (status)
		goto err_card_register;

	return 0;

err_card_register:
	usb_free_all_descriptors(f);
bind_fail:
	free(as_in_type_i_desc);
	as_in_type_i_desc = NULL;
build_in_type_i_fail:
	free(as_out_type_i_desc);
	as_out_type_i_desc = NULL;
build_out_type_i_fail:
	free(ac_header_desc);
	ac_header_desc = NULL;
	return status;
}

static void f_audio_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct g_audio *audio = func_to_g_audio(f);

	g_audio_cleanup(audio);
	usb_free_all_descriptors(f);

	free(as_in_type_i_desc);
	as_in_type_i_desc = NULL;
	free(as_out_type_i_desc);
	as_out_type_i_desc = NULL;
	free(ac_header_desc);
	ac_header_desc = NULL;

	audio->gadget = NULL;
}

static int f_audio_validate(const struct uac1_params *params)
{
	if (!params->p_chmask && !params->c_chmask) {
		panic("Error: no playback and capture channels\n");
		return -EINVAL;
	} else if (params->p_chmask & ~UAC1_CHANNEL_MASK) {
		panic("Error: unsupported playback channels mask\n");
		return -EINVAL;
	} else if (params->c_chmask & ~UAC1_CHANNEL_MASK) {
		panic("Error: unsupported capture channels mask\n");
		return -EINVAL;
	} else if (params->buffer_size_ms == 0) {
		panic("Error: buffer_size_ms is zero\n");
		return -EINVAL;
	}

	if (EPOUT_EN(params)) {
		if ((params->p_ssize < 1) || (params->p_ssize > 4)) {
			panic("Error: incorrect playback sample size\n");
			return -EINVAL;
		} else if (params->p_srate == 0) {
			panic("Error: incorrect playback sampling rate\n");
			return -EINVAL;
		} else if (params->p_feature != 0 && params->p_feature_callback == NULL) {
			panic("Error: incorrect not have p_feature_callback\n");
			return -EINVAL;
		}
	}

	if (EPIN_EN(params)) {
		if ((params->c_ssize < 1) || (params->c_ssize > 4)) {
			panic("Error: incorrect capture sample size\n");
			return -EINVAL;
		} else if (params->c_srate == 0) {
			panic("Error: incorrect capture sampling rate\n");
			return -EINVAL;
		} else if (params->c_feature != 0 && params->c_feature_callback == NULL) {
			panic("Error: incorrect not have c_feature_callback\n");
			return -EINVAL;
		}
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

struct usb_function *f_audio_alloc(const struct uac1_params *params)
{
	int ret;
	u32 srate_num;
	u16 max_packet_size;
	u32 *dst_srates;
	const u32 *src_srates;
	struct f_uac1 *uac1;

	ret = f_audio_validate(params);
	if (ret)
		return ERR_PTR(ret);

	/* allocate and initialize one new instance */
	uac1 = malloc(sizeof(*uac1));
	if (!uac1)
		return ERR_PTR(-ENOMEM);

	memset(uac1, 0, sizeof(*uac1));

	uac1->g_audio.func.name = "uac1_func";
	uac1->g_audio.func.bind = f_audio_bind;
	uac1->g_audio.func.unbind = f_audio_unbind;
	uac1->g_audio.func.set_alt = f_audio_set_alt;
	uac1->g_audio.func.get_alt = f_audio_get_alt;
	uac1->g_audio.func.setup = f_audio_setup;
	uac1->g_audio.func.disable = f_audio_disable;

	sfeature_unit_desc.bmaControls[0] = params->p_feature;
	cfeature_unit_desc.bmaControls[0] = params->c_feature;

	memcpy(&uac1->g_audio.params, params, sizeof(struct uac1_params));

	if (EPOUT_EN(params)) {
		if (params->p_srate_num == 0 || params->p_srates == NULL) {
			src_srates = &params->p_srate;
			srate_num = 1;
		} else {
			src_srates = params->p_srates;
			srate_num = params->p_srate_num;
		}

		dst_srates = malloc(sizeof(*src_srates) * srate_num);
		if (!dst_srates) {
			free(uac1);
			return ERR_PTR(-ENOMEM);
		}

		memcpy(dst_srates, src_srates, sizeof(*src_srates) * srate_num);
		sort_up_int((int *)dst_srates, srate_num);

		uac1->g_audio.params.p_srates = dst_srates;
		uac1->g_audio.params.p_srate_num = srate_num;

		max_packet_size = dst_srates[srate_num - 1] / 1000;
		if (dst_srates[srate_num - 1] % 1000)		
			max_packet_size++;
		max_packet_size = max_packet_size * num_channels(params->p_chmask) * params->p_ssize;
		max_packet_size = max_t(u16, 32, max_packet_size);
		as_out_ep_desc.wMaxPacketSize = min_t(u16, 1024, max_packet_size);
	}

	if (EPIN_EN(params)) {
		if (params->c_srate_num == 0 || params->c_srates == NULL) {
			src_srates = &params->c_srate;
			srate_num = 1;
		} else {
			src_srates = params->c_srates;
			srate_num = params->c_srate_num;
		}

		dst_srates = malloc(sizeof(*src_srates) * srate_num);
		if (!dst_srates) {
			if (uac1->g_audio.params.p_srates)
				free(uac1->g_audio.params.p_srates);
			free(uac1);
			return ERR_PTR(-ENOMEM);
		}

		memcpy(dst_srates, src_srates, sizeof(*src_srates) * srate_num);
		sort_up_int((int *)dst_srates, srate_num);

		uac1->g_audio.params.c_srates = dst_srates;
		uac1->g_audio.params.c_srate_num = srate_num;

		max_packet_size = dst_srates[srate_num - 1] / 1000;
		if (dst_srates[srate_num - 1] % 1000)
			max_packet_size++;
		max_packet_size = max_packet_size * num_channels(params->c_chmask) * params->c_ssize;
		max_packet_size = max_t(u16, 32, max_packet_size);
		as_in_ep_desc.wMaxPacketSize = min_t(u16, 1024, max_packet_size);
	}

	return &uac1->g_audio.func;
}

void f_audio_free(struct usb_function *f)
{
	struct g_audio *audio;

	audio = func_to_g_audio(f);
	if (EPOUT_EN(&audio->params))
		free(audio->params.p_srates);
	if (EPIN_EN(&audio->params))
		free(audio->params.c_srates);
	free(audio);
}
