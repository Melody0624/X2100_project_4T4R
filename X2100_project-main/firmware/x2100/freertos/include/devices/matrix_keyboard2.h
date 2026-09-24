#ifndef _MAT_KEYBOARD2_H_
#define _MAT_KEYBOARD2_H_
#include <driver/input_key.h>

struct matrix_keyboard2_config {
    int *code;                      /* 存放矩阵键盘键值 */
    char *keyboard_name;            /* 矩阵键盘设备名称 */
    int max_event_count;            /* 存放键值的最大缓冲区 */
    unsigned int debounce_time_ms;  /* 按键消抖时间 */
    int use_extern_pull_down_resist;  /* 是否使用外部下拉电阻 */

    unsigned int *row_gpio;         /* 存放所有行的gpio */
    unsigned int *col_gpio;         /* 存放所有列的gpio */
    unsigned int row_gpio_count;    /* 行数 */
    unsigned int col_gpio_count;    /* 列数 */
};

/* 使用方案二键盘
 *
 * 功能特性：可同时检测多个按键，
 * 硬件连接：矩阵键盘原理图在freertos/drivers/keyboard/matrix_keyboard2_sch.png中。
 *          将行作为输入，并置下拉电阻（如果没有外部下拉电阻则要求所有行引脚具有内部下拉能力），
 *          列作为输出通过二极管与开关连接（二极管正极在列引脚这一端）
 *
 * 原理：将行引脚设为上升沿中断触发，得到上升沿触发时就通知键盘开始扫描。
 *
 *          扫描方式为：先将所有列设为低电平，再逐一将每一列置高电平（置某一列为高电平时，其他列为低电平），
 *          然后再扫描行的电平，哪一行行的电平被拉高了，就说明这一行这一列的按键被按下了，
 *          反之就是没有被按下或已经释放了。
 *
 *          例如：当我置第一列为高电平时，这时再扫描行，扫描到第二行时发现行引脚为高电平，说明这一行被拉高了，
 *           说明第二行第一列的按键被按下。
 *
 *   二极管的作用：利用二极管的单向导通性 来避免如原理图中的任意三个按键同时被按下时
 *              会导致剩下的一个按键也被检测到按下。
 *
 */

struct matrix_keyboard2_data;

struct matrix_keyboard2_data * matrix_keyboard2_init(struct matrix_keyboard2_config *matrix_config);
void matrix_keyboard2_deinit(struct matrix_keyboard2_data *matrix);

#endif