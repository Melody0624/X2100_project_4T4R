#include <common.h>
#include <stdio.h>
#include <errno.h>
#include <driver/input_key.h>
#include <driver/input.h>
#include <os.h>
#include <driver/gpio.h>
#include <devices/matrix_keyboard2.h>
#include <usb/gadget_hid.h>
#include "hid_key_event.h"

#define M_QUIT 0xff

static const unsigned char keyboard_report[] = {
    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop)              */
    0x09, 0x06,    /* USAGE (Keyboard)                       */
    0xa1, 0x01,    /* COLLECTION (Application)               */
    0x05, 0x07,    /*   USAGE_PAGE (Keyboard)                */
    0x19, 0xe0,    /*   USAGE_MINIMUM (Keyboard LeftControl) */
    0x29, 0xe7,    /*   USAGE_MAXIMUM (Keyboard Right GUI)   */
    0x15, 0x00,    /*   LOGICAL_MINIMUM (0)                  */
    0x25, 0x01,    /*   LOGICAL_MAXIMUM (1)                  */
    0x75, 0x01,    /*   REPORT_SIZE (1)                      */
    0x95, 0x08,    /*   REPORT_COUNT (8)                     */
    0x81, 0x02,    /*   INPUT (Data,Var,Abs)                 */
    0x95, 0x01,    /*   REPORT_COUNT (1)                     */
    0x75, 0x08,    /*   REPORT_SIZE (8)                      */
    0x81, 0x03,    /*   INPUT (Cnst,Var,Abs)                 */
    0x95, 0x05,    /*   REPORT_COUNT (5)                     */
    0x75, 0x01,    /*   REPORT_SIZE (1)                      */
    0x05, 0x08,    /*   USAGE_PAGE (LEDs)                    */
    0x19, 0x01,    /*   USAGE_MINIMUM (Num Lock)             */
    0x29, 0x05,    /*   USAGE_MAXIMUM (Kana)                 */
    0x91, 0x02,    /*   OUTPUT (Data,Var,Abs)                */
    0x95, 0x01,    /*   REPORT_COUNT (1)                     */
    0x75, 0x03,    /*   REPORT_SIZE (3)                      */
    0x91, 0x03,    /*   OUTPUT (Cnst,Var,Abs)                */
    0x95, 0x06,    /*   REPORT_COUNT (6)                     */
    0x75, 0x08,    /*   REPORT_SIZE (8)                      */
    0x15, 0x00,    /*   LOGICAL_MINIMUM (0)                  */
    0x25, 0x65,    /*   LOGICAL_MAXIMUM (101)                */
    0x05, 0x07,    /*   USAGE_PAGE (Keyboard)                */
    0x19, 0x00,    /*   USAGE_MINIMUM (Reserved)             */
    0x29, 0x65,    /*   USAGE_MAXIMUM (Keyboard Application) */
    0x81, 0x00,    /*   INPUT (Data,Ary,Abs)                 */
    0xc0        /* END_COLLECTION                         */
};

/* hid descriptor for a keyboard */
static const struct hid_report_descriptor hid_report = {
    .subclass        = 0, /* No subclass */
    .protocol        = 1, /* Keyboard */
    .report_length        = 8,
    .report_desc_length    = sizeof(keyboard_report),
    .report_desc        = keyboard_report
};

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4ac
};

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

/* 配置行引脚，从第一行到到最后一行依次配置 */
static unsigned int row_gpio[] = {GPIO_PA(0), GPIO_PA(1), GPIO_PA(2), GPIO_PA(3), GPIO_PA(4)};
/* 配置列引脚，从第一列到到最后一列依次配置 */
static unsigned int col_gpio[] = {GPIO_PA(5), GPIO_PA(6), GPIO_PA(7), GPIO_PA(8), GPIO_PA(9)};

static int keyboard_value[5*5] = {
    USB_A, USB_B, USB_C, USB_D, USB_E,
    USB_F, USB_G, USB_H, USB_I, USB_J,
    USB_K, USB_L, USB_M, USB_N, USB_O,
    USB_P, USB_Q, USB_R, USB_S, USB_T,
    USB_LEFTCTRL, USB_LEFTSHIFT, USB_LEFTALT, USB_LEFTWINDOW, M_QUIT
};

static void usb_keyboard_thread(void *data)
{
    int ret;
   struct hid_key_event hid_key;
    struct input_handle *handle;
    struct input_event event;
    struct matrix_keyboard2_data *matrix;
    struct matrix_keyboard2_config matrix_config;

    /* 配置矩阵键盘键值 */
    matrix_config.row_gpio_count = ARRAY_SIZE(row_gpio);
    matrix_config.col_gpio_count = ARRAY_SIZE(col_gpio);
    matrix_config.row_gpio = row_gpio;
    matrix_config.col_gpio = col_gpio;
    matrix_config.code = keyboard_value;
    matrix_config.keyboard_name = "usb_keyboard";
    matrix_config.max_event_count = 32;
    matrix_config.debounce_time_ms = 10;
    matrix_config.use_extern_pull_down_resist = 1;
    matrix = matrix_keyboard2_init(&matrix_config);

    handle = input_open("usb_keyboard");

    hid_key_event_init(&hid_key);
    while (1) {
        ret = input_read(handle, &event, 5000);
        if (ret == -ETIMEDOUT) {
            continue;
        } else if (ret < 0) {
            printf("read key failure ret == %d", ret);
            break;
        }

        if (event.code == M_QUIT)
            break;
        hid_key_event_add(&hid_key, event.code, event.value);

        gadget_keyboard_report_event(&hid_key);
    }

    input_close(handle);
    hid_key_event_init(&hid_key);
    gadget_keyboard_report_event(&hid_key);
    matrix_keyboard2_deinit(matrix);
    return;
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = NULL,
};

void keyboard_init(void)
{
    gadget_hid_init(&usb_id, &hid_report, &hid_cb);
    thread_create("usb_matrix_keyboard", 8192, usb_keyboard_thread, NULL);
}


