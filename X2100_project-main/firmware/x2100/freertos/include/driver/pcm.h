#ifndef _PCM_H_
#define _PCM_H_

#include <os.h>
#include <wake_lock.h>

typedef enum {
    pcm_interface_i2s,
    pcm_interface_i2s_MSB, // i2s 右对齐
    pcm_interface_i2s_left_justified, // i2s 左对齐

    pcm_interface_dmic,

    pcm_interface_modeA, // PCM A-mode
    pcm_interface_modeB, //PCM B-mode

    pcm_interface_nums,
} pcm_interface;

typedef enum {
    pcm_fmt_S8,
    pcm_fmt_U8,
    pcm_fmt_S16LE,
    pcm_fmt_S16BE,
    pcm_fmt_U16LE,
    pcm_fmt_U16BE,
    pcm_fmt_S24LE,
    pcm_fmt_S24BE,
    pcm_fmt_U24LE,
    pcm_fmt_U24BE,
    pcm_fmt_S32LE,
    pcm_fmt_S32BE,
    pcm_fmt_U32LE,
    pcm_fmt_U32BE,

    pcm_fmt_nums,
} pcm_data_fmt;

typedef enum {
    pcm_rate_5512,
    pcm_rate_8000,
    pcm_rate_11025,
    pcm_rate_12000,
    pcm_rate_16000,
    pcm_rate_22050,
    pcm_rate_24000,
    pcm_rate_32000,
    pcm_rate_44100,
    pcm_rate_48000,
    pcm_rate_64000,
    pcm_rate_88200,
    pcm_rate_96000,
    pcm_rate_176400,
    pcm_rate_192000,
    pcm_rate_384000,

    pcm_rate_nums,
} pcm_sample_rate;

typedef enum {
    i2s_LR_mode,
    i2s_RL_mode,
} i2s_frame_mode;

typedef enum {
    i2s_bclk_codec_slave, // codec为从, 控制器向codec设备发出bclk
    i2s_bclk_codec_master, // codec为主, codec设备向控制器发出bclk
} i2s_bclk_direction;

typedef enum {
    i2s_frame_codec_slave, // codec为从, 控制器向codec设备发出sync
    i2s_frame_codec_master, // codec为主, codec设备向控制器发出sync
} i2s_frame_direction;

typedef enum {
    pcm_stream_playback,
    pcm_stream_capture,
} pcm_stream_type;

struct pcm_params {
    pcm_interface pcm_interface; // 数据传输模式
    i2s_frame_mode i2s_frame_mode; // 左右声道传输先后顺序
    i2s_bclk_direction i2s_bclk_direction; // bclk主从模式
    i2s_frame_direction i2s_frame_direction; // sync主从模式(一般与i2s_bclk_direction一致)

    pcm_data_fmt pcm_data_fmt; // 数据格式
    pcm_sample_rate pcm_sample_rate; // 采样率

    unsigned int channels; // 通道数
    unsigned int buffer_time_ms; // 处理一次音频数据缓冲区的时间, 以ms为单位
    unsigned int period_time_ms; // 处理音频数据的周期, 以ms为单位
};

struct pcm_dev_data {
    const char *name;
    pcm_stream_type stream_type; // 数据流类型
    unsigned long pcm_interface_list;
    unsigned long channels_list;
    unsigned long pcm_data_fmt_list;
    unsigned long pcm_sample_rate_list;
    unsigned char i2s_frame_mode_list;
    unsigned char i2s_bclk_direction_list;
    unsigned char i2s_frame_direction_list;
    int (*pcm_write_frame)(struct pcm_dev_data *dev,
                           void *buf, int frame_count, unsigned int timeout_ms);
    int (*pcm_read_frame)(struct pcm_dev_data *dev,
                          void *buf, int frame_count, unsigned int timeout_ms);
    int (*pcm_enable)(struct pcm_dev_data *dev, struct pcm_params *params);
    void (*pcm_disable)(struct pcm_dev_data *dev);
    int (*pcm_start)(struct pcm_dev_data *dev);
    void (*pcm_stop)(struct pcm_dev_data *dev);
    int (*pcm_set_mute)(struct pcm_dev_data *dev, int mute);
    int (*pcm_set_volume)(struct pcm_dev_data *dev, int val);
    int (*pcm_get_volume)(struct pcm_dev_data *dev);
    int (*pcm_private_ctrl)(struct pcm_dev_data *dev, const char *ctrl_id, unsigned long value);
    void (*pcm_remove)(struct pcm_dev_data *dev);

    void *drv_data;
};

struct pcm_device {
    struct pcm_dev_data *data;
    struct list_head link;
    struct mutex lock;
    struct wake_lock w_lock;
    struct pcm_params params;
    unsigned int volume;
    unsigned char is_enable;
    unsigned char is_start;
    unsigned char is_mute;
    unsigned char is_transfer;
    volatile unsigned char is_exiting;
};

/**
 * 音频数据由多帧数据组成, 一帧数据大小为 通道数 * 通道数据大小, 一个通道数据大小根据数据格式不同而不同
 * 例 音频数据格式为 S16_LE, 通道数为 1, 采样率为 16k
 * 1秒 音频数据大小 = 秒数 * 数据格式对应字节 * 通道数 * 采样率 = 1 * 2 * 1 * 16000
 */

/**
 * @brief 设备注册, 在初始化 aic/codec 根据实际注册,或者用于注册混音设备
 * @param data 设备数据结构体, 包含设备名字,数据流类型,可支持的类型列表,pcm回调函数指针
 * @return 返回设备结构体
 */
struct pcm_device *pcm_register(struct pcm_dev_data *data);

