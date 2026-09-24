#ifndef _HOST_KBD_H_
#define _HOST_KBD_H_

#include <common.h>
#include <errno.h>
#include <os.h>

#ifdef CONFIG_USB_HOST_HID_KEYBOARD

typedef void (*kbd_notify_callback_t)(unsigned char *report, unsigned int report_len, char *data, unsigned int data_len);

extern void usb_kbd_register_callback(kbd_notify_callback_t callback);

#endif

#endif /* _HOST_KBD_H_ */