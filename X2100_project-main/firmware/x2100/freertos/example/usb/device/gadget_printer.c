#include <common.h>
#include <usb/gadget_printer.h>
#include <os.h>

static const struct gadget_id printer_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a8
};

static void printer_connect_callback(int connect)
{
    printf("printer_connect_callback %d\n", connect);
}

static unsigned char usb_test_buf[1024];
static void usb_gadget_printer_thread(void *data)
{
    int len, i;
    u8 status = PRINTER_STATUS_NOT_ERROR | PRINTER_STATUS_SELECTED;

    gadget_printer_set_status(status);

    while (1) {
        gadget_printer_wait_connect(THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        len = gadget_printer_read(usb_test_buf, sizeof(usb_test_buf), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        if (len > 0) {
            for(i = 0; i < len; i++)
                printf("%c", usb_test_buf[i]);
        }

    }
}

int gadget_usb_printer_test(void)
{
    gadget_printer_init(&printer_id, printer_connect_callback);
    thread_create("usb gadget printer thread", 4096, usb_gadget_printer_thread, NULL);
    return 0;
}
