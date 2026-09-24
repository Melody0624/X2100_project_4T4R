#ifndef _CAMERA_ISP_H_
#define _CAMERA_ISP_H_

#include <driver.h>
#include <driver/camera.h>
#include <driver/isp_tuning.h>

typedef struct handle_desc camera_hd_t;

/* 裁剪 */
struct channel_crop {
    int enable;
    unsigned int top;   /* 起始坐标 */
    unsigned int left;
    unsigned int width;
    unsigned int height;
};

/* 缩放 */
struct channel_scaler {
    int enable;
    unsigned int width;
    unsigned int height;
};

struct frame_image_format {
    unsigned int width;             /* ISP MScaler 输出分宽 */
    unsigned int height;            /* ISP MScaler 输出分高 */
    camera_pixel_fmt pixel_format;  /* ISP MScaler 输出格式 */
    unsigned int frame_size;        /* ISP MScaler 帧大小 */

    struct channel_scaler scaler;   /* ISP MScaler 缩放属性 */
    struct channel_crop crop;       /* ISP MScaler 裁剪属性 */

    int frame_nums;                 /* ISP MScaler 缓存个数 */
};

/**
 * @brief 探测指定ISP,指定通道是否有可用camera
 * @param index  :选择ISP序号(范围:0 ~ 1)
 * @param channel:选择输出通道 (范围:0 ~2)
 * @return 非NULL : 成功, 返回可操作camera的设备句柄
 *         NULL   : 失败, 一般是i2c通信失败或者没有sensor注册
 */
camera_hd_t *isp_detect(int index, int channel);

/**
 * @brief 关闭camera设备 句柄
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 无
 */
void isp_release(camera_hd_t *camera_hd);

/**
 * @brief 获得输出经过ISP处理后的camera信息, 长宽,格式等
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return camera 信息
 */
struct camera_info *isp_get_info(camera_hd_t *camera_hd);

/**
 * @brief 获得输出camera信息,未经过ISP的原始sensor信息
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return camera 信息
 */
struct camera_info *isp_get_sensor_info(camera_hd_t *camera_hd);

/**
 * @brief 打开 ISP时钟, camera电源等
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 0 : 成功
 *        < 0: 失败, 一般是i2c 通信失败
 */
int isp_power_on(camera_hd_t *camera_hd);

/**
 * @brief 关闭 ISP时钟, camera 电源等
 * @param camera_hd 由 isp_detect() 返回的句柄
 */
void isp_power_off(camera_hd_t *camera_hd);

/**
 * @brief 打开 camera 图像输出
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 0 : 成功
 *        < 0: 失败, 一般是i2c 通信失败
 */
int isp_stream_on(camera_hd_t *camera_hd);

/**
 * @brief 关闭 camera 图像输出
 * @param camera_hd 由 isp_detect() 返回的句柄
 */
void isp_stream_off(camera_hd_t *camera_hd);

/**
 * @brief 获取camera 帧错误原因
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return camera 帧错误原因
 * camera_error_null 表示没有错误，其它表示出错 @see camera_frame_error_type
 */
camera_frame_error_type isp_get_frame_error(camera_hd_t *camera_hd);

/**
 * @brief 等待可用的帧(超时时间3s)
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 非NULL: 帧的首地址
 *          NULL: 表示失败,失败原因由isp_get_frame_error()得到
 */
void *isp_wait_frame(camera_hd_t *camera_hd);

/**
 * @brief 获取一帧图像数据(不等待)
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 非NULL: 帧的首地址
 *           NULL: 当前缓存中没有有效数据
 */
void *isp_get_frame(camera_hd_t *camera_hd);

/**
 * @brief 释放一帧图像缓冲区
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_put_frame(camera_hd_t *camera_hd, void *frame);


/**
 * @brief 获取一帧录制的图像数据(不等待)
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param frame 获取的帧信息指针
 * @return 成功返回0,失败返回负数
 */
int isp_dqbuf(camera_hd_t *camera_hd, struct frame_info *frame);

/**
 * @brief 获取一帧录制的图像数据(未获取有效数据继续等待3s)
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param frame 获取的帧信息指针
 * @return 成功返回0,失败返回负数
 */
int isp_dqbuf_wait(camera_hd_t *camera_hd, struct frame_info *frame);

/**
 * @brief 释放一帧图像缓冲区
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param frame 释放的帧信息指针
 * @return 成功返回0,失败返回负数
 */
int isp_qbuf(camera_hd_t *camera_hd, struct frame_info *frame);

/**
 * @brief 获取可用的帧数 (最大值由驱动frame buffer数量决定)
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 可用的帧数
 */
unsigned int isp_get_available_frame_count(camera_hd_t *camera_hd);

/**
 * @brief 跳过指定可用帧个数,这样isp_wait_frame() / isp_get_frame() 能拿到较新的帧
 * @param frames 需要跳过的帧数
 * @param camera_hd 由 isp_detect() 返回的句柄
 */
void isp_skip_frames(camera_hd_t *camera_hd, unsigned int frames);

/**
 * @brief 获得ISP scaler的最大尺寸
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param width 用于存放ISP的宽
 * @param height 用于存放ISP的高
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_get_max_scaler_size(camera_hd_t *camera_hd, int *width, int *height);

/**
 * @brief 获得ISP 行对齐大小,单位是字节
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param align_size 用于存放ISP的行对齐大小
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_get_line_align_size(camera_hd_t *camera_hd, int *align_size);

/**
 * @brief 设置输出信息,分辨率,buffer个数等
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param fmt 用于传递输出信息,不能为NULL
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_set_format(camera_hd_t *camera_hd, struct frame_image_format *fmt);

/**
 * @brief 获取输出信息,frame_size(未PageSize对齐)等
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param fmt 用于存放调整后的输出的信息,不能为NULL
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_get_format(camera_hd_t *camera_hd, struct frame_image_format *fmt);

/**
 * @brief 申请帧buffer
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @param fmt 用于存放输出的信息,不能为NULL
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_request_buffer(camera_hd_t *camera_hd, struct frame_image_format *fmt);

/**
 * @brief 释放帧buffer
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_free_buffer(camera_hd_t *camera_hd);

/**
 * @brief 读取sensor寄存器值
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_get_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg);

/**
 * @brief 设置sensor寄存器
 * @param camera_hd 由 isp_detect() 返回的句柄
 * @return 0 : 成功
 *        <0 : 失败
 */
int isp_set_sensor_reg(camera_hd_t *camera_hd, struct sensor_dbg_register *reg);

#endif /* _CAMERA_ISP_H_ */
