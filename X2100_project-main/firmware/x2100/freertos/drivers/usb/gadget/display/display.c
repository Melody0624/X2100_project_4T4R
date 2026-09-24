#include <common.h>
#include <os.h>
#include <usb/gadget_display.h>
#include <driver/efuse.h>
#include <little_things.h>

#include "../composite.h"

#define DRIVER_DESC        "USB Display"

extern struct usb_function *display_device_alloc(struct display_edid *edid, struct display_specific_des *des, connect_callback_t connect_cb);
extern void display_device_free(void);

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass =        0,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    .bNumConfigurations =    1
};

/*-------------------------------------------------------------------------*/
char chip_id_strings[CHIP_ID_SIZE * 2 + 1]; // 12 byte -> 24 char + '\0'

/* static strings, in UTF-8 */
static struct usb_string        strings [] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
    [USB_GADGET_SERIAL_IDX].s = chip_id_strings,
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

static struct usb_configuration display_cfg_driver = {
    .label            = DRIVER_DESC,
    .bConfigurationValue    = 1,
    .MaxPower = 500,
};

static struct usb_function *f_display;
static struct display_edid *display_edid;
static struct display_specific_des *display_specific_des;
static connect_callback_t display_connect_cb;

static int display_do_config(struct usb_configuration *c)
{
    int            status = 0;

    f_display = display_device_alloc(display_edid, display_specific_des, display_connect_cb);
    if (IS_ERR(f_display))
        return PTR_ERR(f_display);

    status = usb_add_function(c, f_display);
    if (status < 0)
        display_device_free();

    return status;
}

static int display_bind(struct usb_composite_dev *cdev)
{
    int ret;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    ret = usb_add_config(cdev, &display_cfg_driver, display_do_config);

    return ret;
}

static int display_unbind(struct usb_composite_dev *cdev)
{
    display_device_free();

    return 0;
}

static struct usb_composite_driver display_driver = {
    .name           = "display",
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind        = display_bind,
    .unbind        = display_unbind,
};

int gadget_display_init(const struct gadget_id *id, struct display_edid *edid, struct display_specific_des *des, connect_callback_t connect_cb)
{
    int ret;
    u8 chip_id_buff[CHIP_ID_SIZE];

    if (id == NULL)
        panic("gadget_display_init param id is NULL! \n");

    if (des == NULL || des->buf == NULL || des->size == 0)
        panic("invalid specific des\n");

    if (edid == NULL || edid->buf == NULL || edid->size == 0)
        panic("invalid edid\n");

    efuse_read_segment(CHIP_ID, chip_id_buff, CHIP_ID_SIZE);
    bytes_to_hex(chip_id_buff, CHIP_ID_SIZE, (char *)chip_id_strings, sizeof(chip_id_strings), 0);

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    display_edid = edid;
    display_specific_des = des;
    display_connect_cb = connect_cb;
    ret = usb_composite_probe(&display_driver);

    return ret;
}

void gadget_display_cleanup(void)
{
    usb_composite_unregister(&display_driver);
}
