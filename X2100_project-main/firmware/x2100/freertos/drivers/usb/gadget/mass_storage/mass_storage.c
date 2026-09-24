#include <common.h>
#include <os.h>
#include "f_mass_storage.h"

#define DRIVER_DESC		"Mass Storage Gadget"
#define DRIVER_VERSION		"2009/09/11"

/*-------------------------------------------------------------------------*/
static struct usb_device_descriptor msg_device_desc = {
	.bLength =		sizeof msg_device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	/* .bcdUSB = DYNAMIC */
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,

	/* .idVendor = DYNAMIC */
	/* .idProduct = DYNAMIC */
	.bNumConfigurations =	1,
};

static struct usb_string strings_dev[] = {
	[USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
	[USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
	[USB_GADGET_SERIAL_IDX].s = "ingenic",
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language       = 0x0409,       /* en-us */
	.strings        = strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_function *f_msg;
struct fsg_config *fsg_config;

/****************************** Configurations ******************************/

static int msg_do_config(struct usb_configuration *c)
{
	int ret;

	f_msg = mass_storage_device_alloc(fsg_config);
	if (IS_ERR(f_msg))
		return PTR_ERR(f_msg);

	ret = usb_add_function(c, f_msg);
	if (ret)
		goto put_func;

	return 0;

put_func:
	mass_storage_device_free(f_msg);
	return ret;
}

static struct usb_configuration msg_config_driver = {
	.label			= "Linux File-Backed Storage",
	.bConfigurationValue	= 1,
	.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
};


/****************************** Gadget Bind ******************************/

static int msg_bind(struct usb_composite_dev *cdev)
{
	int status;

	status = usb_string_ids_tab(cdev, strings_dev);
	if (status < 0)
		return status;
	msg_device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
	msg_device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
	msg_device_desc.iSerialNumber = strings_dev[USB_GADGET_SERIAL_IDX].id;

	status = usb_add_config(cdev, &msg_config_driver, msg_do_config);
	if (status < 0)
		return status;

	printf(DRIVER_DESC ", version: " DRIVER_VERSION "\n");
	return 0;
}

static int msg_unbind(struct usb_composite_dev *cdev)
{
	mass_storage_device_free(f_msg);

	return 0;
}

/****************************** Some noise ******************************/

static struct usb_composite_driver msg_driver = {
	.name		= "g_mass_storage",
	.dev		= &msg_device_desc,
	.max_speed	= USB_SPEED_HIGH,
	.strings	= dev_strings,
	.bind		= msg_bind,
	.unbind		= msg_unbind,
};

int gadget_mass_storage_init(const struct gadget_id *id, struct fsg_config *config)
{
	int i;
	int ret;

	if (id == NULL || config == NULL)
		panic("%s: param id or config is NULL!\n", __func__);

	if (config->nluns == 0 || config->luns == NULL)
		panic("%s: luns is NULL!\n", __func__);
	
	if (config->nluns > FSG_MAX_LUNS) {
		config->nluns = FSG_MAX_LUNS;
		printf("%s: nluns more than the FSG_MAX_LUNS\n", __func__);
	}

	for (i = 0; i < config->nluns; i++) {
		if (config->luns[i].read_callback == NULL)
			panic("%s: lun%d read_callback is NULL!\n", __func__, i);

		if (config->luns[i].ro == 0 && config->luns[i].write_callback == NULL)
			panic("%s: lun%d write_callback is NULL!\n", __func__, i);

		if (config->luns[i].block_size == 0 || config->luns[i].num_sectors == 0)
			panic("%s: lun%d block_size or num_sectors is zero!\n", __func__, i);

		if (config->luns[i].cdrom) {
			if (config->luns[i].block_size != 2048)
				panic("%s: lun%d cdrom block_size must be 2048!\n", __func__, i);
			
			if (config->luns[i].num_sectors < 300)
				panic("%s: lun%d cdrom num_sectors must be greater than 300!\n", __func__, i);

			if (config->luns[i].num_sectors >= 256*60*75)
				panic("%s: lun%d cdrom num_sectors must be less than (256*60*75)!\n", __func__, i);
		}
	}

	if (config->serialnumber)
		strings_dev[USB_GADGET_SERIAL_IDX].s = config->serialnumber;

	msg_device_desc.idVendor = id->vendor_id;
	msg_device_desc.idProduct = id ->product_id;
	fsg_config = config;

	ret = usb_composite_probe(&msg_driver);

    return ret;
}

void gadget_mass_storage_cleanup(void)
{
	usb_composite_unregister(&msg_driver);
}