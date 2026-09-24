#include <common.h>
#include <little_things.h>
#include <usb/gadget_winusb.h>
#include <os.h>

static const struct gadget_id winusb_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a6
};

static struct winusb_ext_prop winusb_ext_prop[] = {
    {
        .type = USB_EXT_PROP_UNICODE,
        .name = "Label",
        .data = "XYZ Device"
    },
};

static struct winusb_descriptor winusb_des = {
    .qw_sign = {'M',  0,  'S',  0,  'F',  0,  'T',  0,  '1',  0,  '0',  0,  '0',  0},
    .vendor_code = 0x01,

    .ext_compat_id = {
            'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,       // compactiableID[8]    //or you can set LIBUSB as well
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // subCompactiableID[8]
    },

    .ext_prop_count = ARRAY_SIZE(winusb_ext_prop),
    .ext_prop = winusb_ext_prop,
};

static void winusb_connect_callback(int connect)
{
    printf("winusb_connect_callback %d\n", connect);
}

static unsigned char usb_test_buf[1024];
static const char *usb_test_char = "hello world!\r\n";
static void usb_gadget_winusb_thread(void *data)
{
    int len, i;

    while (1) {
        msleep(1000);
        gadget_winusb_write((const unsigned char *)usb_test_char, strlen(usb_test_char), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        len = gadget_winusb_read(usb_test_buf, sizeof(usb_test_buf), 0, 0);
        if (len > 0) {
            for(i = 0; i < len; i++)
                printf("usb winusb read data %d\n", usb_test_buf[i]);
        }

    }
}

int gadget_usb_winusb_test(void)
{
    gadget_winusb_init(&winusb_id, &winusb_des, winusb_connect_callback);
    thread_create("usb gadget winusb thread", 4096, usb_gadget_winusb_thread, NULL);
    return 0;
}
