#include <common.h>
#include <errno.h>
#include <os.h>
#include <driver/irq.h>
#include <driver/gpio.h>
#include <usb/usb.h>
#include <usb/gadget_hid_wakeup.h>
#include <wake_lock.h>
#include <driver/pm.h>

#define BUTTION_GPIO                GPIO_PB(28)

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
    int i, ret;
    u8 data, value, led_flag;
    u8 hid_data[3][8];
    unsigned int num = 0;

    if (buf == NULL || len == 0)
        return -EINVAL;

    memset(hid_data, 0, sizeof(hid_data));

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

        hid_data[num][0] = (led_flag ^ caps_led_state) ? 2 : 0;
        hid_data[num][2] = value;
        ret = gadget_hid_write(&hid_data[num][0], 8, 1, 500);
        if (ret != 8)
            return ret;

        ret = gadget_hid_write(&hid_data[2][0], 8, 1, 5000);
        if (ret != 8)
            return ret;

        num = (num + 1) % 2;
    }

    return 0;
}

static void hid_connect_callback(int connect)
{
    printf("hid_connect_callback %d\n", connect);
}

static int system_sleep_flag;
static thread_waiter_t system_sleep_waiter;

static void hid_suspend_callback(int suspend)
{
    printf("hid_suspend_callback %d\n", suspend);

    system_sleep_flag = suspend;

    if (suspend)
        thread_waiter_wakeup(&system_sleep_waiter);
}

static void system_sleep_thread(void *data)
{
    while (1) {
        thread_waiter_wait(&system_sleep_waiter);

        if (system_sleep_flag) {
wait_wake_lock:
            os_enter_critical();

            if (wake_lock_wait_timeout(THREAD_COND_TIMEOUT_NO_LIMIT_MS)) {
                printf("Enter sleep timeout, maybe some wake_lock locked!\n");
                wake_locks_show();
                os_exit_critical();
                goto wait_wake_lock;
            }

            if (system_sleep_flag) {
                usb_core_suspend();

                pm_enter_sleep();

                usb_core_resume();
            }

            os_exit_critical();
        }
    }

}

static int send_flag;
static thread_waiter_t send_waiter;
static void button_gpio_irq_handler(int irq, void *data)
{
    printf("button irq: %d %d\n", irq, gpio_get_value(irq_to_gpio(irq)));
    send_flag = 1;
    thread_waiter_wakeup(&send_waiter);
}

static const char *usb_hid_char = "INGENIC hid test\r\n";
static void usb_gadget_hid_thread(void *data)
{
    int ret;
    int len, i;
    u8 led_state = 0;
    unsigned char hid_data[8];

    while (1) {
        thread_waiter_wait(&send_waiter);

        if (send_flag) {
            len = gadget_hid_read(hid_data, sizeof(hid_data), 0, 0);
            if (len > 0) {
                for(i = 0; i < len; i++) {
                    if (hid_data[i] & BIT(1))
                        led_state = 1;
                    else
                        led_state = 0;

                    printf("usb hid read data %d\n", hid_data[i]);
                }
            }

            ret = gadget_keyboard_write_string(usb_hid_char, strlen(usb_hid_char), led_state);
            if (ret == -EHOSTDOWN) {
                gadget_hid_wakeup();
                /* 唤醒后必须有数据,否则部分电脑将再次休眠 */
                gadget_keyboard_write_string(usb_hid_char, strlen(usb_hid_char), led_state);
            }
            send_flag = 0;
        }


    }
}

static struct hid_callback hid_cb = {
    .connect_cb = hid_connect_callback,
    .request_cb = NULL,
};

int gadget_usb_hid_wakeup_sleep_test(void)
{
    thread_waiter_init(&system_sleep_waiter);
    /* init usb driver */
    gadget_hid_wakeup_init(&usb_id, &hid_report, &hid_cb, hid_suspend_callback);
    thread_create("usb gadget hid thread", 1024, usb_gadget_hid_thread, NULL);
    thread_create("system sleep thread", 1024, system_sleep_thread, NULL);

    thread_waiter_init(&send_waiter);
    /* request button irq */
    assert(!gpio_request(BUTTION_GPIO, "button"));
    gpio_set_func(BUTTION_GPIO , (GPIO_INPUT | GPIO_PULL_HIZ));
    request_irq(gpio_to_irq(BUTTION_GPIO), IRQ_TYPE_EDGE_FALLING, button_gpio_irq_handler, "button", NULL);

    return 0;
}
