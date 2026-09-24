#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>

#include "../composite.h"
#include "hid.h"

#define DRIVER_DESC        "HID Gadget"
#define DRIVER_VERSION        "2010/03/16"

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
    .bLength = sizeof(device_desc),
    .bDescriptorType = USB_DT_DEVICE,

    /* .bcdUSB = DYNAMIC */

    .bDeviceClass =        USB_CLASS_MISC,
    .bDeviceSubClass =    0x02,
    .bDeviceProtocol =    0x01,
    /* .bMaxPacketSize0 = f(hardware) */

    /* Vendor and product id can be overridden by module parameters.  */
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    /* .iManufacturer = DYNAMIC */
    /* .iProduct = DYNAMIC */
    /* NO SERIAL NUMBER */
    .bNumConfigurations =    0, /* dynamic */
};

#define STRING_DESCRIPTION_IDX		USB_GADGET_FIRST_AVAIL_IDX

/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
    [USB_GADGET_SERIAL_IDX].s = "ingenic",
    [STRING_DESCRIPTION_IDX].s = DRIVER_DESC,
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

static connect_callback_t hid_connect_cb[2];
static hid_request_callback_t hid_request_cb[2];
static const struct hid_report_descriptor *hid_report[2];
struct usb_function *m_hid[2];

static int do_config(struct usb_configuration *c)
{
    int status;
    struct usb_function *f_hid;
    int i;

    for (i = 0; i < 2; i++) {
        f_hid = hid_device_alloc(hid_report[i], hid_connect_cb[i], hid_request_cb[i]);
        if (IS_ERR(f_hid))
            return -ENOMEM;

        status = usb_add_function(c, f_hid);
        if (status < 0)
            goto put;

        m_hid[i] = f_hid;
    }

    return 0;
put:
    if (m_hid[0])
        hid_device_free2(m_hid[0]);
    if (m_hid[1])
        hid_device_free2(m_hid[1]);
    return status;
}

static struct usb_configuration config_driver = {
    .label            = "HID Gadget",
    .bConfigurationValue    = 1,
    /* .iConfiguration = DYNAMIC */
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
    config_driver.iConfiguration = strings_dev[STRING_DESCRIPTION_IDX].id;

    /* register our configuration */
    status = usb_add_config(cdev, &config_driver, do_config);
    if (status < 0)
        return status;

    printf(DRIVER_DESC ", version: " DRIVER_VERSION "\n");

    return 0;
}

static int hid_unbind(struct usb_composite_dev *cdev)
{
    if (m_hid[0])
        hid_device_free2(m_hid[0]);
    if (m_hid[1])
        hid_device_free2(m_hid[1]);

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

int gadget_hid_init2(const struct gadget_id *id,
 const struct hid_report_descriptor *report0, struct hid_callback *hid_cb0,
 const struct hid_report_descriptor *report1, struct hid_callback *hid_cb1)
{
    int ret;

    hid_report[0] = report0;
    if (hid_cb0) {
        hid_connect_cb[0] = hid_cb0->connect_cb;
        hid_request_cb[0] = hid_cb0->request_cb;
    }
    hid_report[1] = report1;
    if (hid_cb1) {
        hid_connect_cb[1] = hid_cb1->connect_cb;
        hid_request_cb[1] = hid_cb1->request_cb;
    }
    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    ret = usb_composite_probe(&hidg_driver);

    return ret;
}

void gadget_hid_get2(struct usb_function **hid0, struct usb_function **hid1)
{
    if (hid0)
        *hid0 = m_hid[0];
    if (hid1)
        *hid1 = m_hid[1];
}

void gadget_hid_cleanup2(void)
{
    usb_composite_unregister(&hidg_driver);
}
