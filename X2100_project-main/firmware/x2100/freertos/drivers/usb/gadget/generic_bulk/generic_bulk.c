#include <common.h>
#include <os.h>
#include <usb/gadget_bulk.h>

#include "../composite.h"

#define DRIVER_DESC        "Generic Bulk Device"

extern struct usb_function *generic_bulk_device_alloc(connect_callback_t connect_cb);
extern void generic_bulk_device_free(void);

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass =        USB_CLASS_VENDOR_SPEC,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    .bNumConfigurations =    1
};

/*-------------------------------------------------------------------------*/

/* static strings, in UTF-8 */
static struct usb_string        strings [] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
    [USB_GADGET_SERIAL_IDX].s = "ingenic",
    {  }        /* end of list */
};

static struct usb_gadget_strings    stringtab_dev = {
    .language    = 0x0409,    /* en-us */
    .strings    = strings,
};

static struct usb_gadget_strings *dev_strings[] = {
    &stringtab_dev,
    NULL,
};

static struct usb_configuration generic_bulk_cfg_driver = {
    .label            = DRIVER_DESC,
    .bConfigurationValue    = 1,
};

static connect_callback_t bulk_connect_cb;

static int generic_bulk_do_config(struct usb_configuration *c)
{
    static struct usb_function *f_bulk;
    int            status = 0;

    f_bulk = generic_bulk_device_alloc(bulk_connect_cb);
    if (IS_ERR(f_bulk))
        return PTR_ERR(f_bulk);

    status = usb_add_function(c, f_bulk);
    if (status < 0)
        generic_bulk_device_free();

    return status;
}

static int generic_bulk_bind(struct usb_composite_dev *cdev)
{
    int ret;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    ret = usb_add_config(cdev, &generic_bulk_cfg_driver, generic_bulk_do_config);

    return ret;
}

static int generic_bulk_unbind(struct usb_composite_dev *cdev)
{
    generic_bulk_device_free();

    return 0;
}

static struct usb_composite_driver generic_bulk_driver = {
    .name           = "generic_bulk",
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind        = generic_bulk_bind,
    .unbind        = generic_bulk_unbind,
};

int gadget_bulk_init(const struct gadget_id *id, connect_callback_t connect_cb)
{
    int ret;

    if (id == NULL)
        panic("gadget_generic_bulk_init param id is NULL! \n");

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    bulk_connect_cb = connect_cb;
    ret = usb_composite_probe(&generic_bulk_driver);

    return ret;
}

void gadget_bulk_cleanup(void)
{
    usb_composite_unregister(&generic_bulk_driver);
}
