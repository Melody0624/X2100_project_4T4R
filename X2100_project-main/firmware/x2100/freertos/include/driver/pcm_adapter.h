#ifndef _PCM_ADAPTER_H_
#define _PCM_ADAPTER_H_

#include <driver/pcm.h>

struct pcm_adapter_param {
    int channels;
    pcm_data_fmt data_fmt;
    pcm_sample_rate sample_rate;
};

struct pcm_adapter;

/**
 * @brief 释放适配器
 * @param adapter 适配器结构体, 包含通道数,数据格式,设备结构体
 */
void pcm_adapter_release(struct pcm_adapter *adapter);

/**
 * @brief 创建适配器, 传入音频数据通道数,数据格式
 * @param aic_dev 设备结构体
 * @param adapter_param 适配器参数结构体, 包含音频数据源的通道数和数据格式
 * @return 返回适配器结构体
 */
struct pcm_adapter *pcm_adapter_create(struct pcm_device *aic_dev, struct pcm_adapter_param *adapter_param);

/**
 * @brief 将音频数据经过适配器写入设备, 若 音频通道数及数据格式 与 实际设备设置 的不一致,将音频数据转换后传入设备, 超时退出
 * @param adapter 适配器结构体
 * @param buf 写入的音频数据存放地址
 * @param frame_count 音频帧数
 * @param timeout_ms 超时时间, 单位为ms
 * @return 成功返回帧数, 失败返回负数
 */
int pcm_adapter_write_frame_timeout(struct pcm_adapter *adapter, void *buf, int frame_count, unsigned int timeout_ms);

/**
 * @brief 将音频数据经过适配器写入设备, 若 音频通道数及数据格式 与 实际设备设置 的不一致,将音频数据转换后传入设备
 * @param adapter 适配器结构体
 * @param buf 写入的音频数据存放地址
 * @param frame_count 音频帧数
 * @return 成功返回帧数, 失败返回负数
 */
int pcm_adapter_write_frame(struct pcm_adapter *adapter, void *buf, int frame_count);

#endif /* _PCM_ADAPTER_H_ */