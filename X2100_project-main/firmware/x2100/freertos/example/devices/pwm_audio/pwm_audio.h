#ifndef _PWM_AUDIO_H_
#define _PWM_AUDIO_H_

struct pwm_audio_dev;

/* 缓存区不够最小播放单位的数据,补零强制播放 */
void pwm_audio_data_flush(struct pwm_audio_dev *dev);

/*
    单声道,有符号16bit,小端,音频数据写入
        sampling_rate: 采样率
        data: 数据
        data_count: 数据个数
 */
int pwm_audio_mono_s16le_write(struct pwm_audio_dev *dev, unsigned int sampling_rate, const short *data, unsigned int data_count);

/* 初始化pwm_audio
    pwm_gpio: pwm对应的gpio编号
    base_freq: 音频载波基频
        大于或等于采样率， 并可以整除采样率
        要求小于 100kHz，由于pwm用单次dma模式，频率过快会出现头尾不相连
        如果出现pwm数据count过大，需要降低基频，或减少最小音频时间。
        参考值: 96000
    unit_time: 最小的音频时间  单位ms
        如果写入数据小于最小时间，会等待写入数据满足最小时间单位才会播放
        如果需要强制播出，调用pwm_audio_data_flush，自动补零并播放
        参考值: 10 20 50
    buf_count: 数据缓存区个数  单位最小音频时间
        最少两个缓冲区，不然可能会出现断音现象
        参考值: 根据实际使用场景设置，如果解码时间较长可以适当增加缓存
 */
struct pwm_audio_dev *pwm_audio_init(int pwm_gpio, unsigned int base_freq, unsigned int unit_time, unsigned int buf_count);

#endif