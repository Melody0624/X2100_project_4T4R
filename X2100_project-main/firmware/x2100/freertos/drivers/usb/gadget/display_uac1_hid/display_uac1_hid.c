#include <common.h>
#include <os.h>
#include <usb/gadget_display_uac1_hid.h>
#include <driver/efuse.h>
#include <little_things.h>

#include "../composite.h"
#include "hid.h"

#define DRIVER_DESC        "USB Display"

extern struct usb_function *display_device_alloc(struct display_edid *edid, struct display_specific_des *des, connect_callback_t connect_cb);
extern void display_device_free(void);

extern struct usb_function *f_audio_alloc(const struct uac1_params *params);
extern void f_audio_free(struct usb_function *f);

/*-------------------------------------------------------------------------*/

/*
 * DESCRIPTORS ... most are static, but strings and (full) configuration
 * descriptors are built on demand.
 */

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,
    /* .bcdUSB = DYNAMIC */
    .bDeviceClass =        USB_CLASS_MISC,
    .bDeviceSubClass =    0x02,
    .bDeviceProtocol =    0x01,
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

static struct usb_configuration display_audio_hid_cfg_driver = {
    .label            = DRIVER_DESC,
    .bConfigurationValue    = 1,
    .MaxPower = 500,
};

static struct usb_function *f_display;
static struct display_params *display_params;

static struct usb_function *f_uac1;
static const struct uac1_params *uac1_params;

static struct usb_function *f_hid;
static const struct hid_params *hid_params;

static int display_audio_hid_do_config(struct usb_configuration *c)
{
    int            status = 0;

    f_display = display_device_alloc(&display_params->edid, &display_params->des, display_params->connect_cb);
    if (IS_ERR(f_display)) {
        status = PTR_ERR(f_display);
        goto err_out;
    }

    status = usb_add_function(c, f_display);
    if (status < 0)
        goto err_add_display;

    f_hid = hid_device_alloc(&hid_params->des, hid_params->connect_cb, hid_params->request_cb);
    if (IS_ERR(f_hid)) {
        status = PTR_ERR(f_hid);
        goto err_alloc_hid;
    }

    status = usb_add_function(c, f_hid);
    if (status)
        goto err_add_hid;

    f_uac1 = f_audio_alloc(uac1_params);
    if (IS_ERR(f_uac1)) {
        status = PTR_ERR(f_uac1);
        goto err_alloc_audio;
    }

    status = usb_add_function(c, f_uac1);
    if (status < 0)
        goto err_add_audio;

    return status;

err_add_audio:
    f_audio_free(f_uac1);
err_alloc_audio:
    usb_remove_function(c, f_hid);
err_add_hid:
    hid_device_free();
err_alloc_hid:
    usb_remove_function(c, f_display);
err_add_display:
    display_device_free();
err_out:
    return status;
}

static int display_audio_hid_bind(struct usb_composite_dev *cdev)
{
    int ret;

    ret = usb_string_ids_tab(cdev, strings);
    if (ret < 0)
        return ret;

    device_desc.iManufacturer = strings[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings[USB_GADGET_PRODUCT_IDX].id;
    device_desc.iSerialNumber = strings[USB_GADGET_SERIAL_IDX].id;

    ret = usb_add_config(cdev, &display_audio_hid_cfg_driver, display_audio_hid_do_config);

    return ret;
}

static int display_audio_hid_unbind(struct usb_composite_dev *cdev)
{
    f_audio_free(f_uac1);
    hid_device_free();
    display_device_free();

    return 0;
}

static struct usb_composite_driver display_driver = {
    .name           = "display",
    .dev            = &device_desc,
    .strings        = dev_strings,
    .max_speed      = USB_SPEED_HIGH,
    .bind        = display_audio_hid_bind,
    .unbind        = display_audio_hid_unbind,
};

int gadget_display_audio_hid_init(const struct gadget_id *id, struct display_params *dparams,
    struct uac1_params *uparams, struct hid_params *hparams)
{
    int ret;
    u8 chip_id_buff[CHIP_ID_SIZE];

    if (!id || !dparams || !uparams || !hparams)
        panic("gadget_display_audio_hid_init param is NULL! \n");

    if (!dparams->des.buf || !dparams->des.size)
        panic("dparams invalid specific des\n");

    if (!dparams->edid.buf || !dparams->edid.size)
        panic("dparams invalid edid\n");

    efuse_read_segment(CHIP_ID, chip_id_buff, CHIP_ID_SIZE);
    bytes_to_hex(chip_id_buff, CHIP_ID_SIZE, (char *)chip_id_strings, sizeof(chip_id_strings), 0);

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;
    display_params = dparams;
    uac1_params = uparams;
    hid_params = hparams;

    ret = usb_composite_probe(&display_driver);

    return ret;
}

void gadget_display_audio_hid_cleanup(void)
{
    usb_composite_unregister(&display_driver);
}
