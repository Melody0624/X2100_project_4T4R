/**
 * @file apple_mirror.h
 * @brief Apple Mirror Protocol 公共API
 *
 * @example
 * ```c
 * struct apple_mirror_callbacks callbacks = {
 *     .send = my_usb_send,
 *     .on_video_format = my_video_format_handler,
 *     .on_video_sample = my_video_handler,
 *     .on_audio_format = my_audio_format_handler,
 *     .on_audio_sample = my_audio_handler,
 * };
 *
 * struct apple_mirror *mirror = apple_mirror_init(&callbacks);
 * apple_mirror_set_audio_supported(mirror, 1);
 *
 * while (connected) {
 *     size_t len = usb_read(buffer, sizeof(buffer));
 *     apple_mirror_handle_usb_frame(mirror, buffer, len);
 * }
 *
 * apple_mirror_deinit(mirror);
 * ```
 */

#ifndef APPLE_MIRROR_H
#define APPLE_MIRROR_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * 公共数据结构 (Public Data Structures)
 * ============================================================================ */

enum apple_mirror_codec {
    APPLE_CODEC_H264 = 0,
    APPLE_CODEC_H265 = 1,
};

/**
 * @brief 视频格式信息
 */
struct apple_mirror_video_format {
    enum apple_mirror_codec codec;  /**< 编码器类型 */
    uint32_t width;                 /**< 视频宽度 */
    uint32_t height;                /**< 视频高度 */
};

/**
 * @brief 音频格式信息
 */
struct apple_mirror_audio_format {
    uint32_t sample_rate;        /**< 采样率 */
    uint32_t bytes_per_frame;    /**< 每帧字节数 */
    uint32_t channels_per_frame; /**< 声道数 */
    uint32_t bits_per_channel;   /**< 位深 */
};

/**
 * @brief 样本数据结构
 */
struct apple_mirror_sample {
    const uint8_t *data;         /**< 样本数据 */
    size_t data_len;             /**< 数据长度 */
};

/**
 * @brief 回调函数集
 */
struct apple_mirror_callbacks {
    /**
     * @brief 获取系统启动以来的纳秒数
     *
     * 当协议需要获取系统时钟时调用此函数。
     *
     * @return 系统启动以来的纳秒数
     */
    uint64_t (*get_time_ns)(void);

    /**
     * @brief 发送数据回调
     *
     * 当协议需要发送数据时调用此函数。
     *
     * @param buf 要发送的数据
     * @param len 数据长度
     * @param user 用户数据
     * @return 0成功，负值失败
     */
    int (*send)(const uint8_t *buf, size_t len, void *user);
    void *send_user;

    /**
     * @brief 视频格式变化回调
     *
     * 当视频格式发生变化时调用。
     *
     * @param format 视频格式信息
     * @param user 用户数据
     */
    void (*on_video_format)(const struct apple_mirror_video_format *format, void *user);
    void *video_format_user;

    /**
     * @brief 视频样本回调
     *
     * 收到视频数据时调用。
     *
     * @param sample 视频样本
     * @param format 当前格式
     * @param user 用户数据
     */
    void (*on_video_sample)(const struct apple_mirror_sample *sample,
                            const struct apple_mirror_video_format *format,
                            void *user);
    void *video_user;

    /**
     * @brief 音频格式变化回调
     *
     * 当音频格式确定时调用（通常在AFMT回复后）。
     *
     * @param format 音频格式信息
     * @param user 用户数据
     */
    void (*on_audio_format)(const struct apple_mirror_audio_format *format, void *user);
    void *audio_format_user;

    /**
     * @brief 音频样本回调
     *
     * 收到音频数据时调用。
     *
     * @param sample 音频样本
     * @param format 当前音频格式
     * @param user 用户数据
     */
    void (*on_audio_sample)(const struct apple_mirror_sample *sample,
                            const struct apple_mirror_audio_format *format,
                            void *user);
    void *audio_user;
};

/**
 * @brief 镜像配置参数
 */
struct apple_mirror_config {
    /* 帧缓冲配置 */
    size_t capacity;             /**< 帧缓冲容量（默认4MB） */
    size_t max_frame_size;       /**< 最大帧大小（默认1MB） */

    /* 显示配置 */
    uint32_t preferred_width;    /**< 首选宽度（默认1280） */
    uint32_t preferred_height;   /**< 首选高度（默认720） */

    /* 最大分辨率限制 */
    uint32_t max_width;          /**< 最大支持宽度（默认1920） */
    uint32_t max_height;         /**< 最大支持高度（默认1080） */

    /* 编解码配置 */
    uint8_t hevc_supported;      /**< 是否支持HEVC/H.265（默认0） */

    /* 音频配置 */
    uint8_t audio_supported;     /**< 是否支持音频（默认1） */
    uint8_t channels;            /**< 音频声道数（默认1） */
    uint32_t sample_rate;        /**< 音频采样率（默认48000） */
    uint32_t format_bits;        /**< 每声道位深（默认16） */
};

/* ============================================================================
 * 不透明句柄 (Opaque Handle)
 * ============================================================================ */

/**
 * @brief 镜像上下文句柄
 */
struct apple_mirror;

/* ============================================================================
 * 公共API函数 (Public API Functions)
 * ============================================================================ */

/**
 * @brief 初始化镜像上下文
 *
 * @param callbacks 回调函数集
 * @param config 配置参数（可选，NULL则使用默认值）
 * @return 镜像句柄，失败返回NULL
 */
struct apple_mirror *apple_mirror_init(const struct apple_mirror_callbacks *callbacks,
                                       const struct apple_mirror_config *config);

/**
 * @brief 重置镜像状态
 *
 * 清除所有状态，准备新的连接。
 *
 * @param ctx 镜像句柄
 */
void apple_mirror_reset(struct apple_mirror *ctx);

/**
 * @brief 释放镜像上下文
 *
 * @param ctx 镜像句柄
 */
void apple_mirror_deinit(struct apple_mirror *ctx);

/**
 * @brief 提交USB数据给镜像上下文处理
 *
 * 将USB数据提交给镜像上下文，上下文会处理数据并触发回调。
 *
 * @param ctx 镜像句柄
 * @param data USB数据
 * @param len 数据长度
 * @return 0成功，负值失败
 */
int apple_mirror_append_data(struct apple_mirror *ctx,
                              const uint8_t *data,
                              size_t len);

#endif /* APPLE_MIRROR_H */
