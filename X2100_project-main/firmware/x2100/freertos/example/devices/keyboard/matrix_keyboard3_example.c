#include <devices/matrix_keyboard3.h>
#include <common.h>
#include <stdio.h>
#include <errno.h>
#include <driver/input.h>
#include <os.h>
#include <driver/gpio.h>

#define COLS 4
#define ROWS 4

void matrix_keyboard3_test(void)
{
    int ret;
    struct input_handle *handle;
    struct input_event event;
    struct matrix_keyboard3_data *matrix;
    struct matrix_keyboard3_config matrix_config;

    /* 配置行引脚，从第一行到到最后一行依次配置 */
    unsigned int row_gpio[ROWS] = {GPIO_PA(0), GPIO_PA(1), GPIO_PA(2), GPIO_PA(3)};
    /* 配置列引脚，从第一列到到最后一列依次配置 */
    unsigned int col_gpio[COLS] = {GPIO_PA(5), GPIO_PA(6), GPIO_PA(7), GPIO_PA(8)};
    /* 配置矩阵键盘键值 */
    int code[COLS * ROWS] = {
        KEY_0, KEY_1, KEY_2, KEY_3,
        KEY_4, KEY_5, KEY_6, KEY_7,
        KEY_8, KEY_9, KEY_DOWN, KEY_RIGHT,
        KEY_LEFT, KEY_UP, KEY_HOME, KEY_MENU
    };
    matrix_config.row_gpio_count = ROWS; /* 配置行数 */
    matrix_config.col_gpio_count = COLS; /* 配置列数 */
    matrix_config.row_gpio = row_gpio;
    matrix_config.col_gpio = col_gpio;

    matrix_config.code = code;
    matrix_config.keyboard_name = "matrix3"; /* 设置矩阵键盘名称 */
    matrix_config.max_event_count = 32;      /* 设置存储键值的缓冲区 */
    matrix_config.debounce_time_ms = 10;     /* 设置消抖时间 */

    /* 设置使用外部上拉电阻， 如果不用外部上拉电阻，
     * 则要求所有行引脚有内部上拉能力
     */
    matrix_config.use_extern_pull_up_resist = 1;

    /* 初始化矩阵键盘 */
    matrix = matrix_keyboard3_init(&matrix_config);

    /* 开启矩阵键盘 */
    handle = input_open("matrix3");

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
    matrix_keyboard3_deinit(matrix);

    return;
}