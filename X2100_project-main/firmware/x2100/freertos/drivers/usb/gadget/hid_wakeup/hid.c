#include <common.h>
#include <os.h>
#include <usb/gadget_hid_wakeup.h>

#include "../composite.h"
#include "hid.h"

#define DRIVER_DESC        "HID Gadget"
#define DRIVER_VERSION        "2010/03/16"

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
    .bLength = sizeof device_desc,
    .bDescriptorType = USB_DT_DEVICE,

    /* .bcdUSB = DYNAMIC */

    /* .bDeviceClass =        USB_CLASS_COMM, */
    /* .bDeviceSubClass =    0, */
    /* .bDeviceProtocol =    0, */
    .bDeviceClass = USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .bMaxPacketSize0 = f(hardware) */

    /* Vendor and product id can be overridden by module parameters.  */
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    /* .iManufacturer = DYNAMIC */
    /* .iProduct = DYNAMIC */
    /* NO SERIAL NUMBER */
    .bNumConfigurations =    1,
};

/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
    [USB_GADGET_SERIAL_IDX].s = "ingenic",
    {  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
    .language    = 0x0409,    /* en-us */
    .strings    = strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
    &stringtab_dev,
    NULL,
};



/****************************** Configurations ******************************/

static connect_callback_t hid_connect_cb;
static hid_request_callback_t hid_request_cb;
static suspend_callback_t hid_suspend_cb;
static const struct hid_report_descriptor *hid_report;
static int do_config(struct usb_configuration *c)
{
    int status;
    struct usb_function *f_hid;

    f_hid = hid_device_alloc(hid_report, hid_connect_cb, hid_request_cb, hid_suspend_cb);
    if (IS_ERR(f_hid))
        return -ENOMEM;

    status = usb_add_function(c, f_hid);
    if (status < 0)
        goto put;

    return 0;
put:
    hid_device_free();
    return status;
}

static struct usb_configuration config_driver = {
    .label            = "HID Gadget",
    .bConfigurationValue    = 1,
    /* .iConfiguration = DYNAMIC */
    .bmAttributes        = USB_CONFIG_ATT_WAKEUP,
};

static int hid_bind(struct usb_composite_dev *cdev)
{
    int status;

    /* Allocate string descriptor numbers ... note that string
     * contents can be overridden by the composite_dev glue.
     */

    status = usb_string_ids_tab(cdev, strings_dev);
    if (status < 0)
        return status;
    device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings_dev[USB_GADGET_SERIAL_IDX].id;

    /* register our configuration */
    status = usb_add_config(cdev, &config_driver, do_config);
    if (status < 0)
        return status;

    printf(DRIVER_DESC ", version: " DRIVER_VERSION "\n");

    return 0;
}

static int hid_unbind(struct usb_composite_dev *cdev)
{
    hid_device_free();

    return 0;
}

static struct usb_composite_driver hidg_driver = {
    .name        = "g_hid",
    .dev        = &device_desc,
    .strings    = dev_strings,
    .max_speed    = USB_SPEED_HIGH,
    .bind        = hid_bind,
    .unbind        = hid_unbind,
};

int gadget_hid_wakeup_init(const struct gadget_id *id, const struct hid_report_descriptor *report, struct hid_callback *hid_cb,
        suspend_callback_t suspend_cb)
{
    int ret;

    if (report == NULL || id == NULL)
        panic("gadget_hid_init param id or report is NULL! \n");

    hid_report = report;
    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    if (hid_cb) {
        hid_connect_cb = hid_cb->connect_cb;
        hid_request_cb = hid_cb->request_cb;
    }
    hid_suspend_cb = suspend_cb;

    ret = usb_composite_probe(&hidg_driver);

    return ret;
}

void gadget_hid_wakeup_cleanup(void)
{
    usb_composite_unregister(&hidg_driver);
}
