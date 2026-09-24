#include <common.h>
#include <malloc.h>
#include <os.h>
#include <usb/gadget_serial.h>

#include "../composite.h"
#include "serial.h"

/* Defines */

#define GS_VERSION_STR			"v2.4"
#define GS_VERSION_NUM			0x2400

#define GS_LONG_NAME			"Gadget Serial"
#define GS_VERSION_NAME			GS_LONG_NAME " " GS_VERSION_STR

/*-------------------------------------------------------------------------*/

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
	[USB_GADGET_PRODUCT_IDX].s = GS_VERSION_NAME,
	[USB_GADGET_SERIAL_IDX].s = "ingenic",
	[STRING_DESCRIPTION_IDX].s = "CDC ACM config",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_device_descriptor device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	/* .bcdUSB = DYNAMIC */
	.bDeviceClass = USB_CLASS_COMM,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */
	/* .idVendor = GS_VENDOR_ID */
	/* .idProduct = GS_CDC_PRODUCT_ID */
	.bcdDevice = GS_VERSION_NUM,
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	.bNumConfigurations =	1,
};

/*-------------------------------------------------------------------------*/

static struct usb_configuration serial_config_driver = {
	.label = "CDC ACM config",
	.bConfigurationValue = 1,
	/* .iConfiguration = DYNAMIC */
};

static struct usb_function *f_serial;
static connect_callback_t serial_connect_cb;
static serial_param_callback_t serial_cdc_param_cb;
static struct usb_cdc_serial_param serial_cdc_param;

static int serial_register_ports(struct usb_configuration *c)
{
	int ret;
	struct usb_cdc_line_coding coding;

	assert(f_serial == NULL);

	ret = gserial_alloc_line(&serial_cdc_param, serial_connect_cb, serial_cdc_param_cb);
	if (ret) {
		goto out;
	}

	coding.dwDTERate = serial_cdc_param.dwDTERate;
	coding.bCharFormat = serial_cdc_param.bCharFormat;
	coding.bParityType = serial_cdc_param.bParityType;
	coding.bDataBits = serial_cdc_param.bDataBits;
	f_serial = acm_alloc_func(&coding);
	if (IS_ERR(f_serial)) {
		ret = PTR_ERR(f_serial);
		goto err_acm_alloc_func;
	}

	ret = usb_add_function(c, f_serial);
	if (ret)
		goto err_add_func;

	return 0;

err_add_func:
	acm_free_func(f_serial);
err_acm_alloc_func:
	f_serial = NULL;
	gserial_free_line();
out:
	return ret;
}

static int gs_bind(struct usb_composite_dev *cdev)
{
	int			status;

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		return status;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	device_desc.iSerialNumber = strings_dev[USB_GADGET_SERIAL_IDX].id;
	status = strings_dev[STRING_DESCRIPTION_IDX].id;
	serial_config_driver.iConfiguration = status;

	/* register our configuration */
    status = usb_add_config(cdev, &serial_config_driver, serial_register_ports);
	if (status < 0)
		return status;

	return 0;
}

static int gs_unbind(struct usb_composite_dev *cdev)
{
	acm_free_func(f_serial);
	f_serial = NULL;

	gserial_free_line();

	return 0;
}

static struct usb_composite_driver gserial_driver = {
	.name		= "g_serial",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= gs_bind,
	.unbind		= gs_unbind,
};

int gadget_serial_init(const struct gadget_id *id, const struct usb_cdc_serial_param *param, connect_callback_t connect_cb, serial_param_callback_t serial_cb)
{
	int ret;

	if (id == NULL) {
		panic("Error: The parameter passed in is NULL! \n");
	}

	serial_connect_cb = connect_cb;
	serial_cdc_param_cb = serial_cb;
	device_desc.idVendor = id->vendor_id;
	device_desc.idProduct = id->product_id;
	if(param) {
		serial_cdc_param = *param;
	} else {
		serial_cdc_param.dwDTERate = 115200;
		serial_cdc_param.bCharFormat = USB_CDC_1_STOP_BITS;
		serial_cdc_param.bParityType = USB_CDC_NO_PARITY;
		serial_cdc_param.bDataBits = 8;
	}

	ret = usb_composite_probe(&gserial_driver);

	return ret;
}

void gadget_serial_cleanup(void)
{
	usb_composite_unregister(&gserial_driver);
}
