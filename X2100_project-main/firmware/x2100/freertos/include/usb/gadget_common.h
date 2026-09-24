#ifndef _GADGET_GADGET_COMMON_H_
#define _GADGET_GADGET_COMMON_H_

#include <common.h>

struct gadget_id {
    uint16_t    vendor_id;
    uint16_t    product_id;
};

typedef void (*connect_callback_t)(int connect);
typedef void (*suspend_callback_t)(int suspend);

#endif
