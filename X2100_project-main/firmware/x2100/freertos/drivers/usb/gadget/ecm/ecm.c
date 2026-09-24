#include <os.h>
#include <common.h>
#include <driver/dtrng.h>
#include <usb/ch9.h>
#include <usb/gadget_ecm.h>

#include "../composite.h"
#include "f_ecm.h"

#define ECM_DEFAULT_MTU     1500

#define DRIVER_DESC         "ECM Gadget"

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

static struct usb_configuration ecm_config_driver = {
    /* .label = f(hardware) */
    .label              = "CDC Ethernet (ECM)",
    .bConfigurationValue    = 1,
    /* .iConfiguration = DYNAMIC */
    .bmAttributes       = USB_CONFIG_ATT_SELFPOWER,
};

/****************************** Configurations ******************************/

static struct usb_ecm_param ecm_param;
static struct usb_function *f_ecm;

static int ecm_do_config(struct usb_configuration *c)
{
    int status;

    f_ecm = ecm_device_alloc(&ecm_param);
    if (IS_ERR(f_ecm))
        return PTR_ERR(f_ecm);

    status = usb_add_function(c, f_ecm);
    if (status < 0) {
        printf( "usb_add_function f_ecm fail\n");
        ecm_device_free(f_ecm);
    }

    return status;
}

static int ecm_bind(struct usb_composite_dev *cdev)
{
    int status;

    status = usb_string_ids_tab(cdev, strings_dev);
    if (status < 0)
        return status;

    device_desc.iManufacturer = strings_dev[USB_GADGET_MANUFACTURER_IDX].id;
    device_desc.iProduct = strings_dev[USB_GADGET_PRODUCT_IDX].id;

    status = usb_add_config(cdev, &ecm_config_driver, ecm_do_config);
    return status;
}

static int ecm_unbind(struct usb_composite_dev *cdev)
{
    ecm_device_free(f_ecm);
    f_ecm = NULL;
    return 0;
}

static struct usb_composite_driver ecmg_driver = {
    .name       = "g_ecm",
    .dev        = &device_desc,
    .strings    = dev_strings,
    .max_speed  = USB_SPEED_HIGH,
    .bind       = ecm_bind,
    .unbind     = ecm_unbind,
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

int gadget_ecm_init(const struct gadget_id *id, struct usb_ecm_param *param)
{
    int ret;

    if (id == NULL)
        panic("gadget_ecm_init param id is NULL! \n");

    device_desc.idVendor = id->vendor_id;
    device_desc.idProduct = id ->product_id;

    if (param)
        memcpy(&ecm_param, param, sizeof(ecm_param));

    if (!ecm_param.mtu)
        ecm_param.mtu = ECM_DEFAULT_MTU;

    check_mac_valid(ecm_param.dev_mac);
    check_mac_valid(ecm_param.host_mac);

    ret = usb_composite_probe(&ecmg_driver);

    return ret;
}

void gadget_ecm_cleanup(void)
{
    usb_composite_unregister(&ecmg_driver);
    memset(&ecm_param, 0, sizeof(ecm_param));
}

struct netif *gadget_ecm_get_netif(void)
{
    return ecm_get_active_netif();
}
