#include <stdio.h>
#include <common.h>
#include <os.h>
#include <usb/host_mouse.h>

void mouse_notify_callback(unsigned char btn_state, char rel_x,
                           char rel_y, char rel_wheel)
{
    printf("Mouse state : BTN_LEFT(%d) BTN_RIGHT(%d) BTN_MIDDLE(%d) BTN_SIDE(%d) BTN_EXTRA(%d)"
           "REL_X(%d) REL_Y(%d) REL_WHEEL(%d)\n", (btn_state & 0x01), (btn_state & 0x02),
           (btn_state & 0x04), (btn_state & 0x08), (btn_state & 0x10), rel_x, rel_y, rel_wheel);
}

void usb_mouse_test(void)
{
    usb_mouse_register_callback(mouse_notify_callback);
}