#ifndef _GADGET_ECM_H_
#define _GADGET_ECM_H_

#include <common.h>
#include "gadget_common.h"

struct usb_ecm_param {
    const char *ipaddr;
    const char *netmask;
    const char *gw;
    int mtu;
    unsigned char dev_mac[6];
    unsigned char host_mac[6];
};

int gadget_ecm_init(const struct gadget_id *id, struct usb_ecm_param *param);

void gadget_ecm_cleanup(void);

struct netif *gadget_ecm_get_netif(void);

#endif /* _GADGET_ECM_H_ */
