#ifndef _USB_H_
#define _USB_H_

#ifdef CONFIG_USB_DRIVER

extern int usb_core_init(void);
extern void usb_core_exit(void);
extern void usb_core_suspend(void);
extern void usb_core_resume(void);

#include <usb/ch9.h>
#include <soc/usb.h>

#ifdef CONFIG_USB_IS_DEVICE
/* 注意： 需要在usb_core_init之前设置callback */
typedef void (*usb_state_callback_t)(enum usb_device_state state);
typedef void (*usb_power_callback_t)(enum usb_device_power type, unsigned mA);

extern void usb_device_set_state_callback(usb_state_callback_t state_cb);
extern void usb_device_set_power_callback(usb_power_callback_t power_cb);
extern const char *usb_state_string(enum usb_device_state state);
extern const char *usb_power_string(enum usb_device_power type);

extern void usb_device_vbus_status_update(int status);

#endif

#endif
#endif
