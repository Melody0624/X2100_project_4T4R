#include <common.h>
#include <os.h>
#include <usb/gadget_ecm.h>

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4a1,
};

struct usb_ecm_param ecm_param = {
    .ipaddr = "192.188.1.100",
    .netmask = "255.255.255.0",
    .gw = "192.188.1.1",
};

int gadget_usb_ecm_test(void)
{
    return gadget_ecm_init(&usb_id, &ecm_param);
}