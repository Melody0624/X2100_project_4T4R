#ifndef _HOST_WWAN_H_
#define _HOST_WWAN_H_

#include <stdio.h>
#include <common.h>

#ifdef CONFIG_USB_HOST_WWAN

#define WWAN_MINORS        32

struct usb_wwan_port_private {
    /* Settings for the port */
    int rts_state;        /* Handshaking pins (outputs) */
    int dtr_state;
    int cts_state;        /* Handshaking pins (inputs) */
    int dsr_state;
    int dcd_state;
    int ri_state;
};

/* usb host wwan supports up to 32 devices, each bit represents a device */
typedef void (*wwan_device_callback_t)(u32 devices_bit);

/* When a wwan device is inserted or removed, callback will be triggered to update the state */
extern void usb_host_wwan_register_callback(wwan_device_callback_t callback);
extern u32 usb_host_wwan_get_devices_bit(void);

/* usb host wwan calls notify_callback when occurs unexpected error in urb complete of interrupt read */
typedef void (*wwan_notify_callback_t)(u8 id, struct usb_wwan_port_private *data);

extern int usb_host_wwan_open(u8 id, wwan_notify_callback_t callback);
extern void usb_host_wwan_close(u8 id);
extern int usb_host_wwan_write(u8 id, void *data, int len, int timeout);
extern int usb_host_wwan_read(u8 id, void *data, int len, int timeout);
extern void usb_host_wwan_set_dtr_rts(u8 id, int dtr_state, int rts_state);
#endif

#endif /* _HOST_WWAN_H_ */
