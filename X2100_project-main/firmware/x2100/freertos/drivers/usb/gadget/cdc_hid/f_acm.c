#include <common.h>
#include <malloc.h>
#include <driver/cache.h>
#include <os.h>

#include "serial.h"

/*-------------------------------------------------------------------------*/

/* notification endpoint uses smallish and infrequent fixed-size messages */

#define GS_NOTIFY_INTERVAL_MS		32
#define GS_NOTIFY_MAXPACKET		10	/* notification + 2 bytes */

/* interface and class descriptors: */

static struct usb_interface_assoc_descriptor
acm_iad_descriptor = {
	.bLength =		sizeof acm_iad_descriptor,
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	.bInterfaceCount = 	2,	// control + data
	.bFunctionClass =	USB_CLASS_COMM,
	.bFunctionSubClass =	USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	/* .iFunction =		DYNAMIC */
};


static struct usb_interface_descriptor acm_control_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	/* .iInterface = DYNAMIC */
};

static struct usb_interface_descriptor acm_data_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc acm_header_desc = {
	.bLength =		sizeof(acm_header_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,
	.bcdCDC =		0x0110,
};

static struct usb_cdc_call_mgmt_descriptor
acm_call_mgmt_descriptor = {
	.bLength =		sizeof(acm_call_mgmt_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_CALL_MANAGEMENT_TYPE,
	.bmCapabilities =	0,
	/* .bDataInterface = DYNAMIC */
};

static struct usb_cdc_acm_descriptor acm_descriptor = {
	.bLength =		sizeof(acm_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ACM_TYPE,
	.bmCapabilities =	USB_CDC_CAP_LINE,
};

static struct usb_cdc_union_desc acm_union_desc = {
	.bLength =		sizeof(acm_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	/* .bMasterInterface0 =	DYNAMIC */
	/* .bSlaveInterface0 =	DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor acm_fs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	GS_NOTIFY_MAXPACKET,
	.bInterval =		GS_NOTIFY_INTERVAL_MS,
};

static struct usb_endpoint_descriptor acm_fs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor acm_fs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *acm_fs_function[] = {
	(struct usb_descriptor_header *) &acm_iad_descriptor,
	(struct usb_descriptor_header *) &acm_control_interface_desc,
	(struct usb_descriptor_header *) &acm_header_desc,
	(struct usb_descriptor_header *) &acm_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &acm_descriptor,
	(struct usb_descriptor_header *) &acm_union_desc,
	(struct usb_descriptor_header *) &acm_fs_notify_desc,
	(struct usb_descriptor_header *) &acm_data_interface_desc,
	(struct usb_descriptor_header *) &acm_fs_in_desc,
	(struct usb_descriptor_header *) &acm_fs_out_desc,
	NULL,
};

/* high speed support: */
static struct usb_endpoint_descriptor acm_hs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	GS_NOTIFY_MAXPACKET,
	.bInterval =		USB_MS_TO_HS_INTERVAL(GS_NOTIFY_INTERVAL_MS),
};

static struct usb_endpoint_descriptor acm_hs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	512,
};

static struct usb_endpoint_descriptor acm_hs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	512,
};

static struct usb_descriptor_header *acm_hs_function[] = {
	(struct usb_descriptor_header *) &acm_iad_descriptor,
	(struct usb_descriptor_header *) &acm_control_interface_desc,
	(struct usb_descriptor_header *) &acm_header_desc,
	(struct usb_descriptor_header *) &acm_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &acm_descriptor,
	(struct usb_descriptor_header *) &acm_union_desc,
	(struct usb_descriptor_header *) &acm_hs_notify_desc,
	(struct usb_descriptor_header *) &acm_data_interface_desc,
	(struct usb_descriptor_header *) &acm_hs_in_desc,
	(struct usb_descriptor_header *) &acm_hs_out_desc,
	NULL,
};

/* string descriptors: */

#define ACM_CTRL_IDX	0
#define ACM_DATA_IDX	1
#define ACM_IAD_IDX	2

/* static strings, in UTF-8 */
static struct usb_string acm_string_defs[] = {
	[ACM_CTRL_IDX].s = "CDC Abstract Control Model (ACM)",
	[ACM_DATA_IDX].s = "CDC ACM Data",
	[ACM_IAD_IDX ].s = "CDC Serial",
	{  } /* end of list */
};

static struct usb_gadget_strings acm_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		acm_string_defs,
};

static struct usb_gadget_strings *acm_strings[] = {
	&acm_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

/* ACM control ... data handling is delegated to tty library code.
 * The main task of this function is to activate and deactivate
 * that code based on device state; track parameters like line
 * speed, handshake state, and so on; and issue notifications.
 */

static void acm_complete_set_line_coding(struct usb_ep *ep,
		struct usb_request *req)
{
	struct f_acm	*acm = ep->driver_data;

