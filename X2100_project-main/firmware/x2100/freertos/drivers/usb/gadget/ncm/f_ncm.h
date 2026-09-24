#ifndef _F_NCM_H_
#define _F_NCM_H_

#include <driver/cache.h>
#include <usb/gadget_ncm.h>
#include "../composite.h"

#ifdef NCM_DEBUG
#define NCM_DBG(...)  printf(__VA_ARGS__)
#else
#define NCM_DBG(...)
#endif

struct usb_function *ncm_device_alloc(struct usb_ncm_param *param);

void ncm_device_free(struct usb_function *f);

struct netif *ncm_get_active_netif(void);

#endif /* _F_NCM_H_ */