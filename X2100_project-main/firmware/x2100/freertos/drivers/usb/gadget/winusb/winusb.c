#include <common.h>
#include <os.h>
#include <usb/gadget_winusb.h>

#include "../composite.h"

#define DRIVER_DESC        "winusb"

extern struct usb_function *winusb_device_alloc(struct winusb_descriptor *winusb_des, connect_callback_t connect_cb);
extern void winusb_device_free(void);

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
    .bDeviceSubClass =    USB_SUBCLASS_VENDOR_SPEC,
    .bDeviceProtocol =    0xFF,
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

static struct usb_configuration winusb_cfg_driver = {
    .label            = DRIVER_DESC,
    .bConfigurationValue    = 1,
};

static connect_callback_t winusb_connect_cb;

static struct winusb_descriptor *winusb_des;

static int winusb_do_config(struct usb_configuration *c)
{
    static struct usb_function *f_winusb;
    int            status = 0;

    f_winusb = winusb_device_alloc(winusb_des, winusb_connect_cb);
    if (IS_ERR(f_winusb))
        return PTR_ERR(f_winusb);

    status = usb_add_function(c, f_winusb);
    if (status < 0)
        winusb_device_free();

    return status;
}

static int winusb_bind(struct usb_composite_dev *cdev)
{
    int ret;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    /* os_string */
    cdev->use_os_string = true;
    cdev->os_desc_config = &winusb_cfg_driver;
    cdev->b_vendor_code = winusb_des->vendor_code;
    memcpy(cdev->qw_sign, winusb_des->qw_sign, OS_STRING_QW_SIGN_LEN);

    ret = usb_add_config(cdev, &winusb_cfg_driver, winusb_do_config);

    return ret;
}

static int winusb_unbind(struct usb_composite_dev *cdev)
{
    winusb_device_free();

    return 0;
}

static struct usb_composite_driver winusb_driver = {
    .name           = "winusb",
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind        = winusb_bind,
    .unbind        = winusb_unbind,
};

int gadget_winusb_init(const struct gadget_id *id, struct winusb_descriptor *des, connect_callback_t connect_cb)
{
    int ret;

    if (id == NULL || des == NULL)
        panic("gadget_winusb_init param is NULL! \n");

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    winusb_des = des;
    winusb_connect_cb = connect_cb;
    ret = usb_composite_probe(&winusb_driver);

    return ret;
}

void gadget_winusb_cleanup(void)
{
    usb_composite_unregister(&winusb_driver);
}
