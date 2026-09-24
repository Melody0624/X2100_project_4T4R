#include <common.h>
#include <usb/gadget_bulk.h>
#include <os.h>

static const struct gadget_id bulk_id = {
    .vendor_id = 0x1CBE,
    .product_id = 0x0003
};

static void bulk_connect_callback(int connect)
{
    printf("generic_bulk_connect_callback %d\n", connect);
}

static unsigned char usb_test_buf[1024];
static const char *usb_test_char = "hello world!\r\n";
static void usb_gadget_bulk_thread(void *data)
{
    int len, i;

    while (1) {
        msleep(1000);
        gadget_bulk_write((const unsigned char *)usb_test_char, strlen(usb_test_char), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        len = gadget_bulk_read(usb_test_buf, sizeof(usb_test_buf), 0, 0);
        if (len > 0) {
            for(i = 0; i < len; i++)
                printf("usb bulk read data %d\n", usb_test_buf[i]);
        }

    }
}

int gadget_usb_bulk_test(void)
{
    gadget_bulk_init(&bulk_id, bulk_connect_callback);
    thread_create("usb gadget bulk thread", 4096, usb_gadget_bulk_thread, NULL);
    return 0;
}
