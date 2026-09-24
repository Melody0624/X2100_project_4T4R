#include <os.h>
#include <common.h>
#include <driver/dtrng.h>
#include <usb/ch9.h>
#include <usb/gadget_ncm.h>

#include "../composite.h"
#include "f_ncm.h"

#define NCM_DEFAULT_MTU     1500

#define DRIVER_DESC         "NCM Gadget"

/*-------------------------------------------------------------------------*/

static struct usb_device_descriptor device_desc = {
    .bLength =        sizeof device_desc,
    .bDescriptorType =    USB_DT_DEVICE,

    /* .bcdUSB = DYNAMIC */

    .bDeviceClass =        USB_CLASS_COMM,
    .bDeviceSubClass =    0,
    .bDeviceProtocol =    0,
    /* .bMaxPacketSize0 = f(hardware) */

    /* .idVendor = DYNAMIC */
    /* .idProduct = DYNAMIC */
    /* .bcdDevice = f(hardware) */
    /* .iManufacturer = DYNAMIC */
    /* .iProduct = DYNAMIC */
    .bNumConfigurations =    1,
};

/* string IDs are assigned dynamically */
static struct usb_string strings_dev[] = {
    [USB_GADGET_MANUFACTURER_IDX].s = "INGENIC",
    [USB_GADGET_PRODUCT_IDX].s = DRIVER_DESC,
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

static struct usb_configuration ncm_config_driver = {
    /* .label = f(hardware) */
    .label              = "CDC Ethernet (NCM)",
    .bConfigurationValue    = 1,
    /* .iConfiguration = DYNAMIC */
    .bmAttributes       = USB_CONFIG_ATT_SELFPOWER,
};

/****************************** Configurations ******************************/

static struct usb_ncm_param ncm_param;
static struct usb_function *f_ncm;

static int ncm_do_config(struct usb_configuration *c)
{
    int status;

    f_ncm = ncm_device_alloc(&ncm_param);
    if (IS_ERR(f_ncm))
        return PTR_ERR(f_ncm);

    status = usb_add_function(c, f_ncm);
    if (status < 0) {
        printf( "usb_add_function f_ncm fail\n");
        ncm_device_free(f_ncm);
    }

    return status;
}

static int ncm_bind(struct usb_composite_dev *cdev)
{
    int status;

    status = usb_string_ids_tab(cdev, strings_dev);
    if (status < 0)
        return status;

    device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

    status = usb_add_config(cdev, &ncm_config_driver, ncm_do_config);
    return status;
}

static int ncm_unbind(struct usb_composite_dev *cdev)
{
    ncm_device_free(f_ncm);
    f_ncm = NULL;
    return 0;
}

static struct usb_composite_driver ncmg_driver = {
    .name       = "g_ncm",
    .dev        = &device_desc,
    .strings    = dev_strings,
    .max_speed  = USB_SPEED_HIGH,
    .bind       = ncm_bind,
    .unbind     = ncm_unbind,
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

        mac[0] &= 0xfe;    /* clear multicast bit */
        mac[0] |= 0x02;    /* set local assignment bit (IEEE802) */
    }
}

int gadget_ncm_init(const struct gadget_id *id, struct usb_ncm_param *param)
{
    int ret;

    if (id == NULL)
        panic("gadget_ncm_init param id is NULL! \n");

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;

    if (param)
        memcpy(&ncm_param, param, sizeof(ncm_param));

    if (!ncm_param.mtu)
        ncm_param.mtu = NCM_DEFAULT_MTU;

    check_mac_valid(ncm_param.dev_mac);
    check_mac_valid(ncm_param.host_mac);

    ret = usb_composite_probe(&ncmg_driver);

    return ret;
}

void gadget_ncm_cleanup(void)
{
    usb_composite_unregister(&ncmg_driver);
    memset(&ncm_param, 0, sizeof(ncm_param));
}

struct netif *gadget_ncm_get_netif(void)
{
    return ncm_get_active_netif();
}
