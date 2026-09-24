#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>
#include <errno.h>
#include <driver/input.h>
#include <include/devices/gt9xx_touch.h>
#include <stdio.h>

#define TIP_SWITCH        (1<<0)
#define IN_RANGE          (1<<1)
#define CONFIDIENCE       (1<<7)

static const unsigned char touch_report[] = {
    0x05, 0x0d,                    // USAGE_PAGE (Digitizers)
    0x09, 0x04,                    // USAGE (Touch Screen)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x09, 0x22,                    //   USAGE (Finger)
    0xa1, 0x00,                    //   COLLECTION (Physical)
    0x09, 0x42,                    //     USAGE (Tip Switch)
    0x15, 0x00,                    //     LOGICAL_MINIMUM (0)
    0x25, 0x01,                    //     LOGICAL_MAXIMUM (1)
    0x75, 0x01,                    //     REPORT_SIZE (1)
    0x95, 0x01,                    //     REPORT_COUNT (1)
    0x81, 0x02,                    //     INPUT (Data,Var,Abs)
    0x09, 0x32,                    //     Usage (In Range)
    0x15, 0x00,                    //     Logical Minimum (0)
    0x25, 0x01,                    //     Logical Maximum (1)
    0x81, 0x02,                    //     Input (Data,Var,Abs,NWrp,Lin,Pref,NNul,Bit)
    0x09, 0x51,                    //     Usage (Contact Identifier)
    0x75, 0x05,                    //     Report Size (5)
    0x95, 0x01,                    //     Report Count (1)
    0x16, 0x00, 0x00,              //     Logical Minimum (0)
    0x26, 0x10, 0x00,              //     Logical Maximum (16)
    0x81, 0x02,                    //     Input (Data,Var,Abs,NWrp,Lin,Pref,NNul,Bit)
    0x09, 0x47,                    //     Usage (Confidence)
    0x75, 0x01,                    //     Report Size (1)
    0x95, 0x01,                    //     Report Count (1)
    0x15, 0x00,                    //     Logical Minimum (0)
    0x25, 0x01,                    //     Logical Maximum (1)
    0x81, 0x02,                    //     Input (Data,Var,Abs,NWrp,Lin,Pref,NNul,Bit)

    0x05, 0x01,                    //     Usage Page (Generic Desktop)
    0x09, 0x30,                    //     Usage (X)
    0x75, 0x10,                    //     Report Size (16)
    0x95, 0x01,                    //     Report Count (1)
    0x15, 0x00,                    //     Logical Minimum (0)
    0x26, 0x00, 0x05,              //     Logical Maximum (1280)
                                   //     USB触摸屏X坐标最大值
    0x81, 0x02,                    //     Input (Data,Var,Abs,NWrp,Lin,Pref,NNul,Bit)

    0x09, 0x31,                    //     Usage (Y)
    0x75, 0x10,                    //     Report Size (16)
    0x95, 0x01,                    //     Report Count (1)
    0x15, 0x00,                    //     Logical Minimum (0)
    0x26, 0xd0, 0x02,              //     Logical Maximum (720)
                                   //     USB触摸屏Y坐标最大值
    0x81, 0x02,                    //     Input (Data,Var,Abs,NWrp,Lin,Pref,NNul,Bit)
    0xC0,                          //   End Collection
    0xC0,                          // End Collection
};

/* hid descriptor for a touch screen */
static const struct hid_report_descriptor hid_report = {
    .subclass       = 0,  /* No subclass */
    .protocol       = 0,  /* None */
    .report_length      = 5,
    .report_desc_length     = sizeof(touch_report),
    .report_desc        = touch_report
};

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xb4b0
};

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static void usb_gadget_hid_thread(void *data)
{
    int ret;
    struct input_handle *handle;
    struct input_event event;
    struct goodix_ts_data gt9xx;
    uint16_t x = 0, y = 0;
    unsigned char hid_data[5];

    /* 初始化gt9xx触摸屏 */
    goodix_touch_init(&gt9xx);
    /* 开启gt9xx触摸屏 */
    handle = input_open("gt9xx");

    while(1){
        ret = input_read(handle, &event, 5000);
        if (ret == -ETIMEDOUT) {
            continue;
        } else if (ret < 0) {
            printf("read touch pos failure ret == %d", ret);
            break;
        }

        memset(hid_data, 0, sizeof(hid_data));

        if (event.id == 0) {
            printf("x == %d   y == %d   id == %d \n", event.pos.x, event.pos.y,event.id);

            x = 1280 - event.pos.y;
            y = event.pos.x;
            if (event.pos.x >=0 && event.pos.y >=0) {
                hid_data[0] = CONFIDIENCE | IN_RANGE | TIP_SWITCH;
                hid_data[1] = x & 0xFF;
                hid_data[2] = (x >> 8) & 0xFF;
                hid_data[3] = y & 0xFF;
                hid_data[4] = (y >> 8) & 0xFF;
            } else {
                hid_data[0] = CONFIDIENCE | IN_RANGE;
            }

            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        }

        /* gt9xx处理多点触控漏上报抬起事件 */
        if (event.pos.x <0 && event.pos.y <0) {
            hid_data[0] = CONFIDIENCE | IN_RANGE;
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
        }
    }

    input_close(handle);
    goodix_touch_deinit(&gt9xx);

    return;
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = NULL,
};

int gadget_usb_hid_touch_test(void)
{
    gadget_hid_init(&usb_id, &hid_report, &hid_cb);
    thread_create("usb_gadget_hid_thread", 8192, usb_gadget_hid_thread, NULL);
    return 0;
}