	if (req->status != 0) {
		printf("acm completion, err %d\n", req->status);
		return;
	}

	/* normal completion */
	if (req->actual != sizeof(acm->port_line_coding)) {
		printf("acm short resp, len %d\n", req->actual);
		usb_ep_set_halt(ep);
	} else {
		struct usb_cdc_line_coding	*value = req->buf;

		/* REVISIT:  we currently just remember this data.
		 * If we change that, (a) validate it first, then
		 * (b) update whatever hardware needs updating,
		 * (c) worry about locking.  This is information on
		 * the order of 9600-8-N-1 ... most of which means
		 * nothing unless we control a real RS232 line.
		 */
		acm->port_line_coding = *value;
		gs_port_update_conding(value);
	}
}

static bool acm_req_match(struct usb_function *f, const struct usb_ctrlrequest *ctrl, bool config0)
{
	struct f_acm		*acm = func_to_acm(f);
	u16			w_index = ctrl->wIndex;
	u16			w_length = ctrl->wLength;

	if (config0 || (w_index != acm->ctrl_id))
		return false;

	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	/* SET_LINE_CODING ... just read and save what the host sends */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_LINE_CODING:
		if (w_length != sizeof(struct usb_cdc_line_coding))
			return false;
		break;

	/* GET_LINE_CODING ... return what host sent, or initial value */
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_GET_LINE_CODING:
		break;

	/* SET_CONTROL_LINE_STATE ... save what the host sent */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		break;

	default:
		return false;
	}

