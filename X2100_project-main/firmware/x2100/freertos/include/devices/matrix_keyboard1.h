#ifndef _MAT_KEYBOARD1_H_
#define _MAT_KEYBOARD1_H_
#include <driver/input_key.h>

struct matrix_keyboard1_config {
    int *code;                      /* 存放矩阵键盘键值 */
    char *keyboard_name;            /* 矩阵键盘设备名称 */
    int max_event_count;            /* 存放键值的最大缓冲区 */
    unsigned int debounce_time_ms;  /* 按键消抖时间 */
    int use_extern_pull_up_resist;  /* 是否使用外部上拉电阻 */

    unsigned int *row_gpio;         /* 存放所有行的gpio */
    unsigned int *col_gpio;         /* 存放所有列的gpio */
    unsigned int row_gpio_count;    /* 行数 */
    unsigned int col_gpio_count;    /* 列数 */
};

/* 使用方案一键盘
 *
 * 功能特性：同一时间只能检测一个按键。
 * 硬件连接：矩阵键盘原理图在freertos/drivers/keyboard/matrix_keyboard1_sch.png中。
 *      将列作为输入，并置外部上拉电阻，行作为输出。
 *      (如果没有外部上拉电阻则要求所有列引脚具有内部上拉能力)。
 *
 * 原理：将列引脚设为下降沿中断触发，得到下降沿触发时就通知键盘开始扫描。
 *      扫描方式为：先将所有行设为高电平，再逐一将每一行置低电平（置某一行为低电平时，其他行为高电平），
 *                然后再扫描列的电平，哪一列的电平被拉低了，就说明这一行这一列的按键被按下了，
 *                反之就是没有被按下或已经释放了。
 *      例如：当我置第一行为低电平时，这时再扫描列，扫描到第二列时发现列引脚为低电平，说明这一行被拉低了，
 *           说明第一行第二列的按键被按下。
 */

struct matrix_keyboard1_data;

struct matrix_keyboard1_data * matrix_keyboard1_init(struct matrix_keyboard1_config *matrix_config);
void matrix_keyboard1_deinit(struct matrix_keyboard1_data *matrix);

#endif