/**
 * @brief 注册混音设备且复制 实际设备dev->data(设备数据结构体:数据流类型,可支持的类型列表,pcm回调函数指针) 到 data
 *      例 创建混音播放设备时, 注册混音设备,复制实际设备的部分数据及回调函数指针 到 混音设备
 * @param dev 设备结构体
 * @param data 设备数据结构体, 包含设备名字,数据流类型,可支持的类型列表,pcm回调函数指针
 * @param name 设备名称
 * @return 返回设备结构体
 */
struct pcm_device *pcm_clone_device(struct pcm_device *dev, struct pcm_dev_data *data, const char *name);

/**
 * @brief 设备注销
 * @param data 设备数据结构体, 包含设备名字,数据流类型,可支持的类型列表,pcm回调函数指针
 */
void pcm_unregister(struct pcm_dev_data *data);

/**
 * @brief 根据名字获取设备结构体
 * @param name 设备名称
 * @return 成功返回设备结构体, 失败返回NULL
 */
struct pcm_device *pcm_get(const char *name);

/**
 * @brief 将音频数据写入设备
 * @param dev 设备结构体
 * @param buf 写入的音频数据存放地址
 * @param frame_count 音频帧数
 * @return 成功返回帧数, 失败返回负数
 */
int pcm_write_frame(struct pcm_device *dev, void *buf, int frame_count);

/**
 * @brief 从设备读取音频数据
 * @param dev 设备结构体
 * @param buf 读取到的音频数据存放地址
 * @param frame_count 音频帧数
 * @return 成功返回帧数, 失败返回负数
 */
int pcm_read_frame(struct pcm_device *dev, void *buf, int frame_count);

/**
 * @brief 将音频数据写入设备, 超时退出
 * @param dev 设备结构体
 * @param buf 写入的音频数据存放地址
 * @param frame_count 音频帧数
 * @param timeout_ms 超时时间, 单位为ms
 * @return 成功返回帧数, 失败返回负数
 */
int pcm_write_frame_timeout(struct pcm_device *dev,
    void *buf, int frame_count, unsigned int timeout_ms);

/**
 * @brief 从设备读取音频数据, 超时退出
 * @param dev 设备结构体
 * @param buf 读取到的音频数据存放地址
 * @param frame_count 音频帧数
 * @param timeout_ms 超时时间, 单位为ms
 * @return 成功返回帧数, 失败返回负数
 */
int pcm_read_frame_timeout(struct pcm_device *dev,
    void *buf, int frame_count, unsigned int timeout_ms);

/**
 * @brief 检查参数配置结构体是否符合设备要求
 * @param dev 设备结构体
 * @param p 参数配置结构体
 * @return 成功返回1, 失败返回0
 */
int pcm_check_params(struct pcm_device *dev, struct pcm_params *p);

/**
 * @brief 使能设备, 初始化设备等
 * @param dev 设备结构体
 * @param param 参数配置结构体
 * @return 成功返回0, 失败返回负数
 */
int pcm_enable(struct pcm_device *dev, struct pcm_params *param);

/**
 * @brief 失能设备
 * @param dev 设备结构体
 */
void pcm_disable(struct pcm_device *dev);

/**
 * @brief 启动设备
 * @param dev 设备结构体
 * @return 成功返回0, 失败返回负数
 */
int pcm_start(struct pcm_device *dev);

/**
 * @brief 停止设备
 * @param dev 设备结构体
 */
void pcm_stop(struct pcm_device *dev);

/**
 * @brief 设置是否静音
 * @param dev 设备结构体
 * @param mute 是否设置静音, 1 为静音
 * @return 成功返回0, 失败返回负数
 */
int pcm_set_mute(struct pcm_device *dev, int mute);

/**
 * @brief 获取是否静音
 * @param dev 设备结构体
 * @return 返回1为静音, 0为工作
 */
int pcm_get_mute(struct pcm_device *dev);

/**
 * @brief 设置音量
 * @param dev 设备结构体
 * @param val 音量, 范围为0-100
 * @return 成功返回0, 失败返回负数
 */
int pcm_set_volume(struct pcm_device *dev, int val);

/**
 * @brief 获取音量
 * @param dev 设备结构体
 * @return 返回音量, 范围1-100
 */
int pcm_get_volume(struct pcm_device *dev);

/**
 * @brief 执行对应配置id的代码并设置值
 * @param dev 设备结构体
 * @param ctrl_id 配置id, "sysclk-set-rate"为设置时钟频率, "sysclk-set-output"为设置时钟传输方向
 * @param value 对应配置id需要设定的值
 * @return 存在配置id并配置成功返回0, 失败返回负数
 */
int pcm_private_ctrl(struct pcm_device *dev, const char *ctrl_id, unsigned long value);

/**
 * @brief 获取数据格式对应大小, 以字节为单位
 * @param fmt 数据格式
 * @return 返回数据格式对应的大小
 */
unsigned int pcm_data_sample_size(pcm_data_fmt fmt);

/**
 * @brief 获取采样率大小, 以hz为单位
 * @param rate 采样率类型
 * @return 返回采样率类型对应的采样率大小
 */
unsigned int pcm_data_sample_rate(pcm_sample_rate rate);

/**
 * @brief 获取一帧大小, 数据格式对应大小 * 通道数, 以字节为单位
 * @param param 参数配置结构体
 * @return 返回帧大小
 */
int pcm_frame_size(struct pcm_params *param);

/**
 * @brief 获取参数配置结构体
 * @param dev 设备结构体
 * @return 返回设备对应的参数配置结构体
 */
struct pcm_params *pcm_get_device_params(struct pcm_device *dev);

#endif /* _PCM_H_ */
