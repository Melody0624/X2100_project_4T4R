#include <usb/host_ch341.h>
#include <le_byteshift.h>

#include "../usb.h"

#define DEFAULT_TIMEOUT	  2000

#define CMD_R  0x95
#define CMD_W  0x9A
#define CMD_C1 0xA1
#define CMD_C2 0xA4
#define CMD_C3 0x5F

#define CH341_L_ER 0x80
#define CH341_L_ET 0x40
#define CH341_L_PS 0x38
#define CH341_L_PM 0x28
#define CH341_L_PE 0x18
#define CH341_L_PO 0x08
#define CH341_L_SB 0x04
#define CH341_L_D8 0x03
#define CH341_L_D7 0x02
#define CH341_L_D6 0x01
#define CH341_L_D5 0x00

#define CH341_RB 0x05
#define CH341_RL 0x18
#define CH341_NB 0x01

#define HARDFLOW		0x01

struct ch341 {
	struct usb_device *dev;				/* the corresponding usb device */
	struct usb_interface *data;			/* data interface */

	u8 minor;				/* ch341 minor number */
	bool opened;				/* someone has this ch341's device open */
	bool exiting;
	unsigned int used;

	u8 *ctrl_buffer;	/* buffers of urbs */
	unsigned int ctrlsize;			/* buffer sizes for freeing */
	dma_addr_t ctrl_dma;	/* dma handles of buffers */
	struct urb *ctrlurb;	/* urbs */

	bool support_hardflow;
	bool hardflow_enable;

	unsigned int in, out;				/* i/o pipes */
	ch341_notify_callback_t notify_callback;
};

static struct usb_driver ch341_driver;

static u32 devices_bit;
static ch341_device_callback_t device_callback;

static struct ch341 *ch341_table[CH341_MINORS];
static struct mutex device_lock[CH341_MINORS];
static thread_cond_t free_cond[CH341_MINORS];

#define CH341_READY(ch341)	(ch341 && ch341->dev && ch341->opened && !ch341->exiting)

void usb_host_ch341_register_callback(ch341_device_callback_t callback)
{
	device_callback = callback;
}

u32 usb_host_ch341_get_state(void)
{
	return devices_bit;
}

static void update_devices_bit(void)
{
	int i;
	struct ch341 *ch341;
	u32 dev_bit = 0;
	ch341_device_callback_t callback = device_callback;

	for (i = 0; i < CH341_MINORS; i++) {
		ch341 = ch341_table[i];
		if (ch341 && ch341->dev)
			dev_bit |= (1 << i);
	}

	devices_bit = dev_bit;
	if (callback)
		callback(dev_bit);
}

/*
 * Functions for CH341 control messages.
 */
static int ch341_control_out(struct ch341 *ch341, u8 request, u16 value, u16 index)
{
	int retval = usb_control_msg(ch341->dev, usb_sndctrlpipe(ch341->dev, 0), request,
				 USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT, value, index, NULL, 0,
				 DEFAULT_TIMEOUT);

	return retval < 0 ? retval : 0;
}

static int ch341_control_in(struct ch341 *ch341, u8 request, u16 value, u16 index, void *buf, unsigned bufsize)
{
	int retval = usb_control_msg(ch341->dev, usb_rcvctrlpipe(ch341->dev, 0), request,
				 USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN, value, index, buf, bufsize,
				 DEFAULT_TIMEOUT);

	return retval;
}

static inline int ch341_set_control(struct ch341 *ch341, int control)
{
	u16 value = 0;

	value |= (u8)~control;

	return ch341_control_out(ch341, CMD_C2, value, 0x0000);
}

static int ch341_get_status(struct ch341 *ch341)
{
	int ret;
	int ctrlin;
	char buffer[2];

	ret = ch341_control_in(ch341, CMD_R, 0x0706, 0, buffer, 2);
	if (ret < 0)
		return ret;

	if (ret > 0)
		ctrlin = (~(*buffer)) & CH341_CTI_ST;
	else
		ret = -EPROTO;

	return ret < 0 ? ret : ctrlin;
}

