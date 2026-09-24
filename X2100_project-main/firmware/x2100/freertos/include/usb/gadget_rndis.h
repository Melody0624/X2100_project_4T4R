#ifndef _GADGET_RNDIS_H_
#define _GADGET_RNDIS_H_

#include <common.h>
#include "gadget_common.h"

struct usb_rndis_param {
    const char *ipaddr;
    const char *netmask;
    const char *gw;
    int mtu;
    unsigned char dev_mac[6];
    unsigned char host_mac[6];
};

int gadget_rndis_init(const struct gadget_id *id, struct usb_rndis_param *param);

void gadget_rndis_cleanup(void);

#endif /* _GADGET_RNDIS_H_ */
