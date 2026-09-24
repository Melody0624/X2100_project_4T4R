#include <common.h>
#include <malloc.h>
#include <os.h>
#include <usb/gadget_serial_hid.h>

#include "../composite.h"
#include "serial.h"
#include "hid.h"

/* Defines */

#define GS_VERSION_NAME            "Gadget Serial and keyboard"

/*-------------------------------------------------------------------------*/

/* string IDs are assigned dynamically */

#define STRING_DESCRIPTION_IDX        USB_GADGET_FIRST_AVAIL_IDX

static struct usb_string strings_dev[] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = GS_VERSION_NAME,
    [USB_GADGET_SERIAL_IDX].s = "ingenic",
    [STRING_DESCRIPTION_IDX].s = "CDC HID Gadget",
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

static struct usb_device_descriptor device_desc = {
    .bLength =        USB_DT_DEVICE_SIZE,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass		= USB_CLASS_MISC,
    .bDeviceSubClass	= 0x02,
    .bDeviceProtocol	= 0x01,
    /* .bMaxPacketSize0 = f(hardware) */
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    /* .iManufacturer = DYNAMIC */
    /* .iProduct = DYNAMIC */
    .bNumConfigurations =    1,
};

/*-------------------------------------------------------------------------*/

static struct usb_configuration serial_config_driver = {
    .label = "CDC HID Gadget",
    .bConfigurationValue = 1,
    /* .iConfiguration = DYNAMIC */
};

static struct usb_function *f_serial;
static const struct hid_report_descriptor *hid_report;
static struct usb_cdc_serial_param serial_hid_param;
static const struct cdc_hid_callback *serial_hid_callback;

static int serial_register_ports(struct usb_configuration *c)
{
    int ret;
    struct usb_function *f_hid;
    struct usb_cdc_line_coding coding;
    connect_callback_t hid_connect_cb = NULL;
    hid_request_callback_t hid_request_cb = NULL;
    connect_callback_t serial_connect_cb = NULL;
    serial_param_callback_t  serial_param_cb = NULL;

    assert(f_serial == NULL);

    if (serial_hid_callback) {
        hid_connect_cb = serial_hid_callback->hid_connect_cb;
        hid_request_cb = serial_hid_callback->hid_request_cb;
        serial_connect_cb = serial_hid_callback->cdc_connect_cb;
        serial_param_cb = serial_hid_callback->serial_cb;
    }

    f_hid = hid_device_alloc(hid_report, hid_connect_cb, hid_request_cb);
    if (IS_ERR(f_hid)) {
        ret = PTR_ERR(f_hid);
        goto err_alloc_hid;
    }

    ret = usb_add_function(c, f_hid);
    if (ret)
        goto err_add_hid;

    ret = gserial_alloc_line(&serial_hid_param, serial_connect_cb, serial_param_cb);
    if (ret)
        goto err_alloc_line;

    coding.dwDTERate = serial_hid_param.dwDTERate;
    coding.bCharFormat = serial_hid_param.bCharFormat;
    coding.bParityType = serial_hid_param.bParityType;
    coding.bDataBits = serial_hid_param.bDataBits;
    f_serial = acm_alloc_func(&coding);
    if (IS_ERR(f_serial)) {
        ret = PTR_ERR(f_serial);
        goto err_acm_alloc_func;
    }

    ret = usb_add_function(c, f_serial);
    if (ret)
        goto err_add_serial;

    return 0;

err_add_serial:
    acm_free_func(f_serial);
    f_serial = NULL;
err_acm_alloc_func:
    gserial_free_line();
err_alloc_line:
    usb_remove_function(c, f_hid);
err_add_hid:
    hid_device_free();
err_alloc_hid:
    return ret;
}

static int gs_hid_bind(struct usb_composite_dev *cdev)
{
    int            status;

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

static int gs_hid_unbind(struct usb_composite_dev *cdev)
{
    acm_free_func(f_serial);
    f_serial = NULL;

    gserial_free_line();

    hid_device_free();

    return 0;
}

static struct usb_composite_driver gserial_driver = {
    .name        = "g_serial_hid",
    .dev        = &device_desc,
    .strings    = dev_strings,
    .max_speed    = USB_SPEED_HIGH,
    .bind        = gs_hid_bind,
    .unbind        = gs_hid_unbind,
};

int gadget_serial_hid_init(const struct gadget_id *id, const struct hid_report_descriptor *report, const struct usb_cdc_serial_param *param, const struct cdc_hid_callback* callback)
{
    int ret;

    if (report == NULL|| id == NULL) {
        panic("Error: The parameter passed in is NULL!! \n");
    }

    hid_report = report;
    if (param) {
        serial_hid_param = *param;
    } else {
        serial_hid_param.dwDTERate = 115200;
        serial_hid_param.bCharFormat = USB_CDC_1_STOP_BITS;
        serial_hid_param.bParityType = USB_CDC_NO_PARITY;
        serial_hid_param.bDataBits = 8;
    }

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id->product_id;
    serial_hid_callback = callback;

    ret = usb_composite_probe(&gserial_driver);

    return ret;
}

void gadget_serial_hid_cleanup(void)
{
    usb_composite_unregister(&gserial_driver);
}
