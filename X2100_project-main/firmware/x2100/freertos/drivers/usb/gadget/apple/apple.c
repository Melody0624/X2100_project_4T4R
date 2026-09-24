#include <common.h>
#include <os.h>
#include <string.h>
#include <stdio.h>
#include <usb/gadget_apple.h>
#include <usb/ch9.h>
#include <driver/efuse.h>
#include <driver/dtrng.h>
#include <little_things.h>

#include "../composite.h"

#define VERSION_NUM                     0x0100

extern struct usb_function *nero_device_alloc(connect_callback_t connect_cb);
extern void nero_device_free(void);

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass =       0,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    .bcdDevice = VERSION_NUM,
    .bNumConfigurations =    1
};

/*-------------------------------------------------------------------------*/
char chip_id_strings[CHIP_ID_SIZE * 2 + 1];

#define USB_APPLE_CONFIGURATION_DESC    (USB_GADGET_FIRST_AVAIL_IDX + 0)

/* static strings, in UTF-8 */
static struct usb_string        strings [] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "Apple Inc",
    [USB_GADGET_PRODUCT_IDX].s      = "VLOG NERO",
    [USB_GADGET_SERIAL_IDX].s       = chip_id_strings,
    [USB_APPLE_CONFIGURATION_DESC].s    = "Apple Accessory",
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

static struct usb_configuration apple_cfg_driver = {
    .label                  = "apple",
    .bConfigurationValue    = 1,
    .bmAttributes = USB_CONFIG_ATT_SELFPOWER,
    .MaxPower = 80,
};

static struct usb_function *f_apple;
static connect_callback_t apple_connect_cb;

static int apple_do_config(struct usb_configuration *c)
{
    int status = 0;

    f_apple = nero_device_alloc(apple_connect_cb);
    if (IS_ERR(f_apple))
        return PTR_ERR(f_apple);

    status = usb_add_function(c, f_apple);
    if (status < 0) {
        nero_device_free();
        f_apple = NULL;
        return status;
    }

    return status;
}

static int apple_bind(struct usb_composite_dev *cdev)
{
    int ret;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    apple_cfg_driver.iConfiguration = strings[USB_APPLE_CONFIGURATION_DESC].id;

    ret = usb_add_config(cdev, &apple_cfg_driver, apple_do_config);

    return ret;
}

static int apple_unbind(struct usb_composite_dev *cdev)
{
    if (f_apple) {
        nero_device_free();
        f_apple = NULL;
    }
    return 0;
}

static struct usb_composite_driver apple_driver = {
    .name           = "apple accessory",
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind           = apple_bind,
    .unbind         = apple_unbind,
};

int gadget_apple_init(const struct gadget_id *id,
                      connect_callback_t connect_cb)
{
    int ret;
    u8 chip_id_buff[CHIP_ID_SIZE];

    if (id == NULL)
        panic("gadget_apple_init param id is NULL! \n");

    efuse_read_segment(CHIP_ID, chip_id_buff, CHIP_ID_SIZE);
    bytes_to_hex(chip_id_buff, CHIP_ID_SIZE, (char *)chip_id_strings, sizeof(chip_id_strings), 0);

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    apple_connect_cb = connect_cb;

    ret = usb_composite_probe(&apple_driver);

    return ret;
}

void gadget_apple_cleanup(void)
{
    usb_composite_unregister(&apple_driver);
}
