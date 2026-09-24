#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <driver/camera_pixel_format.h>
typedef enum {
    /* 当前并未出错
     */
    camera_error_null,

    /* 等待帧数据时camera_stream_off被调用
     */
    camera_error_stream_is_off,

    /* 帧传输时dma出错，可能导致帧数据错位
     */
    camera_error_dma_error,

    /* 帧接收超时，其它原因导致
     */
    camera_error_timeout,
} camera_frame_error_type;

struct camera_info {
    char name[64];

    /* 每行像素数 */
    unsigned int width;

    /* 行数 */
    unsigned int height;

    /* camera 帧率 */
    unsigned int fps;

    /* camera 帧数据格式 */
    camera_pixel_fmt data_fmt;

    /* 一行的长度,单位字节
     * 对于 nv12,nv21, 表示y数据一行的长度
     * 另外由此可以算出uv数据偏移 line_length*height
     */
    unsigned int line_length;

    /* 一帧数据经过对齐之前的大小 */
    unsigned int frame_size;

    /* 帧缓冲总数 */
    unsigned int frame_nums;

    /* 帧缓冲的物理基地址 */
    unsigned long phys_mem;

    /* mmap 后的帧缓冲基地址 */
    void *mapped_mem;

    /*帧对齐大小 */
    unsigned int frame_align_size;



    /* 上面的结构体成员与 Linux 一致，下面的成员仅 RTOS 所有*/

    /* 诸如 NV12 等 planner 格式下, UV 数据在一帧中的偏移 */
    unsigned int uv_data_offset;
};

/* 图像帧信息 */
struct frame_info {
    unsigned int index;             /* 缓存编号 */
    unsigned int sequence;          /* 帧序列号 */

    unsigned int width;             /* 帧宽 */
    unsigned int height;            /* 帧高 */
    unsigned int pixfmt;            /* 帧的图像格式 */
    unsigned int size;              /* 帧所占用空间大小 */
    void *vaddr;                    /* 帧的虚拟地址 */
    unsigned long paddr;            /* 帧的物理地址 */

    unsigned long long timestamp;   /* 帧的时间戳，单位微秒，单调时间 */

    unsigned int isp_timestamp;     /* isp时间戳，在vic通过isp clk计数换算得出，单位微秒，
                                       最大值10000秒左右（最大值和isp clk相关），大于最大值重新清零计时 */
    unsigned int shutter_count;     /* 曝光计数 */
};

struct sensor_dbg_register {
    unsigned long long reg;
    unsigned long long val;
    unsigned int size;      /* val size, unit:byte */
};

struct camera_device;

struct cis_dpi_config {
    int dpi;                                                        /*配置的dpi*/
    int tr_cycle;                                                   /*对应dpi的tr_cycle*/
    int pixel_cycle;                                                /*对应dpi的pixel_cycle*/
};

/**
 * @brief camera 驱动初始化
 */
void camera_init(void);

/**
 * @brief 探测第一个可用的 camera
 * @param index 选择控制器(仅X2000有效)
 * @return 非NULL : 成功    NULL: 失败, 一般是i2c 通信失败或者没有sensor注册
 */
struct camera_device *camera_detect(int index);

/**
 * @brief 或者camera 信息
 * @param camera 由 camera_detect() 返回的指针
 * @return camera 信息
 */
struct camera_info *camera_get_info(struct camera_device *camera);

/**
 * @brief 获取 camera sensor ID
 * @param camera 由 camera_detect() 返回的指针
 * @return camera sensor ID
 */
unsigned char *camera_get_sensor_id(struct camera_device *camera);

/**
 * @brief 打开 camera 电源
 * @param camera 由 camera_detect() 返回的指针
 * @return 0 : 成功    < 0: 失败, 一般是i2c 通信失败
 */
int camera_power_on(struct camera_device *camera);

/**
 * @brief 关闭 camera 电源
 * @param camera 由 camera_detect() 返回的指针
 */
void camera_power_off(struct camera_device *camera);

/**
 * @brief 打开 camera 图像输出
 * @param camera 由 camera_detect() 返回的指针
 * @return 0 : 成功    < 0: 失败, 一般是i2c 通信失败
 */
int camera_stream_on(struct camera_device *camera);

/**
 * @brief 关闭 camera 图像输出
 * @param camera 由 camera_detect() 返回的指针
 */
void camera_stream_off(struct camera_device *camera);

/**
 * @brief 等待可用的帧
 * @param camera 由 camera_detect() 返回的指针
 * @return 非NULL: 帧的首地址  NULL: 表示失败,失败原因由camera_get_frame_error()得到
 */
void *camera_wait_frame(struct camera_device *camera);

/**
 * @brief 获取camera 帧错误原因
 * @param camera 由 camera_detect() 返回的指针
 * @return camera 帧错误原因
 * camera_error_null 表示没有错误，其它表示出错 @see camera_frame_error_type
 */
camera_frame_error_type camera_get_frame_error(struct camera_device *camera);

/**
 * @brief 等待可用的帧
 * @param camera 由 camera_detect() 返回的指针
 * @param frame 由 camera_wait_frame() 返回的指针
 */