static int ch341_configure(struct ch341 *ch341)
{
	int ret;
	int ctrlin;
	char buffer[2];

	ret = ch341_control_in(ch341, CMD_C3, 0, 0, buffer, 2);
	if (ret < 0)
		return ret;

	ret = ch341_control_out(ch341, CMD_C1, 0, 0);
	if (ret < 0)
		return ret;

	ret = ch341_control_out(ch341, CMD_W, 0x1312, 0xd982);
	if (ret < 0)
		return ret;

	ret = ch341_control_out(ch341, CMD_W, 0x0f2c, 0x0007);
	if (ret < 0)
		return ret;

	ret = ch341_control_in(ch341, CMD_R, 0x2518, 0, buffer, 2);
	if (ret < 0)
		return ret;

	ctrlin = ch341_get_status(ch341);
	if (ctrlin < 0)
		return ctrlin;

	ret = ch341_control_out(ch341, CMD_W, 0x2727, 0);
	if (ret < 0)
		return ret;

	return ctrlin;
}

static void ch341_ctrl_irq(struct urb *urb)
{
	struct ch341 *ch341 = urb->context;
	int ret;

	switch (urb->status) {
		case 0:
			/* success */
			break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
		case -EPROTO:
		case -EPIPE:
			return;
		default:
			printf("Error: %s urb->status %d\n", __func__, urb->status);
			goto exit;
	}

	if (!CH341_READY(ch341))
		return;

	if (ch341->notify_callback)
		ch341->notify_callback(ch341->minor, urb->transfer_buffer, urb->actual_length);
exit:
	ret = usb_submit_urb (urb);
	if (ret)
		printf("%s - usb_submit_urb failed with result %d\n", __func__, ret);
}

