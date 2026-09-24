#ifndef _HOST_MOUSE_H_
#define _HOST_MOUSE_H_

#include <common.h>
#include <errno.h>
#include <os.h>

#ifdef CONFIG_USB_HOST_HID_MOUSE

typedef void (*mouse_notify_callback_t)(unsigned char btn_state, char rel_x,
                                        char rel_y, char rel_wheel);

void usb_mouse_register_callback(mouse_notify_callback_t callback);

#endif

#endif /* _HOST_MOUSE_H_ */