#ifndef _GADGET_NCM_H_
#define _GADGET_NCM_H_

#include <common.h>
#include "gadget_common.h"

struct usb_ncm_param {
    const char *ipaddr;
    const char *netmask;
    const char *gw;
    int mtu;
    unsigned char dev_mac[6];
    unsigned char host_mac[6];
};

int gadget_ncm_init(const struct gadget_id *id, struct usb_ncm_param *param);

void gadget_ncm_cleanup(void);

struct netif *gadget_ncm_get_netif(void);

#endif /* _GADGET_NCM_H_ */