void camera_put_frame(struct camera_device *camera, void *frame);

/**
 * @brief 获取一帧录制的图像数据
 * @param camera 由 camera_detect() 返回的指针
 */
void *camera_get_frame(struct camera_device *camera);

/**
 * @brief 等待可用的帧
 * @param camera 由 camera_detect() 返回的指针
 * @param frame 由 camera_wait_frame() 返回的指针
 */
int camera_dqbuf(struct camera_device *camera, struct frame_info *frame);

/**
 * @brief 等待可用的帧
 * @param camera 由 camera_detect() 返回的指针
 * @param frame 由 camera_wait_frame() 返回的指针
 */
int camera_dqbuf_wait(struct camera_device *camera, struct frame_info *frame);

/**
 * @brief 等待可用的帧
 * @param camera 由 camera_detect() 返回的指针
 * @param frame 由 camera_wait_frame() 返回的指针
 */
int camera_qbuf(struct camera_device *camera, struct frame_info *frame);

/**
 * @brief 获取sensor寄存器
 * @param camera 由 camera_detect() 返回的指针
 * @return 成功返回0,失败返回负数
 */
int camera_get_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg);

/**
 * @brief 设置sensor寄存器
 * @param camera 由 camera_detect() 返回的指针
 * @return 成功返回0,失败返回负数
 */
int camera_set_sensor_reg(struct camera_device *camera, struct sensor_dbg_register *reg);

/**
 * @brief 获取可用的帧数 (最大值由驱动frame buffer数量决定)
 * @param camera 由 camera_detect() 返回的指针
 * @return 可用的帧数
 */
unsigned int camera_get_available_frame_count(struct camera_device *camera);

/**
 * @brief 跳过指定的可用帧,这样camera_wait_frame() 能拿到较新的帧
 * @param camera 由 camera_detect() 返回的指针
 */
void camera_skip_frames(struct camera_device *camera, unsigned int frames);

/**
 * @brief 设置统计luminance区域(设置坐标). 目前仅X1600支持该功能
 * @param camera 由 camera_detect() 返回的指针
 * @param lumi_enable 使能/禁止统计区域luminance值功能
 * @param (x1, y1) 划分区域luminance第一个点坐标
 * @param (x2, y2) 划分区域luminance第二个点坐标
 * @return 是否支持该功能 < 0 设置区域失败/不支持统计luminance功能
 *                      = 0 设置区域成功
 *
 *              x1        x2
 *      |--------|--------|--------|
 *      |  area0 |  area1 |  area2 |
 *   y1 |--------|--------|--------|
 *      |  area3 |  area4 |  area5 |
 *   y2 |--------|--------|--------|
 *      |  area6 |  area7 |  area8 |
 *      |--------|--------|--------|
 */
int camera_set_luminance_area(struct camera_device *camera, int lumi_enable, int x1, int y1, int x2, int y2);

/**
 * @brief 获取区域luminance区域总和值. 目前仅X1600支持该功能
 * @param camera 由 camera_detect() 返回的指针
 * @param frame 由 camera_wait_frame() 返回的指针
 * @param lumi 各区域内luminance的总和
 * @return 统计区域占内存大小
 *         > 0, luminance统计值占用内存大小
 *         = 0, 禁止/不支持luminance统计功能
 */
int camera_get_luminance_area_total(struct camera_device *camera, void *frame, void *lumi);

/**
 * @brief 设置cis设备的dpi分辨率
 * @param camera 由 camera_detect() 返回指针
 * @param dpi 待设置的dpi参数
 * @return 是否成功
 *         >0, 设置成功
 *         <0, 设置失败
 */
int camera_set_cis_dpi(struct camera_device *camera, int dpi);

/**
 * @brief 获取cis设备支持的dpi分辨率
 * @param camera 由 camera_detect() 返回指针
 * @param size 有效可配值dpi大小
 * @return 返回有效的dpi地址
 *          获取得到cis设备注册参数：dpi tr_cycle pixel_cycle
 */
struct cis_dpi_config* camera_get_support_dpi(struct camera_device *camera, int* size);

/**
 * @brief 获取cis设备的当前的dpi分辨率
 * @param camera 由 camera_detect() 返回指针
 * @param cfg 获取得到cis设备注册参数：dpi tr_cycle pixel_cycle
 * @return 是否成功
 *         >0, 设置成功
 *         <0, 设置失败
 */
int camera_get_cis_dpi(struct camera_device *camera, struct cis_dpi_config *cfg);

/**
 * @brief 设置cis设备的led亮度
 * @param r 红灯亮度
 * @param g 绿灯亮度
 * @param b 蓝灯亮度
 * @return 0 成功，<0 失败
 */
int camera_set_cis_led_brightness(unsigned int r,
                                  unsigned int g,
                                  unsigned int b);

/**
 * @brief 开启/关闭cis设备的led
 * @param enable 1 开灯 0 关灯
 * @return 0 成功，<0 失败
 */
int camera_set_cis_led_enable(int enable);


#endif /* _CAMERA_H_ */
