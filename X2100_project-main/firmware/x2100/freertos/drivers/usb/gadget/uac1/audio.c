#include <common.h>
#include <os.h>
#include <usb/gadget_uac1.h>

#include "../composite.h"

struct usb_function *f_audio_alloc(const struct uac1_params *params);
void f_audio_free(struct usb_function *f);

#define DRIVER_DESC		"Linux USB Audio Gadget"
#define DRIVER_VERSION		"Feb 2, 2012"

/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "1.0",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language = 0x0409,	/* en-us */
	.strings = strings_dev,
};

static struct usb_gadget_strings *audio_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_function *f_uac1;
static const struct uac1_params *uvc1_params;

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */
	.bDeviceClass =		USB_CLASS_MISC,
	.bDeviceSubClass =	0x02,
	.bDeviceProtocol =	0x01,
	/* .bMaxPacketSize0 = f(hardware) */

	/* Vendor and product id defaults change according to what configs
	 * we support.  (As does bNumConfigurations.)  These values can
	 * also be overridden by module parameters.
	 */
	/* .idVendor =		DYNAMIC */
	/* .idProduct =		DYNAMIC */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* .iSerialNumber = DYNAMIC */
	.bNumConfigurations =	1,
};

/*-------------------------------------------------------------------------*/

static int audio_do_config(struct usb_configuration *c)
{
	int status;

	f_uac1 = f_audio_alloc(uvc1_params);
	if (IS_ERR(f_uac1)) {
		status = PTR_ERR(f_uac1);
		return status;
	}

	status = usb_add_function(c, f_uac1);
	if (status < 0) {
		f_audio_free(f_uac1);
		f_uac1 = NULL;
		return status;
	}

	return 0;
}

static struct usb_configuration audio_config_driver = {
	.label			= DRIVER_DESC,
	.bConfigurationValue	= 1,
	/* .iConfiguration = DYNAMIC */
};

/*-------------------------------------------------------------------------*/

static int audio_bind(struct usb_composite_dev *cdev)
{
	int			status;

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		return status;
	device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	device_desc.iSerialNumber = strings_dev[USB_GADGET_SERIAL_IDX].id;

	status = usb_add_config(cdev, &audio_config_driver, audio_do_config);

	return status;
}

static int audio_unbind(struct usb_composite_dev *cdev)
{
	if (!IS_ERR_OR_NULL(f_uac1))
		f_audio_free(f_uac1);

	return 0;
}

static struct usb_composite_driver audio_driver = {
	.name		= "g_audio",
	.dev		= &device_desc,
	.strings	= audio_strings,
	.max_speed	= USB_SPEED_HIGH,
	.bind		= audio_bind,
	.unbind		= audio_unbind,
};

int gadget_uac1_init(const struct gadget_id *id, const struct uac1_params *params)
{
	int ret;

	if (params == NULL || id == NULL)
		panic("%s: param or id is NULL!\n", __func__);

	device_desc.idVendor = id->vendor_id;
	device_desc.idProduct = id->product_id;
	uvc1_params = params;

	ret = usb_composite_probe(&audio_driver);

	return ret;
}

void gadget_uac1_cleanup(void)
{
	usb_composite_unregister(&audio_driver);
}