	return true;
}

static int acm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_acm		*acm = func_to_acm(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -1;
	u16			w_index = ctrl->wIndex;
	u16			w_value = ctrl->wValue;
	u16			w_length = ctrl->wLength;

	/* composite driver infrastructure handles everything except
	 * CDC class messages; interface activation uses set_alt().
	 *
	 * Note CDC spec table 4 lists the ACM request profile.  It requires
	 * encapsulated command support ... we don't handle any, and respond
	 * to them by stalling.  Options include get/set/clear comm features
	 * (not that useful) and SEND_BREAK.
	 */
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
	/* SET_LINE_CODING ... just read and save what the host sends */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_LINE_CODING:
		if (w_length != sizeof(struct usb_cdc_line_coding)
				|| w_index != acm->ctrl_id)
			goto invalid;

		value = w_length;
		cdev->gadget->ep0->driver_data = acm;
		req->complete = acm_complete_set_line_coding;
		break;

	/* GET_LINE_CODING ... return what host sent, or initial value */
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_GET_LINE_CODING:
		if (w_index != acm->ctrl_id)
			goto invalid;

		value = min_t(unsigned, w_length,
				sizeof(struct usb_cdc_line_coding));
		memcpy(req->buf, &acm->port_line_coding, value);
		break;

	/* SET_CONTROL_LINE_STATE ... save what the host sent */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		if (w_index != acm->ctrl_id)
			goto invalid;

		value = 0;

		/* FIXME we should not allow data to flow until the
		 * host sets the ACM_CTRL_DTR bit; and when it clears
		 * that bit, we should return to that no-flow state.
		 */
		acm->port_handshake_bits = w_value;
		break;

	default:
invalid:
		printf("invalid control req%02x.%02x v%04x i%04x l%d\n",
			 ctrl->bRequestType, ctrl->bRequest, w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req);
		if (value < 0)
			printf("acm response, err %d\n", value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int acm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_acm		*acm = func_to_acm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* we know alt == 0, so this is an activation or a reset */

	if (intf == acm->ctrl_id) {
		usb_ep_disable(acm->notify);

		if (!acm->notify->desc)
			if (config_ep_by_speed(cdev->gadget, f, acm->notify))
				return -EINVAL;

		usb_ep_enable(acm->notify);

	} else if (intf == acm->data_id) {
		if (!acm->port.in->desc || !acm->port.out->desc) {
			if (config_ep_by_speed(cdev->gadget, f, acm->port.in) ||
				config_ep_by_speed(cdev->gadget, f, acm->port.out)) {
				acm->port.in->desc = NULL;
				acm->port.out->desc = NULL;
				return -EINVAL;
			}
		}
		printf("usb gadget serial connect!!!\n");
		gserial_connect(&acm->port);
	} else
		return -EINVAL;

	return 0;
}

static void acm_disable(struct usb_function *f)
{
	struct f_acm	*acm = func_to_acm(f);

	printf("usb acm deactivated\n");
	gserial_disconnect(&acm->port);
	usb_ep_disable(acm->notify);
}

/*-------------------------------------------------------------------------*/

/**
 * acm_cdc_notify - issue CDC notification to host
 * @acm: wraps host to be notified
 * @type: notification type
 * @value: Refer to cdc specs, wValue field.
 * @data: data to be sent
 * @length: size of data
 * Context: irqs blocked, acm->lock held, acm_notify_req non-null
 *
 * Returns zero on success or a negative errno.
 *
 * See section 6.3.5 of the CDC 1.1 specification for information
 * about the only notification we issue:  SerialState change.
 */
static int acm_cdc_notify(struct f_acm *acm, u8 type, u16 value,
		void *data, unsigned length)
{
	struct usb_ep			*ep = acm->notify;
	struct usb_request		*req;
	struct usb_cdc_notification	*notify;
	const unsigned			len = sizeof(*notify) + length;
	void				*buf;
	int				status;

	req = acm->notify_req;
	acm->notify_req = NULL;
	acm->pending = false;

	req->length = len;
	notify = req->buf;
	buf = notify + 1;

	notify->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	notify->bNotificationType = type;
	notify->wValue = value;
	notify->wIndex = acm->ctrl_id;
	notify->wLength = length;
	memcpy(buf, data, length);

	/* ep_queue() can complete immediately if it fills the fifo... */
	status = usb_ep_queue(ep, req);
	if (status < 0) {
		printf("acm can't notify serial state, %d\n", status);
		acm->notify_req = req;
	}

	return status;
}

static int acm_notify_serial_state(struct f_acm *acm)
{
	int			status;
	u16			serial_state;
	unsigned long flags;

	usb_spin_lock_irqsave(&acm->lock, flags);
	if (acm->notify_req) {
		serial_state = acm->serial_state;
		status = acm_cdc_notify(acm, USB_CDC_NOTIFY_SERIAL_STATE,
				0, &serial_state, sizeof(acm->serial_state));
	} else {
		acm->pending = true;
		status = 0;
	}
	usb_spin_unlock_irqrestore(&acm->lock, flags);

	return status;
}

static void acm_cdc_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_acm		*acm = req->context;
	unsigned long flags;

	/* on this call path we do NOT hold the port spinlock,
	 * which is why ACM needs its own spinlock
	 */
	usb_spin_lock_irqsave(&acm->lock, flags);
	acm->notify_req = req;
	usb_spin_unlock_irqrestore(&acm->lock, flags);

	if (acm->pending)
		acm_notify_serial_state(acm);
}

/* connect == the TTY link is open */

static void acm_connect(struct gserial *port)
{
	struct f_acm		*acm = port_to_acm(port);

	acm->serial_state |= ACM_CTRL_DSR | ACM_CTRL_DCD;
	acm_notify_serial_state(acm);
}

static void acm_disconnect(struct gserial *port)
{
	struct f_acm		*acm = port_to_acm(port);

	acm->serial_state &= ~(ACM_CTRL_DSR | ACM_CTRL_DCD);
}

static int acm_send_break(struct gserial *port, int duration)
{
	struct f_acm		*acm = port_to_acm(port);
	u16			state;

	state = acm->serial_state;
	state &= ~ACM_CTRL_BRK;
	if (duration)
		state |= ACM_CTRL_BRK;

	acm->serial_state = state;
	return acm_notify_serial_state(acm);
}

/*-------------------------------------------------------------------------*/

/* ACM function driver setup/binding */
static int acm_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_acm		*acm = func_to_acm(f);
	struct usb_string	*us;
	int			status;
	struct usb_ep		*ep;

