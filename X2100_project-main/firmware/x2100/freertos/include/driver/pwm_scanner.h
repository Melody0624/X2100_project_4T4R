#ifndef __PWM_SCANNER_H__
#define __PWM_SCANNER_H__

/**
 * pwm_scanner 驱动暂不开源
 * 扫描驱动本质上是多路时钟同步输出，并且需要支持相位差以及不同频率
 * 先实现pwm_scanner，pwm 要支持相位差只能用dma模式
 * dma模式下的pwm输出最大只能到12M/s 或者更小，无需配置相位差的无此限制，
 * 若后续需要更高速率的时钟输出，可添加slcd_scanner 以及 nemc_scanner 来实现
 **/

#include <common.h>
#include <bit_field.h>

#define MAX_DEVICE_MULTI_CLK_CNT 8
#define MAX_DEVICE_CIS_LED_CNT   4

struct multi_clk_sync_config {
    char name[64];                                                 /*名字，唯一标识符，暂无用处*/
    int gpio;                                                      /*时钟输出引脚，目前一定需要是pwm的功能引脚*/
    unsigned int freq;                                             /*输出时钟频率*/
    int idle_level;                                                /*io空闲引脚电平*/
    unsigned int duty;                                             /*占空比1~99*/
    uint64_t delay_ns;                                             /*延迟启动时间，主要用来配置相位差*/
};

struct cis_led_config {
    char name[64];                                                /*名字，唯一标识符，暂无用处*/
    int gpio;                                                     /*灯的引脚*/
    int active_level;                                             /*灯的有效值*/
    unsigned int brightness;                                      /*灯的亮度设置*/
    unsigned int max_brightness;                                  /*灯的可设最大值*/
};

struct afe_device_data {
    char name[64];                                                  /*名字，唯一标识符，暂无用处*/
    int channels;                                                   /*通道数，需要与cis 通道数匹配*/

    struct multi_clk_sync_config adcclk;                            /*afe 采样时钟配置*/

    int clk_cnt;                                                    /*需要额外时钟配置个数*/
    struct multi_clk_sync_config clk[MAX_DEVICE_MULTI_CLK_CNT];     /*需要额外配置的时钟*/

    int (*power_on)(void);                                          /*电源使能回调函数*/
    void (*power_off)(void);                                        /*电源失能回调函数*/

    int (*stream_on)(void);                                         /*开流回调函数*/
    void (*stream_off)(void);                                       /*关流回调函数*/
};

struct cis_device_data {
    char name[64];                                                          /*名字，唯一标识符，暂无用处*/
    int channels;                                                           /*通道数，需要与afe的通道数一致*/

    int led_clk_cnt;                                                        /*需配置的光源时钟个数*/
    struct multi_clk_sync_config mclk;                                      /*设备主时钟配置，配置的cycle都以该时钟频率为单位*/
    struct cis_led_config led_config[MAX_DEVICE_CIS_LED_CNT];               /*光源时钟配置*/

    int tr_gpio;                                                            /*也叫start_pulse 引脚。一般用来同步以及做分辨率选择。需要同时连接到dvp的Vsync引脚*/
    int tr_cycle;                                                           /*tr 高电平cycle数, (分辨率选择，根据设备配置)*/

    int smargin_cycle;                                                      /*前沿时钟cycle数，根据设备配置。(分辨率选择周期+准备时钟+参考电平时钟)*/
    int pixel_cycle;                                                        /*单通道的有效像素时钟个数*/
    int emargin_cycle;                                                      /*后沿时钟个数*/

    int (*power_on)(void);                                                  /*电源使能回调函数*/
    void (*power_off)(void);                                                /*电源失能回调函数*/

    int (*stream_on)(void);                                                 /*开流回调函数*/
    void (*stream_off)(void);                                               /*关流回调函数*/

    struct cis_dpi_config *dpi_table;                                       /*dpi配置table*/
    unsigned int dpi_table_size;                                            /*dpi table 的大小*/
};

/**
 * @brief 注册afe 设备
 * @param afe_dev afe 设备配置信息
 * @return 成功返回0，失败返回负数
 */
int pwm_scanner_register_afe(struct afe_device_data *afe_dev);

/**
 * @brief 注销afe 设备
 * @param afe_dev 已注册的afe 设备配置信息
 * @return 无
 */
void pwm_scanner_unregister_afe(struct afe_device_data *afe_dev);

/**
 * @brief 注册cis 设备
 * @param cis_dev cis 设备配置信息
 * @return 成功返回0，失败返回负数
 */
int pwm_scanner_register_cis(struct cis_device_data *cis_dev);

/**
 * @brief 注销cis 设备
 * @param afe_dev 已注册的cis 设备配置信息
 * @return 无
 */
void pwm_scanner_unregister_cis(struct cis_device_data *cis_dev);

/**
 * @brief 运行时设置输入格式与HSYNC GPIO
 * @param fmt 输入格式,与CONFIG_PWM_SCANNER_INPUT_FMT一致
 * @param gpio HSYNC GPIO
 */
void pwm_scanner_set_config(unsigned int fmt, int gpio);

/**
 * @brief 运行时开关LED输出（需在stream_on前设置）
 * @param enable 1开灯 0关灯
 * @return 0 成功，<0 失败
 */
int pwm_scanner_set_led_enable(int enable);

/**
 * @brief 运行时设置LED亮度（需在stream_on前设置）
 * @param r 红灯亮度
 * @param g 绿灯亮度
 * @param b 蓝灯亮度
 * @return 0 成功，<0 失败
 */
int pwm_scanner_set_led_brightness(unsigned int r, unsigned int g, unsigned int b);

/**
 * @brief 初始化pwm scanner 驱动
 * @param void 无
 * @return 初始化成功与否
 *          < 0  失败
 *          > 0  成功
 */
int pwm_scanner_init(void);

#endif
