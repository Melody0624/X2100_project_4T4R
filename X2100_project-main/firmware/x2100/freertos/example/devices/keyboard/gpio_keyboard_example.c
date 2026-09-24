#include <devices/gpio_keyboard.h>
#include <common.h>
#include <stdio.h>
#include <errno.h>
#include <driver/input.h>
#include <os.h>
#include <driver/gpio.h>

void gpio_keyboard_test(void)
{
    int ret;
    struct input_handle *handle;
    struct input_event event;
    struct gpio_keyboard_data *gpio_keyboard;

    /* 初始化独立按键 */
    gpio_keyboard_init();

    /* 开启独立按键 */
    handle = input_open("gpio_keyboard");

    /* 当按下键值为 KEY_HOME 的按键时就停止读值 */
    while (1) {
        ret = input_read(handle, &event, 5000);

        if (ret == -ETIMEDOUT) {
            continue;
        } else if (ret < 0) {
            printf("read key failure ret == %d", ret);
            break;
        }

        printf("code == %d   value == %d\n", event.code, event.value);

        if (event.code == KEY_HOME)
            break;
    }

    input_close(handle);
    gpio_keyboard_deinit();

    return;
}