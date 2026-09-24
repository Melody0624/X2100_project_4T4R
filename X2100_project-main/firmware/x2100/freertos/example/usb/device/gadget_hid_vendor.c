#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>

#define VENDOR_REPORT_LENGTH    64

static const unsigned char vendor_report[] = {
    0x06, 0x00, 0xff,   /* USAGE_PAGE (Vendor Defined Page 1)           */
    0x09, 0x01,         /* USAGE (Vendor Usage 1)                       */
    0xa1, 0x01,         /* COLLECTION (Application)                     */
    0x15, 0x00,         /* LOGICAL_MINIMUM (0)                          */
    0x26, 0xff, 0x00,   /* LOGICAL_MAXIMUM (255)                        */
    0x75, 0x08,         /*   REPORT_SIZE (8)                            */
    0x95, VENDOR_REPORT_LENGTH,         /*   REPORT_COUNT                          */
    0x92, 0xb8, 0x01,   /* OUTPUT (Data,Ary,Abs,Wrap,NLin,NPrf,Vol,Buf) */
    0x15, 0x00,         /* LOGICAL_MINIMUM (0)                          */
    0x26, 0xff, 0x00,   /* LOGICAL_MAXIMUM (255)                        */
    0x75, 0x08,         /*   REPORT_SIZE (8)                            */
    0x95, VENDOR_REPORT_LENGTH,         /*   REPORT_COUNT                          */
    0x82, 0xb8, 0x01,   /* INPUT (Data,Ary,Abs,Wrap,NLin,NPrf,Vol,Buf)  */
    0xc0                /* END_COLLECTION                               */
};

/* hid descriptor for a keyboard */
static const struct hid_report_descriptor hid_report = {
    .subclass        = 0, /* No subclass */
    .protocol        = 0, /* Keyboard */
    .report_length        = VENDOR_REPORT_LENGTH,
    .report_desc_length    = sizeof(vendor_report),
    .report_desc        = vendor_report
};

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4ac
};

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static void usb_gadget_hid_thread(void *data)
{
    int i;
    int len;
    unsigned char hid_data[VENDOR_REPORT_LENGTH];

    while (1) {
        msleep(1000);
        len = gadget_hid_read(hid_data, sizeof(hid_data), 0, 0);
        printf("gadget_hid_read len %d\n", len);
        if (len > 0) {
            if (len != VENDOR_REPORT_LENGTH)
                printf("vendor data len is incorrect");
            /* verdor to do */
            /* 每次接收和发送必须是报表规定的长度, 报表规定长度为VENDOR_REPORT_LENGTH */
            for(i = 0; i < len; i++)
                printf("usb hid read data %d\n", hid_data[i]);
        }

        /* verdor to do */
        /* 每次接收和发送必须是报表规定的长度, 报表规定长度为VENDOR_REPORT_LENGTH
            建议用户数据封包处理，每个包大小为VENDOR_REPORT_LENGTH
        */
        memset(hid_data, 0, sizeof(hid_data));
        gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
    }
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = NULL,
};

int gadget_usb_hid_test(void)
{
    gadget_hid_init(&usb_id, &hid_report, &hid_cb);
    thread_create("usb gadget hid thread", 8192, usb_gadget_hid_thread, NULL);

    return 0;
}
