#include <common.h>
#include <os.h>
#include <usb/gadget_printer.h>

#include "../composite.h"

extern struct usb_function *printer_device_alloc(connect_callback_t connect_cb, const char *pnp_string);
extern void printer_device_free(void);

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass =        USB_CLASS_PER_INTERFACE,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    .bNumConfigurations =    1
};

/*-------------------------------------------------------------------------*/

static const char *printer_pnp_string =
    "MFG:rtos;MDL:g_printer;CLS:PRINTER;SN:1;";

/* static strings, in UTF-8 */
static struct usb_string        strings [] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = "Printer Gadget",
    [USB_GADGET_SERIAL_IDX].s =    "ingenic",
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

static struct usb_configuration printer_cfg_driver = {
    .label            = "printer",
    .bConfigurationValue    = 1,
    .bmAttributes        = USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
};

static connect_callback_t printer_connect_cb;

static int printer_do_config(struct usb_configuration *c)
{
    struct usb_gadget    *gadget = c->cdev->gadget;
    static struct usb_function *f_printer;
    int            status = 0;

    usb_ep_autoconfig_reset(gadget);

    usb_gadget_set_selfpowered(gadget);

    f_printer = printer_device_alloc(printer_connect_cb, printer_pnp_string);
    if (IS_ERR(f_printer))
        return PTR_ERR(f_printer);

    status = usb_add_function(c, f_printer);
    if (status < 0)
        printer_device_free();

    return status;
}

static int printer_bind(struct usb_composite_dev *cdev)
{
    int ret;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    ret = usb_add_config(cdev, &printer_cfg_driver, printer_do_config);

    return ret;
}

static int printer_unbind(struct usb_composite_dev *cdev)
{
    printer_device_free();

    return 0;
}

static struct usb_composite_driver printer_driver = {
    .name           = "printer",
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind        = printer_bind,
    .unbind        = printer_unbind,
};

int gadget_printer_init(const struct gadget_id *id, connect_callback_t connect_cb)
{
    int ret;

    if (id == NULL)
        panic("gadget_printer_init param id is NULL! \n");

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    printer_connect_cb = connect_cb;

    ret = usb_composite_probe(&printer_driver);

    return ret;
}

void gadget_printer_cleanup(void)
{
    usb_composite_unregister(&printer_driver);
}
