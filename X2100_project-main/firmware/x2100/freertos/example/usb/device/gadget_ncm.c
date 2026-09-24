#include <common.h>
#include <os.h>
#include <usb/gadget_ncm.h>

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4a1,
};

struct usb_ncm_param ncm_param = {
    .ipaddr = "192.188.1.100",
    .netmask = "255.255.255.0",
    .gw = "192.188.1.1",
};

int gadget_usb_ncm_test(void)
{
    return gadget_ncm_init(&usb_id, &ncm_param);
}