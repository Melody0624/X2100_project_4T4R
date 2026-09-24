#include <stdio.h>
#include <common.h>
#include <os.h>
#include <usb/host_kbd.h>

void kbd_notify_callback(unsigned char *report, unsigned int report_len, char *data, unsigned int data_len)
{
    if (data_len < 8) {
        printf("Invalid data length: %u\n", data_len);
        return;
    }

    /* the special_key byte of each bit is a special key state */
    printf("special key scancode[%d] ", data[0]);

    /* normal_key byte is a normal key scancode */
    printf("normal key scancode [%d] [%d] [%d] [%d] [%d] [%d]\n", 
            data[2], data[3], data[4], data[5], data[6], data[7]);
}

void usb_kbd_test(void)
{
    usb_kbd_register_callback(kbd_notify_callback);
}