#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>
#include <errno.h>
#include <driver/input.h>
#include <include/devices/gt9xx_touch.h>
#include <stdio.h>
#include <math.h>

#define LIFT_DISTANCE 360
#define DISTANCE 1100

static const unsigned char mouse_report[] = {
    0x05, 0x01, /* USAGE_PAGE (Generic Desktop Control)   */
    0x09, 0x02, /* USAGE (Mouse)                          */
    0xa1, 0x01, /* COLLECTION (Application)               */
    0x09, 0x01, /* USAGE (Pointer)                        */
    0xa1, 0x00, /* COLLECTION (Physical)                  */

    0x05, 0x09, /* USAGE_PAGE (Button)                    */
    0x19, 0x01, /* USAGE_MINIMUM (1)                      */
    0x29, 0x03, /* USAGE_MINIMUM (3)                      */
    0x15, 0x00, /* LOGICAL_MINIMUM (0)                    */
    0x25, 0x01, /* LOGICAL_MINIMUM (1)                    */
    0x75, 0x01, /* REPORT_SIZE (1)                        */
    0x95, 0x03, /* REPORT_COUNT (3)                       */
    0x81, 0x02, /* INPUT (Data,Var,Abs)                   */

    0x75, 0x05, /* REPORT_SIZE (5)                        */
    0x95, 0x01, /* REPORT_COUNT (1)                       */
    0x81, 0x01, /* INPUT (Constant)                       */

    0x05, 0x01, /* USAGE_PAGE (Generic Desktop Control)   */
    0x09, 0x30, /* USAGE (X)                              */
    0x09, 0x31, /* USAGE (Y)                              */
    0x09, 0x38, /* USAGE (Whell)                          */
    0x15, 0x81, /* LOGICAL_MINIMUM (-127)                 */
    0x25, 0x7f, /* LOGICAL_MINIMUM (127)                  */
    0x75, 0x08, /* REPORT_SIZE (8)                        */
    0x95, 0x03, /* REPORT_COUNT (3)                       */
    0x81, 0x06, /* INPUT (Data, Var, Rel)                 */
    0xc0,       /* END_COLLECTION                    */
    0xc0        /* END_COLLECTION                    */
};

/* hid descriptor for a mouse */
static const struct hid_report_descriptor hid_report = {
    .subclass = 0, /* No subclass */
    .protocol = 2, /* Mouse */
    .report_length = 4,
    .report_desc_length = sizeof(mouse_report),
    .report_desc = mouse_report};

static const struct gadget_id usb_id = {
    .vendor_id = 0x0525,
    .product_id = 0xb4b0};

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static void usb_gadget_hid_thread(void *data)
{
    int rel_x;   // x的位移
    int rel_y;   // y的位移
    int old_x;   // 记录前一次得到的数据
    int old_y;   // 记录前一次得到的数据
    int first_x; // 记录按下后的初坐标

    int old_value = 0; // 上一次按下的值
    int key_left = 0;  // 左键按下
    int key_right = 0; // 右键按下

    int ret;
    struct input_handle *handle;
    struct input_event event;

    struct goodix_ts_data gt9xx;
    /* 初始化gsl1680触摸屏 */

    goodix_touch_init(&gt9xx);

    /* 开启gsl1680触摸屏 */
    handle = input_open("gt9xx");

    while (1)
    {
        ret = input_read(handle, &event, 5);

        if (ret == -ETIMEDOUT) {
            continue;
        } else if (ret < 0) {
            printf("read touch pos failure ret == %d", ret);
            break;
        }

        printf("x == %d   y == %d  id == %d   \n", event.pos.x, event.pos.y, event.id);

        if (event.pos.x >=0 && event.pos.y >=0 ) {
            if (event.pos.y > DISTANCE && event.id == 0) {
                if (event.pos.x < LIFT_DISTANCE) {
                    // 左键按下
                    unsigned char hid_data[4];
                    memset(hid_data, 0, sizeof(hid_data));
                    hid_data[0] = 0x01;
                    gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
                    key_left = 1;
                } else {
                    // 右键按下
                    unsigned char hid_data[4];
                    memset(hid_data, 0, sizeof(hid_data));
                    hid_data[0] = 0x03;
                    gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
                    key_right = 1;
                }
            } else {
                if (old_value == 0 && event.pos.x >=0 && event.pos.y >=0) {
                    // 按下
                    old_x = event.pos.x; // 更新前一次坐标
                    old_y = event.pos.y; // 更新前一次坐标
                    old_value = 1; //按下时
                }
                else if (key_left == 1 || key_right == 1 || event.id == 0) {
                    // 按下后滑动
                    rel_x = event.pos.x - old_x;
                    rel_y = event.pos.y - old_y;

                    if (abs(rel_x) > 10 || abs(rel_y) > 10) {

                        printf("rel_x = %d\n", rel_x);
                        printf("rel_y = %d\n", rel_y);

                        unsigned char hid_data[4];
                        memset(hid_data, 0, sizeof(hid_data));
                        hid_data[1] = rel_x;
                        hid_data[2] = rel_y;

                        if (event.id == 1 && key_left == 1)
                            hid_data[0] = 0x01;

                        else if (event.id == 1 && key_right == 1)
                            hid_data[0] = 0x03;

                        gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

                        old_x = event.pos.x; // 更新前一次数据
                        old_y = event.pos.y; // 更新前一次数据
                    }
                }
            }
        } else {
            unsigned char hid_data[4];
            memset(hid_data, 0, sizeof(hid_data));
            hid_data[0] = 0x00;
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
            key_left = key_right = 0;
            old_value = 0;
        }
    }

    input_close(handle);
    goodix_touch_deinit(&gt9xx);
    // 关闭触摸屏
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = NULL,
};

int gadget_usb_hid_mouse_test(void)
{
    gadget_hid_init(&usb_id, &hid_report, &hid_cb);
    thread_create("usb_gadget_hid_mouse_thread", 8192, usb_gadget_hid_thread, NULL);
    return 0;
}
