#include <common.h>
#include <os.h>
#include <usb/gadget_hid.h>

#define KEYBOARD_REPORT_ID      1
#define MOUSE_REPORT_ID         2

/* 报表数据长度，主要用来申请端点大小，可以比实际通信数据大 */
#define REPORT_LENGTH       64

static const unsigned char keyboard_report[] = {
    /* keyboard_report */
    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop)              */
    0x09, 0x06,    /* USAGE (Keyboard)                       */
    0xa1, 0x01,    /* COLLECTION (Application)               */
    0x85, KEYBOARD_REPORT_ID,
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
    0xc0,        /* END_COLLECTION                         */

    /* mouse_report */
    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop Control)   */
    0x09, 0x02,    /* USAGE (Mouse)                          */
    0xa1, 0x01,    /* COLLECTION (Application)               */
    0x85, MOUSE_REPORT_ID,
    0x09, 0x01,    /* USAGE (Pointer)                        */
    0xa1, 0x00,    /* COLLECTION (Physical)                  */

    0x05, 0x09,    /* USAGE_PAGE (Button)                    */
    0x19, 0x01,    /* USAGE_MINIMUM (1)                      */
    0x29, 0x03,    /* USAGE_MINIMUM (3)                      */
    0x15, 0x00,    /* LOGICAL_MINIMUM (0)                    */
    0x25, 0x01,    /* LOGICAL_MINIMUM (1)                    */
    0x75, 0x01,    /* REPORT_SIZE (1)                        */
    0x95, 0x03,    /* REPORT_COUNT (3)                       */
    0x81, 0x02,    /* INPUT (Data,Var,Abs)                   */

    0x75, 0x05,    /* REPORT_SIZE (5)                        */
    0x95, 0x01,    /* REPORT_COUNT (1)                       */
    0x81, 0x01,    /* INPUT (Constant)                       */

    0x05, 0x01,    /* USAGE_PAGE (Generic Desktop Control)   */
    0x09, 0x30,    /* USAGE (X)                              */
    0x09, 0x31,    /* USAGE (Y)                              */
    0x09, 0x38,    /* USAGE (Whell)                          */
    0x15, 0x81,    /* LOGICAL_MINIMUM (-127)                 */
    0x25, 0x7f,    /* LOGICAL_MINIMUM (127)                  */
    0x75, 0x08,    /* REPORT_SIZE (8)                        */
    0x95, 0x03,    /* REPORT_COUNT (3)                       */
    0x81, 0x06,    /* INPUT (Data, Var, Rel)                 */
    0xc0,               /* END_COLLECTION                    */
    0xc0                /* END_COLLECTION                    */
};

/* hid descriptor for a keyboard */
static const struct hid_report_descriptor hid_report = {
    .subclass        = 0, /* No subclass */
    .protocol        = 0, /* Keyboard */
    .report_length        = REPORT_LENGTH,
    .report_desc_length    = sizeof(keyboard_report),
    .report_desc        = keyboard_report
};

static const struct gadget_id usb_id = {
    .vendor_id     = 0x0525,
    .product_id    = 0xa4ac
};

static struct {
    u8 value;
    u8 flag;
} key_value[256] = {
    ['\n'] = { 0x28, 0 },
    ['\r'] = { 0x28, 0 },
    [' '] = { 0x2c, 0 },

    ['-'] = { 0x2d, 0 },
    ['='] = { 0x2e, 0 },
    ['['] = { 0x2f, 0 },
    [']'] = { 0x30, 0 },
    ['\\'] = { 0x31, 0 },
    [';'] = { 0x33, 0 },
    ['\''] = { 0x34, 0 },
    ['`'] = { 0x35, 0 },
    [','] = { 0x36, 0 },
    ['.'] = { 0x37, 0 },
    ['/'] = { 0x38, 0 },

    ['_'] = { 0x2d, 1 },
    ['+'] = { 0x2e, 1 },
    ['{'] = { 0x2f, 1 },
    ['}'] = { 0x30, 1 },
    ['|'] = { 0x31, 1 },
    [':'] = { 0x33, 1 },
    ['"'] = { 0x34, 1 },
    ['~'] = { 0x35, 1 },
    ['<'] = { 0x36, 1 },
    ['>'] = { 0x37, 1 },
    ['?'] = { 0x38, 1 },

    ['1'] = { 0x1e, 0 },
    ['2'] = { 0x1f, 0 },
    ['3'] = { 0x20, 0 },
    ['4'] = { 0x21, 0 },
    ['5'] = { 0x22, 0 },
    ['6'] = { 0x23, 0 },
    ['7'] = { 0x24, 0 },
    ['8'] = { 0x25, 0 },
    ['9'] = { 0x26, 0 },
    ['0'] = { 0x27, 0 },

    ['!'] = { 0x1e, 1 },
    ['@'] = { 0x1f, 1 },
    ['#'] = { 0x20, 1 },
    ['$'] = { 0x21, 1 },
    ['%'] = { 0x22, 1 },
    ['^'] = { 0x23, 1 },
    ['&'] = { 0x24, 1 },
    ['*'] = { 0x25, 1 },
    ['('] = { 0x26, 1 },
    [')'] = { 0x27, 1 },

    ['a'] = { 0x4, 0 },
    ['b'] = { 0x5, 0 },
    ['c'] = { 0x6, 0 },
    ['d'] = { 0x7, 0 },
    ['e'] = { 0x8, 0 },
    ['f'] = { 0x9, 0 },
    ['g'] = { 0xa, 0 },
    ['h'] = { 0xb, 0 },
    ['i'] = { 0xc, 0 },
    ['j'] = { 0xd, 0 },
    ['k'] = { 0xe, 0 },
    ['l'] = { 0xf, 0 },
    ['m'] = { 0x10, 0 },
    ['n'] = { 0x11, 0 },
    ['o'] = { 0x12, 0 },
    ['p'] = { 0x13, 0 },
    ['q'] = { 0x14, 0 },
    ['r'] = { 0x15, 0 },
    ['s'] = { 0x16, 0 },
    ['t'] = { 0x17, 0 },
    ['u'] = { 0x18, 0 },
    ['v'] = { 0x19, 0 },
    ['w'] = { 0x1a, 0 },
    ['x'] = { 0x1b, 0 },
    ['y'] = { 0x1c, 0 },
    ['z'] = { 0x1d, 0 },

    ['A'] = { 0x4, 1 },
    ['B'] = { 0x5, 1 },
    ['C'] = { 0x6, 1 },
    ['D'] = { 0x7, 1 },
    ['E'] = { 0x8, 1 },
    ['F'] = { 0x9, 1 },
    ['G'] = { 0xa, 1 },
    ['H'] = { 0xb, 1 },
    ['I'] = { 0xc, 1 },
    ['J'] = { 0xd, 1 },
    ['K'] = { 0xe, 1 },
    ['L'] = { 0xf, 1 },
    ['M'] = { 0x10, 1 },
    ['N'] = { 0x11, 1 },
    ['O'] = { 0x12, 1 },
    ['P'] = { 0x13, 1 },
    ['Q'] = { 0x14, 1 },
    ['R'] = { 0x15, 1 },
    ['S'] = { 0x16, 1 },
    ['T'] = { 0x17, 1 },
    ['U'] = { 0x18, 1 },
    ['V'] = { 0x19, 1 },
    ['W'] = { 0x1a, 1 },
    ['X'] = { 0x1b, 1 },
    ['Y'] = { 0x1c, 1 },
    ['Z'] = { 0x1d, 1 },
};

