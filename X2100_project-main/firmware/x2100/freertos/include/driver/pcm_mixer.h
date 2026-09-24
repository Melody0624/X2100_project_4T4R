#ifndef _PCM_MIXER_H_
#define _PCM_MIXER_H_

#include <driver/pcm.h>

struct pcm_mixer {
    float F; // 音频数据系数, 混合出的数据乘以该值进行缩放到合理范围
    int step; // 步进分频系数, count达到阈值 且 F不大于0.999时, F步进 (1 - F) / step
    int count; // F使用的次数, 使用F处理的帧次数, F改变count清零
    int threshold; // F使用的次数阈值, count达到该阈值重新计算F精度
};

/**
 * @brief 初始化音频数据混合结构体
 * @param m 音频数据混合结构体
 * @param step 步进分频系数
 */
void pcm_mixer_init(struct pcm_mixer *m, int step);

/**
 * @brief 设定F使用的次数阈值
 * @param m 音频数据混合结构体
 * @param threshold 设定F使用的次数阈值
 */
void pcm_mixer_set_threshold(struct pcm_mixer *m, int threshold);

/**
 * @brief 音频数据混合, 将多个混音设备传入的数据相加乘以合适的浮点数,使得输出数据处于合理范围内[-32767,32768]
 * @param m 混合结构体, 包含音频数据系数,步进分频系数,F使用的次数,F使用的次数阈值
 * @param inputs 需要混合的音频源的数组地址
 * @param out 混合出的音频数据存放地址
 * @param input_nums 需要混合的音频源数量
 * @param channel_nums 通道数
 * @param sample_nums 音频帧数
 */
void pcm_mixer_mix(
    struct pcm_mixer *m, short **inputs, short *out,
    int input_nums, int channel_nums, int sample_nums);

struct pcm_mixer_dev;

struct pcm_mixer_server;

/**
 * @brief 混音播放服务器创建, 依据实际设备创建混音播放服务器
 * @param dai 设备结构体
 * @param channels 通道数
 * @param period_samples 音频帧数
 * @return 返回混音播放服务器结构体
 */
struct pcm_mixer_server *pcm_mixer_server_create(
    struct pcm_device *dai, int channels, int period_samples);

/**
 * @brief 删除混音播放服务器
 * @param m 混音播放服务器结构体
 */
void pcm_mixer_server_delete(struct pcm_mixer_server *m);

/**
 * @brief 混音播放设备创建, 一个混音播放服务器最多支持8个混音播放设备
 * @param m 混音播放服务器结构体
 * @param name 设备名称
 * @param buf_size 数据缓冲区大小
 * @return 返回混音播放设备结构体
 */
struct pcm_mixer_dev *pcm_mixer_dev_create(
    struct pcm_mixer_server *m, const char *name, int buf_size);

/**
 * @brief 删除混音播放设备
 * @param dev 混音播放设备结构体
 */
void pcm_mixer_dev_delete(struct pcm_mixer_dev *dev);


#endif /* _PCM_MIXER_H_ */