	/* maybe allocate device-global string IDs, and patch descriptors */
	us = usb_gstrings_attach(cdev, acm_strings, ARRAY_SIZE(acm_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	acm_control_interface_desc.iInterface = us[ACM_CTRL_IDX].id;
	acm_data_interface_desc.iInterface = us[ACM_DATA_IDX].id;
	acm_iad_descriptor.iFunction = us[ACM_IAD_IDX].id;

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	acm->ctrl_id = status;
	acm_iad_descriptor.bFirstInterface = status;

	acm_control_interface_desc.bInterfaceNumber = status;
	acm_union_desc .bMasterInterface0 = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	acm->data_id = status;

	acm_data_interface_desc.bInterfaceNumber = status;
	acm_union_desc.bSlaveInterface0 = status;
	acm_call_mgmt_descriptor.bDataInterface = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &acm_fs_in_desc);
	if (!ep)
		goto fail;
	acm->port.in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &acm_fs_out_desc);
	if (!ep)
		goto fail;
	acm->port.out = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &acm_fs_notify_desc);
	if (!ep)
		goto fail;
	acm->notify = ep;

	/* allocate notification */
	acm->notify_req = gs_alloc_req(ep,
			sizeof(struct usb_cdc_notification) + 2);
	if (!acm->notify_req)
		goto fail;

	acm->notify_req->complete = acm_cdc_notify_complete;
	acm->notify_req->context = acm;

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	acm_hs_in_desc.bEndpointAddress = acm_fs_in_desc.bEndpointAddress;
	acm_hs_out_desc.bEndpointAddress = acm_fs_out_desc.bEndpointAddress;
	acm_hs_notify_desc.bEndpointAddress =
		acm_fs_notify_desc.bEndpointAddress;

	status = usb_assign_descriptors(f, acm_fs_function, acm_hs_function);
	if (status)
		goto fail;

	printf("acm: %s speed IN/%s OUT/%s NOTIFY/%s\n",
		gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
		acm->port.in->name, acm->port.out->name,
		acm->notify->name);
	return 0;

fail:
	if (acm->notify_req)
		gs_free_req(acm->notify, acm->notify_req);

	printf("%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void acm_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_acm		*acm = func_to_acm(f);

	acm_string_defs[0].id = 0;
	usb_free_all_descriptors(f);
	if (acm->notify_req)
		gs_free_req(acm->notify, acm->notify_req);
}

/*-------------------------------------------------------------------------*/

void acm_free_func(struct usb_function *f)
{
	struct f_acm		*acm = func_to_acm(f);
	free(acm);
}

struct usb_function *acm_alloc_func(struct usb_cdc_line_coding *coding)
{
	struct f_acm *acm;

	assert(coding);

	acm = malloc(sizeof(*acm));
	if (!acm)
		return ERR_PTR(-ENOMEM);

	memset(acm, 0, sizeof(*acm));

	spin_lock_init_recursive(&acm->lock);

	acm->port.connect = acm_connect;
	acm->port.disconnect = acm_disconnect;
	acm->port.send_break = acm_send_break;

	acm->port.func.name = "acm";
	acm->port.func.strings = acm_strings;
	/* descriptors are per-instance copies */
	acm->port.func.bind = acm_bind;
	acm->port.func.set_alt = acm_set_alt;
	acm->port.func.setup = acm_setup;
	acm->port.func.disable = acm_disable;
	acm->port.func.unbind = acm_unbind;
	acm->port.func.req_match = acm_req_match;

	acm->port_line_coding = *coding;

	return &acm->port.func;
}
