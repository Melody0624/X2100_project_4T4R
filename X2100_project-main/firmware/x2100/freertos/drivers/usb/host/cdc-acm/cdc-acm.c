#include <os.h>
#include <usb/host_cdc_acm.h>
#include <usb/cdc.h>

#include "../usb.h"


/*
 * Requests.
 */

#define USB_RT_ACM		(USB_TYPE_CLASS | USB_RECIP_INTERFACE)

/*
 * Internal driver structures.
 */

struct acm {
	struct usb_device *dev;				/* the corresponding usb device */
	struct usb_interface *control;			/* control interface */
	struct usb_interface *data;			/* data interface */

	u8 minor;				/* acm minor number */
	bool opened;				/* someone has this acm's device open */
	bool exiting;
	unsigned int used;

	u8 *ctrl_buffer;	/* buffers of urbs */
	unsigned int ctrlsize;			/* buffer sizes for freeing */
	dma_addr_t ctrl_dma;	/* dma handles of buffers */
	struct urb *ctrlurb;	/* urbs */

	unsigned int in, out;				/* i/o pipes */
	struct usb_cdc_line_coding line;		/* bits, stop, parity */
	cdc_acm_notify_callback_t notify_callback;
};

/* devices aren't required to support these requests.
 * the cdc acm descriptor tells whether they do...
 */
#define acm_set_control(acm, control, timeout) \
	acm_ctrl_msg(acm, USB_CDC_REQ_SET_CONTROL_LINE_STATE, control, NULL, 0, timeout)
#define acm_set_line(acm, line, timeout) \
	acm_ctrl_msg(acm, USB_CDC_REQ_SET_LINE_CODING, 0, line, sizeof *(line), timeout)
#define acm_send_break(acm, ms, timeout) \
	acm_ctrl_msg(acm, USB_CDC_REQ_SEND_BREAK, ms, NULL, 0, timeout)

static struct usb_driver acm_driver;

static u32 devices_bit;
static cdc_acm_device_callback_t device_callback;

static struct acm *acm_table[ACM_MINORS];
static struct mutex device_lock[ACM_MINORS];
static thread_cond_t free_cond[ACM_MINORS];

#define ACM_READY(acm)	(acm && acm->dev && acm->opened && !acm->exiting)

void usb_host_cdc_acm_register_callback(cdc_acm_device_callback_t callback)
{
	device_callback = callback;
}

u32 usb_host_cdc_acm_get_state(void)
{
	return devices_bit;
}

static void update_devices_bit(void)
{
	int i;
	struct acm *acm;
	u32 dev_bit = 0;
	cdc_acm_device_callback_t callback = device_callback;

	for (i = 0; i < ACM_MINORS; i++) {
		acm = acm_table[i];
		if (acm && acm->dev)
			dev_bit |= (1 << i);
	}

	devices_bit = dev_bit;
	if (callback)
		callback(dev_bit);
}

/*
 * Functions for ACM control messages.
 */
static int acm_ctrl_msg(struct acm *acm, int request, int value, void *buf, int len, int timeout)
{
	int retval = usb_control_msg(acm->dev, usb_sndctrlpipe(acm->dev, 0),
		request, USB_RT_ACM, value,
		acm->control->altsetting[0].desc.bInterfaceNumber,
		buf, len, timeout);
	return retval < 0 ? retval : 0;
}

int usb_host_cdc_acm_open(u8 id, cdc_acm_notify_callback_t callback)
{
	struct acm *acm;
	int ret = 0;

	if (id >= ACM_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!acm || !acm->dev) {
		ret = -ENODEV;
		goto err_out;
	}

	if (acm->opened) {
		ret = -EBUSY;
		goto err_out;
	}

	acm->ctrlurb->dev = acm->dev;
	if (usb_submit_urb(acm->ctrlurb)) {
		ret = -EIO;
		goto err_out;
	}

	acm->opened = true;
	acm->notify_callback = callback;

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

void usb_host_cdc_acm_close(u8 id)
{
	struct acm *acm;

	if (id >= ACM_MINORS)
		return;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!acm || !acm->opened || acm->exiting)
		goto done;

	acm->exiting = true;

	while (acm->used)
		thread_cond_wait(&free_cond[id], &device_lock[id]);

	acm->exiting = false;
	acm->opened = false;

	if (acm->dev) {
		usb_kill_urb(acm->ctrlurb);
	} else {
		assert(acm->minor == id);
		acm_table[acm->minor] = NULL;
		usb_free_urb(acm->ctrlurb);
		free(acm);
	}

done:
	mutex_unlock(&device_lock[id]);
}

