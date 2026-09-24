#include <common.h>
#include <os.h>
#include <usb/gadget_rndis.h>

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4a2
};

struct usb_rndis_param rndis_param = {
    .ipaddr = "192.168.1.100",
    .netmask = "255.255.255.0",
    .gw = "192.168.1.1",
};

int gadget_usb_rndis_test(void)
{
    return gadget_rndis_init(&usb_id, &rndis_param);
}