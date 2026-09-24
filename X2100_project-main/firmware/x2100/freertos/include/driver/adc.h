#ifndef _ADC_H_
#define _ADC_H_

#include <errno.h>

void adc_init(void);    // 使能adc时钟、初始化控制器、并申请adc中断
int adc_read_data(unsigned int channel);    // channel:     0:AUX0  1:AUX1  2:AUX2
void adc_deinit(void);      // 释放中断、失能adc控制器、失能adc时钟
unsigned long adc_clk_get_rate(void);  // 获取 adc clk rate

typedef void (*adc_irq_cb_t) (void);
void adc_set_irq_cb(adc_irq_cb_t cb_func); // 设置多通道采样时的中断回调
void adc_start_channels_sampling(unsigned int channels); // 开启多通道采样
int adc_read_raw_channel_data(unsigned int channel); // 在回调中读取多通道采样后的具体通道数据
void adc_enable_repeat_sampling(int enable); // 是否自动重启采样

void adc_enable_poll_mode(void);  // 打开 adc 轮询模式
void adc_disable_poll_mode(void); // 关闭 adc 轮询模式
int adc_read_data_poll(unsigned int channel); // 使用 轮询模式 采样adc 通道的值

#endif