int gadget_keyboard_write_string(const char *buf, u32 len, u8 caps_led_state)
{
    int i;
    u8 data, value, led_flag;
    unsigned char hid_data[9];

    if (buf == NULL || len == 0)
        return -EINVAL;

    for (i = 0; i < len; i++) {
        data = buf[i];
        if (data == 0)
            continue;

        value = key_value[data].value;
        led_flag = key_value[data].flag;

        if (value == 0) {
            value = key_value['?'].value;
            led_flag = key_value['?'].flag;
        }

        memset(hid_data, 0, sizeof(hid_data));
        /* 数据第一个字节是报表ID */
        hid_data[0] = KEYBOARD_REPORT_ID;
        hid_data[1] = (led_flag ^ caps_led_state) ? 2 : 0;
        hid_data[3] = value;
        gadget_hid_write(hid_data, sizeof(hid_data), 1, 500);


        memset(hid_data, 0, sizeof(hid_data));
        hid_data[0] = KEYBOARD_REPORT_ID;
        gadget_hid_write(hid_data, sizeof(hid_data), 1, 500);
    }

    return 0;
}

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static const char *usb_hid_char = "  INGENIC hid test\r\n";
static void usb_gadget_hid_thread(void *data)
{
    int len;
    u8 led_state = 0;
    unsigned char hid_data[REPORT_LENGTH];

    while (1) {
        msleep(1000);
        len = gadget_hid_read(hid_data, sizeof(hid_data), 0, 0);
        printf("len %d\n", len);
        if (len > 1) {
            /* 数据第一个字节是报表ID */
            if (hid_data[0] == KEYBOARD_REPORT_ID) {
                led_state = hid_data[1] & BIT(1);
                printf("usb hid keyboard read data 0x%x\n", hid_data[1]);
            }
        }

        gadget_keyboard_write_string(usb_hid_char, strlen(usb_hid_char), led_state);
    }
}

#define ONE_MOVE_DISTANCE 1
#define MOVE_STEPS 400

static void usb_gadget_hid_mouse_thread(void *data)
{
    int cnt;
    unsigned char hid_data[5];
    while(1) {
        memset(hid_data, 0, sizeof(hid_data));
        /* 数据第一个字节是报表ID */
        hid_data[0] = MOUSE_REPORT_ID;
        hid_data[3] = ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        msleep(1000);

        memset(hid_data, 0, sizeof(hid_data));
        /* 数据第一个字节是报表ID */
        hid_data[0] = MOUSE_REPORT_ID;
        hid_data[2] = ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        msleep(1000);

        memset(hid_data, 0, sizeof(hid_data));
        /* 数据第一个字节是报表ID */
        hid_data[0] = MOUSE_REPORT_ID;
        hid_data[3] = -ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        msleep(1000);

        memset(hid_data, 0, sizeof(hid_data));
        /* 数据第一个字节是报表ID */
        hid_data[0] = MOUSE_REPORT_ID;
        hid_data[2] = -ONE_MOVE_DISTANCE;
        for (cnt = 0; cnt < MOVE_STEPS; cnt++)
            gadget_hid_write(hid_data, sizeof(hid_data), 1, THREAD_COND_TIMEOUT_NO_LIMIT_MS);

        msleep(1000);
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
    thread_create("usb_gadget_hid_mouse_thread", 8192, usb_gadget_hid_mouse_thread, NULL);

    return 0;
}
