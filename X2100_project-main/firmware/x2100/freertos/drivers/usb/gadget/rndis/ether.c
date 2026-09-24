#include <common.h>
#include <os.h>
#include <driver/dtrng.h>
#include <usb/gadget_rndis.h>

#include "../composite.h"

#define RNDIS_DEFAULT_MTU   1500

struct usb_function *rndis_device_alloc(struct usb_rndis_param *param);
void rndis_device_free(struct usb_function *f);

#define DRIVER_DESC		"Ethernet Gadget"
#define DRIVER_VERSION		"Memorial Day 2008"
#define PREFIX			"RNDIS/"

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,

    /* .bcdUSB = DYNAMIC */

    .bDeviceClass =    USB_CLASS_COMM,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .bMaxPacketSize0 = f(hardware) */

    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    /* .iManufacturer = DYNAMIC */
    /* .iProduct = DYNAMIC */
    .bNumConfigurations =    1,
};

static struct usb_string strings_dev[] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = PREFIX DRIVER_DESC,
    [USB_GADGET_SERIAL_IDX].s = "",
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

static struct usb_configuration rndis_config_driver = {
    .label            = "RNDIS",
    .bConfigurationValue    = 1,
    /* .iConfiguration = DYNAMIC */
    .bmAttributes        = USB_CONFIG_ATT_SELFPOWER,
};

/****************************** Configurations ******************************/

static struct usb_rndis_param rndis_param;
static struct usb_function *f_rndis;

static int rndis_do_config(struct usb_configuration *c)
{
    int status = 0;

    f_rndis = rndis_device_alloc(&rndis_param);
    if (IS_ERR(f_rndis))
        return PTR_ERR(f_rndis);

    status = usb_add_function(c, f_rndis);
    if (status < 0){
        printf( "usb_add_function f_rndis fail\n");
        rndis_device_free(f_rndis);
    }

    return status;
}

static int rndis_bind(struct usb_composite_dev *cdev)
{
    int status;

    status = usb_string_ids_tab(cdev, strings_dev);
    if (status < 0)
        return status;

    device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

    status = usb_add_config(cdev, &rndis_config_driver, rndis_do_config);
    return status;
}

static int rndis_unbind(struct usb_composite_dev *cdev)
{
    rndis_device_free(f_rndis);
    return 0;
}

static struct usb_composite_driver rndisg_driver = {
    .name        = "g_rndis",
    .dev        = &device_desc,
    .strings    = dev_strings,
    .max_speed    = USB_SPEED_HIGH,
    .bind        = rndis_bind,
    .unbind        = rndis_unbind,
};

static void check_mac_valid(unsigned char *mac)
{
    int i, sum;

    sum = 0;
    for (i = 0; i < 6; i++)
        sum += mac[i];

    if (!sum) {
        for (i = 0; i < 6; i++)
            mac[i] = dtrng_read_random_data() % 256;

        mac[0] &= 0xfe;	/* clear multicast bit */
        mac[0] |= 0x02;	/* set local assignment bit (IEEE802) */
    }
}

int gadget_rndis_init(const struct gadget_id *id, struct usb_rndis_param *param)
{
    int ret;

    if (id == NULL)
        panic("gadget_rndis_init param id is NULL! \n");

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;

    if (param)
        memcpy(&rndis_param, param, sizeof(rndis_param));

    if (!rndis_param.mtu)
        rndis_param.mtu = RNDIS_DEFAULT_MTU;

    check_mac_valid(rndis_param.dev_mac);
    check_mac_valid(rndis_param.host_mac);

    ret = usb_composite_probe(&rndisg_driver);

    return ret;
}

void gadget_rndis_cleanup(void)
{
    usb_composite_unregister(&rndisg_driver);
    memset(&rndis_param, 0, sizeof(rndis_param));
}