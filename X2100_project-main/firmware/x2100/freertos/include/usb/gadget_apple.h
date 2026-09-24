#ifndef _GADGET_APPLE_H_
#define _GADGET_APPLE_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_APPLE

struct nero_buffer {
    const u8 *buf;
    u32 length; /* buf size */
    u32 actual; /* buf actual size */
    void *private_data;
};

extern int gadget_apple_init(const struct gadget_id *id, connect_callback_t connect_cb);
extern void gadget_apple_cleanup(void);

extern int gadget_nero_get_buffer(struct nero_buffer *nero_buffer, uint32_t timeout_ms);
extern int gadget_nero_put_buffer(struct nero_buffer *nero_buffer);

extern int gadget_nero_write(const uint8_t *buffer, uint32_t len, uint32_t timeout_ms);
extern int gadget_nero_flush_data(uint32_t timeout_ms);

extern int gadget_nero_get_connect_status(void);
extern int gadget_nero_wait_connect(uint32_t timeout_ms);

#endif

#endif /* _GADGET_APPLE_H_ */