int usb_host_cdc_acm_set_line_coding(u8 id, struct cdc_acm_line_coding *param, int timeout)
{
	struct acm *acm;
	int ret = 0;

	if (id >= ACM_MINORS)
		return -ENODEV;

	if (!param)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!ACM_READY(acm)) {
		ret = -ENODEV;
		goto err_out;
	}

	acm->used++;
	mutex_unlock(&device_lock[id]);

	acm->line.dwDTERate = param->dwDTERate;
	acm->line.bCharFormat = param->bCharFormat;
	acm->line.bParityType = param->bParityType;
	acm->line.bDataBits = param->bDataBits;

	ret = acm_set_line(acm, &acm->line, timeout);

	mutex_lock(&device_lock[id]);
	acm->used--;
	if (acm->exiting && !acm->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_cdc_acm_set_control(u8 id, u16 ctrlout, int timeout)
{
	struct acm *acm;
	int ret = 0;

	if (id >= ACM_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!ACM_READY(acm)) {
		ret = -ENODEV;
		goto err_out;
	}

	acm->used++;
	mutex_unlock(&device_lock[id]);

	ret = acm_set_control(acm, ctrlout, timeout);

	mutex_lock(&device_lock[id]);
	acm->used--;
	if (acm->exiting && !acm->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_cdc_acm_send_break(u8 id, int state, int timeout)
{
	struct acm *acm;
	int ret = 0;

	if (id >= ACM_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!ACM_READY(acm)) {
		ret = -ENODEV;
		goto err_out;
	}

	acm->used++;
	mutex_unlock(&device_lock[id]);

	ret = acm_send_break(acm, state ? 0xffff : 0, timeout);

	mutex_lock(&device_lock[id]);
	acm->used--;
	if (acm->exiting && !acm->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_cdc_acm_read(u8 id, void *data, int len, int timeout)
{
	struct acm *acm;
	int actual_len = 0;
	int ret = 0;

	if (id >= ACM_MINORS)
		return -ENODEV;

	if (!data || !len)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!ACM_READY(acm)) {
		ret = -ENODEV;
		goto err_out;
	}

	acm->used++;
	mutex_unlock(&device_lock[id]);

	ret = usb_bulk_msg(acm->dev, acm->in, data, len, &actual_len, timeout);

	mutex_lock(&device_lock[id]);
	acm->used--;
	if (acm->exiting && !acm->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret < 0 ? ret : actual_len;
}

int usb_host_cdc_acm_write(u8 id, void *data, int len, int timeout)
{
	struct acm *acm;
	int actual_len = 0;
	int ret = 0;

	if (id >= ACM_MINORS)
		return -ENODEV;

	if (!data || !len)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	acm = acm_table[id];
	if (!ACM_READY(acm)) {
		ret = -ENODEV;
		goto err_out;
	}

	acm->used++;
	mutex_unlock(&device_lock[id]);

	ret = usb_bulk_msg(acm->dev, acm->out, data, len, &actual_len, timeout);

	mutex_lock(&device_lock[id]);
	acm->used--;
	if (acm->exiting && !acm->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret < 0 ? ret : actual_len;
}

/*
 * Interrupt handlers for various ACM device responses
 */

/* control interface reports status changes with "interrupt" transfers */
static void acm_ctrl_irq(struct urb *urb)
{
	struct acm *acm = urb->context;
	int ret;

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
			goto exit;
	}

	if (!ACM_READY(acm))
		return;

	if (acm->notify_callback)
		acm->notify_callback(acm->minor, urb->transfer_buffer, urb->actual_length);
exit:
	ret = usb_submit_urb (urb);
	if (ret)
		printf("%s - usb_submit_urb failed with result %d\n", __func__, ret);
}

static int acm_probe (struct usb_interface *intf, const struct usb_device_id *id)
{
	u8 *buf;
	int minor;
	int ctrlsize;
	struct acm *acm;
	struct usb_interface *control_interface;
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epctrl;
	struct usb_endpoint_descriptor *epread;
	struct usb_endpoint_descriptor *epwrite;
	struct usb_cdc_union_desc *union_header = NULL;
	unsigned char *buffer = intf->altsetting->extra;
	int buflen = intf->altsetting->extralen;
	u8 call_management_function = 0;
	int call_interface_num = -1;
	int data_interface_num = 0;
	struct usb_device *usb_dev = interface_to_usbdev(intf);

	/* normal probing*/
	if (!buffer) {
		printf("%s: Wierd descriptor references\n", __func__);
		return -EINVAL;
	}

	if (!buflen) {
		if (intf->cur_altsetting->endpoint &&
				intf->cur_altsetting->endpoint->extralen &&
				intf->cur_altsetting->endpoint->extra) {
			buflen = intf->cur_altsetting->endpoint->extralen;
			buffer = intf->cur_altsetting->endpoint->extra;
		} else {
			printf("%s: Zero length descriptor references\n", __func__);
			return -EINVAL;
		}
	}

	while (buflen > 0) {
		if (buffer [1] != USB_DT_CS_INTERFACE) {
			printf("%s: skipping garbage\n", __func__);
			goto next_desc;
		}

		switch (buffer [2]) {
			case USB_CDC_UNION_TYPE: /* we've found it */
				if (union_header) {
					printf("%s: More than one union descriptor, skipping ...\n", __func__);
					goto next_desc;
				}
				union_header = (struct usb_cdc_union_desc *)buffer;
				break;
			case USB_CDC_COUNTRY_TYPE: /* maybe somehow export */
				break; /* for now we ignore it */
			case USB_CDC_HEADER_TYPE: /* maybe check version */ 
				break; /* for now we ignore it */ 
			case USB_CDC_ACM_TYPE:
				break; /* for now we ignore it */ 
			case USB_CDC_CALL_MANAGEMENT_TYPE:
				call_management_function = buffer[3];
				call_interface_num = buffer[4];
				if ((call_management_function & 3) != 3)
					printf("%s: This device cannot do calls on its own. It is no modem.\n", __func__);
				break;
				
			default:
				printf("%s: Ignoring extra header, type %d, length %d\n", __func__, buffer[2], buffer[0]);
				break;
			}
next_desc:
		buflen -= buffer[0];
		buffer += buffer[0];
	}

	if (!union_header) {
		if (call_interface_num > 0) {
			printf("%s: No union descriptor, using call management descriptor\n", __func__);
			data_interface = usb_ifnum_to_if(usb_dev, (data_interface_num = call_interface_num));
			control_interface = intf;
		} else {
			printf("%s: No union descriptor, giving up\n", __func__);
			return -ENODEV;
		}
	} else {
		control_interface = usb_ifnum_to_if(usb_dev, union_header->bMasterInterface0);
		data_interface = usb_ifnum_to_if(usb_dev, (data_interface_num = union_header->bSlaveInterface0));
		if (!control_interface || !data_interface) {
			printf("%s: no interfaces\n", __func__);
			return -ENODEV;
		}
	}
	
	if (data_interface_num != call_interface_num)
		printf("%s: Seperate call control interface. That is not fully supported.\n", __func__);

	/*workaround for switched interfaces */
	if (data_interface->cur_altsetting->desc.bInterfaceClass != USB_CLASS_CDC_DATA) {
		if (control_interface->cur_altsetting->desc.bInterfaceClass == USB_CLASS_CDC_DATA) {
			struct usb_interface *t;
			printf("%s: Your device has switched interfaces.\n", __func__);

			t = control_interface;
			control_interface = data_interface;
			data_interface = t;
		} else {
			return -EINVAL;
		}
	}

	if (usb_interface_claimed(data_interface)) { /* valid in this context */
		printf("%s: The data interface isn't available\n", __func__);
		return -EBUSY;
	}

	if (data_interface->cur_altsetting->desc.bNumEndpoints < 2 ||
	    control_interface->cur_altsetting->desc.bNumEndpoints == 0)
		return -EINVAL;

	epctrl = &control_interface->cur_altsetting->endpoint[0].desc;
	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;

	/* workaround for switched endpoints */
	if (!usb_endpoint_dir_in(epread)) {
		/* descriptors are swapped */
		struct usb_endpoint_descriptor *t;
		printf("%s: The data interface has switched endpoints\n", __func__);

		t = epread;
		epread = epwrite;
		epwrite = t;
	}

	for (minor = 0; minor < ACM_MINORS && acm_table[minor]; minor++);

	if (minor == ACM_MINORS) {
		printf("%s: no more free acm devices\n", __func__);
		return -ENODEV;
	}

	if (!(acm = malloc(sizeof(struct acm)))) {
		printf("%s: out of memory (acm malloc)\n", __func__);
		goto alloc_fail;
	}
	memset(acm, 0, sizeof(struct acm));

	ctrlsize = usb_endpoint_maxp(epctrl);
	acm->control = control_interface;
	acm->data = data_interface;
	acm->minor = minor;
	acm->dev = usb_dev;
	acm->ctrlsize = ctrlsize;

	if (usb_endpoint_xfer_int(epread))
		acm->in = usb_rcvintpipe(usb_dev, epread->bEndpointAddress);
	else
		acm->in = usb_rcvbulkpipe(usb_dev, epread->bEndpointAddress);

	if (usb_endpoint_xfer_int(epwrite))
		acm->out = usb_sndintpipe(usb_dev, epwrite->bEndpointAddress);
	else
		acm->out = usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress);

	buf = usb_alloc_coherent(usb_dev, ctrlsize, &acm->ctrl_dma);
	if (!buf) {
		printf("%s: out of memory (ctrl buffer alloc)\n", __func__);
		goto alloc_fail2;
	}
	acm->ctrl_buffer = buf;

	acm->ctrlurb = usb_alloc_urb(0);
	if (!acm->ctrlurb) {
		printf("%s: out of memory (ctrlurb kmalloc)\n", __func__);
		goto alloc_fail3;
	}

	usb_fill_int_urb(acm->ctrlurb, usb_dev, usb_rcvintpipe(usb_dev, epctrl->bEndpointAddress),
			 acm->ctrl_buffer, ctrlsize, acm_ctrl_irq, acm, epctrl->bInterval ? epctrl->bInterval : 16);
	acm->ctrlurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	acm->ctrlurb->transfer_dma = acm->ctrl_dma;

	usb_set_intfdata (intf, acm);

	usb_driver_claim_interface(&acm_driver, data_interface, acm);

	acm_table[minor] = acm;

	printf("NEW USB ACM device [%d]\n", minor);

	update_devices_bit();

	return 0;

alloc_fail3:
	usb_free_coherent(usb_dev, ctrlsize, acm->ctrl_buffer);
alloc_fail2:
	free(acm);
alloc_fail:
	return -ENOMEM;
}

static void acm_disconnect(struct usb_interface *intf)
{
	u32 id;
	struct acm *acm = usb_get_intfdata (intf);

	if (!acm || !acm->dev)
		return;

	id = acm->minor;
	assert(id < ACM_MINORS);

	mutex_lock(&device_lock[id]);
	acm->dev = NULL;
	usb_set_intfdata(acm->control, NULL);
	usb_set_intfdata(acm->data, NULL);

	usb_kill_urb(acm->ctrlurb);
	usb_free_coherent(acm->dev, acm->ctrlsize, acm->ctrl_buffer);

	usb_driver_release_interface(&acm_driver, intf == acm->control ?
				acm->data : acm->control);

	if (!acm->opened) {
		acm_table[acm->minor] = NULL;
		usb_free_urb(acm->ctrlurb);
		free(acm);
	}

	mutex_unlock(&device_lock[id]);

	printf("USB ACM device disconnect [%d]\n", id);

	update_devices_bit();
}

/*
 * USB driver structure.
 */

static struct usb_device_id acm_ids[] = {
	/* control interfaces with various AT-command sets */
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_V25TER) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_PCCA101) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_PCCA101_WAKE) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_GSM) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_3G	) },
	{ USB_INTERFACE_INFO(USB_CLASS_COMM, USB_CDC_SUBCLASS_ACM,
		USB_CDC_ACM_PROTO_AT_CDMA) },

	/* NOTE:  COMM/ACM/0xff is likely MSFT RNDIS ... NOT a modem!! */
	{ }
};

static struct usb_driver acm_driver = {
	.name =		"cdc_acm",
	.probe =	acm_probe,
	.disconnect =	acm_disconnect,
	.id_table =	acm_ids,
};

void usb_cdc_acm_driver_register(void)
{
	int i;

	for (i = 0; i < ACM_MINORS; i++) {
		mutex_init(&device_lock[i]);
		thread_cond_init(&free_cond[i]);
	}

	usb_register_driver(&acm_driver);
}

void usb_cdc_acm_driver_deregister(void)
{
	usb_deregister(&acm_driver);
}