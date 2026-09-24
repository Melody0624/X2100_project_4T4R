#include <common.h>
#include <usb/gadget_serial.h>
#include <os.h>

static const struct gadget_id serial_id = {
    .vendor_id = 0x0525,
    .product_id = 0xa4a7
};

static const struct usb_cdc_serial_param serial_parameters ={
    .dwDTERate = 115200,
    .bCharFormat = USB_CDC_1_STOP_BITS,
    .bParityType = USB_CDC_NO_PARITY,
    .bDataBits = 8
};

static void serial_param_callback(struct usb_cdc_serial_param *p)
{
    printf("usb serial: dwDTERate %d, bCharFormat %d, bParityType %d, bDataBits %d\n",
            p->dwDTERate, p->bCharFormat, p->bParityType, p->bDataBits);
}

static void serial_connect_callback(int connect)
{
    printf("serial_connect_callback %d\n", connect);
}

static unsigned char usb_test_buf[1024];
static const char *usb_test_char = "hello world!\r\n";
static void usb_gadget_serial_thread(void *data)
{
    int len, i;

    while (1) {
        msleep(1000);
        gadget_serial_write((const unsigned char *)usb_test_char, strlen(usb_test_char), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        len = gadget_serial_read(usb_test_buf, sizeof(usb_test_buf), 0, 0);
        if (len > 0) {
            for(i = 0; i < len; i++)
                printf("usb serial read data %d\n", usb_test_buf[i]);
        }

    }
}

int gadget_usb_serial_test(void)
{
    gadget_serial_init(&serial_id, &serial_parameters, serial_connect_callback, serial_param_callback);
    thread_create("usb gadget serial thread", 8192, usb_gadget_serial_thread, NULL);
    return 0;
}