int usb_host_ch341_set_control(u8 id, u16 ctrlout)
{
	struct ch341 *ch341;
	int ret = 0;

	if (id >= CH341_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!CH341_READY(ch341)) {
		ret = -ENODEV;
		goto err_out;
	}

	ch341->used++;
	mutex_unlock(&device_lock[id]);

	if (ch341->hardflow_enable)
		ctrlout |= CH341_CTO_R;

	ret = ch341_set_control(ch341, ctrlout);

	mutex_lock(&device_lock[id]);
	ch341->used--;
	if (ch341->exiting && !ch341->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_ch341_open(u8 id, ch341_notify_callback_t callback)
{
	int ret;
	int ctrlin;
	struct ch341 *ch341;

	if (id >= CH341_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!ch341 || !ch341->dev) {
		ret = -ENODEV;
		goto err_out;
	}

	if (ch341->opened) {
		ret = -EBUSY;
		goto err_out;
	}

	ctrlin = ch341_configure(ch341);
	if (ctrlin < 0) {
		ret = ctrlin;
		goto err_out;
	}

	ch341->ctrlurb->dev = ch341->dev;
	if (usb_submit_urb(ch341->ctrlurb)) {
		ret = -EIO;
		goto err_out;
	}

	ch341->opened = true;
	ch341->notify_callback = callback;

	mutex_unlock(&device_lock[id]);
	return ctrlin;

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

void usb_host_ch341_close(u8 id)
{
	struct ch341 *ch341;

	if (id >= CH341_MINORS)
		return;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!ch341 || !ch341->opened || ch341->exiting)
		goto done;

	ch341->exiting = true;

	while (ch341->used)
		thread_cond_wait(&free_cond[id], &device_lock[id]);

	ch341->exiting = false;
	ch341->opened = false;

	if (ch341->dev) {
		usb_kill_urb(ch341->ctrlurb);
	} else {
		assert(ch341->minor == id);
		ch341_table[ch341->minor] = NULL;
		usb_free_urb(ch341->ctrlurb);
		free(ch341);
	}

done:
	mutex_unlock(&device_lock[id]);
}

int usb_host_ch341_send_break(u8 id, int state)
{
	int ret;
	u8 regbuf[2];
	u16 reg_contents;
	struct ch341 *ch341;
	const u16 regval = ((u16)CH341_RL << 8) | CH341_RB;

	if (id >= CH341_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!CH341_READY(ch341)) {
		ret = -ENODEV;
		goto err_out;
	}

	ch341->used++;
	mutex_unlock(&device_lock[id]);

	ret = ch341_control_in(ch341, CMD_R, regval, 0, regbuf, 2);
	if (ret == 2) {
		if (state != 0) {
			regbuf[0] &= ~CH341_NB;
			regbuf[1] &= ~CH341_L_ET;
		} else {
			regbuf[0] |= CH341_NB;
			regbuf[1] |= CH341_L_ET;
		}
		reg_contents = get_unaligned_le16(regbuf);
	
		ret = ch341_control_out(ch341, CMD_W, regval, reg_contents);
	} else {
		ret = -EIO;
	}

	mutex_lock(&device_lock[id]);
	ch341->used--;
	if (ch341->exiting && !ch341->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

static int ch341_get(unsigned int baval, unsigned char *factor, unsigned char *divisor)
{
	unsigned char a;
	unsigned char b;
	unsigned long c;

	switch (baval) {
	case 921600:
		a = 0xf3;
		b = 7;
		break;
	case 307200:
		a = 0xd9;
		b = 7;
		break;
	default:
		if (baval > 6000000 / 255) {
			b = 3;
			c = 6000000;
		} else if (baval > 750000 / 255) {
			b = 2;
			c = 750000;
		} else if (baval > 93750 / 255) {
			b = 1;
			c = 93750;
		} else {
			b = 0;
			c = 11719;
		}
		a = (unsigned char)(c / baval);
		if (a == 0 || a == 0xFF)
			return -EINVAL;
		if ((c / a - baval) > (baval - c / (a + 1)))
			a++;
		a = 256 - a;
		break;
	}
	*factor = a;
	*divisor = b;

	return 0;
}

int usb_host_ch341_set_line_coding(u8 id, struct ch341_line_coding *param)
{
	struct ch341 *ch341;

	unsigned char divisor = 0;
	unsigned char reg_count = 0;
	unsigned char factor = 0;
	unsigned char reg_value = 0;
	unsigned short value = 0;
	unsigned short index = 0;
	int ret = 0;

	if (id >= CH341_MINORS)
		return -ENODEV;

	if (!param)
		return -EINVAL;

	ch341_get(param->dwDTERate, &factor, &divisor);
	if (param->bCharFormat == 2)
		reg_value |= CH341_L_SB;

	switch (param->bParityType) {
		case USB_CH341_ODD_PARITY:
			reg_value |= CH341_L_PO;
			break;
		case USB_CH341_EVEN_PARITY:
			reg_value |= CH341_L_PE;
			break;
		case USB_CH341_MARK_PARITY:
			reg_value |= CH341_L_PM;
			break;
		case USB_CH341_SPACE_PARITY:
			reg_value |= CH341_L_PS;
			break;
		default:
			break;
	}

	switch (param->bDataBits) {
		case 5:
			reg_value |= CH341_L_D5;
			break;
		case 6:
			reg_value |= CH341_L_D6;
			break;
		case 7:
			reg_value |= CH341_L_D7;
			break;
		case 8:
			reg_value |= CH341_L_D8;
			break;
		default:
			printf("%s: not Supported bDataBits %d\n", __func__, param->bDataBits);
			return -EINVAL;
	}

	reg_value |= 0xc0;
	reg_count |= 0x9c;

	value |= reg_count;
	value |= (unsigned short)reg_value << 8;
	index |= 0x80 | divisor;
	index |= (unsigned short)factor << 8;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!CH341_READY(ch341)) {
		ret = -ENODEV;
		goto err_out;
	}

	ch341->used++;
	mutex_unlock(&device_lock[id]);

	ret = ch341_control_out(ch341, CMD_C1, value, index);
	if (!ret && ch341->support_hardflow) {
		ch341->hardflow_enable = param->hardflow;
		if (param->hardflow)
			ret = ch341_control_out(ch341, CMD_W, 0x2727, 0x0101);
		else
			ret = ch341_control_out(ch341, CMD_W, 0x2727, 0x0000);
	}

	mutex_lock(&device_lock[id]);
	ch341->used--;
	if (ch341->exiting && !ch341->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_ch341_read(u8 id, void *data, int len, int timeout)
{
	struct ch341 *ch341;
	int actual_len = 0;
	int ret = 0;

	if (id >= CH341_MINORS)
		return -ENODEV;

	if (!data || !len)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!CH341_READY(ch341)) {
		ret = -ENODEV;
		goto err_out;
	}

	ch341->used++;
	mutex_unlock(&device_lock[id]);

	ret = usb_bulk_msg(ch341->dev, ch341->in, data, len, &actual_len, timeout);

	mutex_lock(&device_lock[id]);
	ch341->used--;
	if (ch341->exiting && !ch341->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret < 0 ? ret : actual_len;
}

int usb_host_ch341_write(u8 id, void *data, int len, int timeout)
{
	struct ch341 *ch341;
	int actual_len = 0;
	int ret = 0;

	if (id >= CH341_MINORS)
		return -ENODEV;

	if (!data || !len)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	ch341 = ch341_table[id];
	if (!CH341_READY(ch341)) {
		ret = -ENODEV;
		goto err_out;
	}

	ch341->used++;
	mutex_unlock(&device_lock[id]);

	ret = usb_bulk_msg(ch341->dev, ch341->out, data, len, &actual_len, timeout);

	mutex_lock(&device_lock[id]);
	ch341->used--;
	if (ch341->exiting && !ch341->used)
		thread_cond_signal(&free_cond[id]);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret < 0 ? ret : actual_len;
}

static int ch341_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	u8 *buf;
	int minor;
	int ctrlsize;
	struct ch341 *ch341;
	struct usb_interface *data_interface;
	struct usb_endpoint_descriptor *epctrl = NULL;
	struct usb_endpoint_descriptor *epread = NULL;
	struct usb_endpoint_descriptor *epwrite = NULL;
	struct usb_device *usb_dev = interface_to_usbdev(intf);

	data_interface = usb_ifnum_to_if(usb_dev, 0);

	if (intf != data_interface)
		return -ENODEV;

	if (data_interface->cur_altsetting->desc.bNumEndpoints < 2)
		return -EINVAL;

	epread = &data_interface->cur_altsetting->endpoint[0].desc;
	epwrite = &data_interface->cur_altsetting->endpoint[1].desc;
	epctrl = &data_interface->cur_altsetting->endpoint[2].desc;

	/* workaround for switched endpoints */
	if (!usb_endpoint_dir_in(epread)) {
		/* descriptors are swapped */
		struct usb_endpoint_descriptor *t;
		printf("%s: The data interface has switched endpoints\n", __func__);

		t = epread;
		epread = epwrite;
		epwrite = t;
	}

	for (minor = 0; minor < CH341_MINORS && ch341_table[minor]; minor++);

	if (minor == CH341_MINORS) {
		printf("%s: no more free ch341 devices\n", __func__);
		return -ENODEV;
	}

	if (!(ch341 = malloc(sizeof(struct ch341)))) {
		printf("%s: out of memory (ch341 malloc)\n", __func__);
		goto alloc_fail;
	}
	memset(ch341, 0, sizeof(struct ch341));

	if (id->driver_info & HARDFLOW)
		ch341->support_hardflow = true;

	ctrlsize = usb_endpoint_maxp(epctrl);
	ch341->data = data_interface;
	ch341->minor = minor;
	ch341->dev = usb_dev;
	ch341->ctrlsize = ctrlsize;

	if (usb_endpoint_xfer_int(epread))
		ch341->in = usb_rcvintpipe(usb_dev, epread->bEndpointAddress);
	else
		ch341->in = usb_rcvbulkpipe(usb_dev, epread->bEndpointAddress);

	if (usb_endpoint_xfer_int(epwrite))
		ch341->out = usb_sndintpipe(usb_dev, epwrite->bEndpointAddress);
	else
		ch341->out = usb_sndbulkpipe(usb_dev, epwrite->bEndpointAddress);

	buf = usb_alloc_coherent(usb_dev, ctrlsize, &ch341->ctrl_dma);
	if (!buf) {
		printf("%s: out of memory (ctrl buffer alloc)\n", __func__);
		goto alloc_fail2;
	}
	ch341->ctrl_buffer = buf;

	ch341->ctrlurb = usb_alloc_urb(0);
	if (!ch341->ctrlurb) {
		printf("%s: out of memory (ctrlurb kmalloc)\n", __func__);
		goto alloc_fail3;
	}

	usb_fill_int_urb(ch341->ctrlurb, usb_dev, usb_rcvintpipe(usb_dev, epctrl->bEndpointAddress),
			 ch341->ctrl_buffer, ctrlsize, ch341_ctrl_irq, ch341, epctrl->bInterval ? epctrl->bInterval : 16);
	ch341->ctrlurb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	ch341->ctrlurb->transfer_dma = ch341->ctrl_dma;

	usb_set_intfdata (intf, ch341);

	usb_driver_claim_interface(&ch341_driver, data_interface, ch341);

	ch341_table[minor] = ch341;

	printf("NEW USB CH341 device [%d]\n", minor);

	update_devices_bit();

	return 0;

alloc_fail3:
	usb_free_coherent(usb_dev, ctrlsize, ch341->ctrl_buffer);
alloc_fail2:
	free(ch341);
alloc_fail:
	return -ENOMEM;
}

static void ch341_disconnect(struct usb_interface *intf)
{
	u32 id;
	struct ch341 *ch341 = usb_get_intfdata (intf);

	if (!ch341 || !ch341->dev)
		return;

	id = ch341->minor;
	assert(id < CH341_MINORS);

	mutex_lock(&device_lock[id]);
	ch341->dev = NULL;
	usb_set_intfdata(ch341->data, NULL);

	usb_kill_urb(ch341->ctrlurb);
	usb_free_coherent(ch341->dev, ch341->ctrlsize, ch341->ctrl_buffer);

	usb_driver_release_interface(&ch341_driver, ch341->data);

	if (!ch341->opened) {
		ch341_table[ch341->minor] = NULL;
		usb_free_urb(ch341->ctrlurb);
		free(ch341);
	}

	mutex_unlock(&device_lock[id]);

	printf("USB CH341 device disconnect [%d]\n", id);

	update_devices_bit();
}

/*
 * USB driver structure.
 */
static const struct usb_device_id ch341_ids[] = { { USB_DEVICE(0x1a86, 0x7523) }, /* ch340 chip */
						  { USB_DEVICE(0x1a86, 0x7522) }, /* ch340k chip */
						  { USB_DEVICE(0x1a86, 0x5523), .driver_info = HARDFLOW}, /* ch341 chip */
						  { USB_DEVICE(0x1a86, 0xe523) }, /* ch330 chip */
						  { USB_DEVICE(0x4348, 0x5523) }, /* ch340 custom chip */
						  {} };

static struct usb_driver ch341_driver = {
	.name = "usb_ch341",
	.probe = ch341_probe,
	.disconnect = ch341_disconnect,
	.id_table = ch341_ids,
};

void usb_ch341_driver_register(void)
{
	int i;

	for (i = 0; i < CH341_MINORS; i++) {
		mutex_init(&device_lock[i]);
		thread_cond_init(&free_cond[i]);
	}

	usb_register_driver(&ch341_driver);
}

void usb_ch341_driver_deregister(void)
{
	usb_deregister(&ch341_driver);
}