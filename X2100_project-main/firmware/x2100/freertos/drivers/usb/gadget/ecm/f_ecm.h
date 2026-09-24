#ifndef _F_ECM_H_
#define _F_ECM_H_

#include <driver/cache.h>
#include <usb/gadget_ecm.h>
#include "../composite.h"

#ifdef ECM_DEBUG
#define ECM_DBG(...)  printf(__VA_ARGS__)
#else
#define ECM_DBG(...)
#endif

struct usb_function *ecm_device_alloc(struct usb_ecm_param *param);

void ecm_device_free(struct usb_function *f);

struct netif *ecm_get_active_netif(void);

#endif /* _F_ECM_H_ */