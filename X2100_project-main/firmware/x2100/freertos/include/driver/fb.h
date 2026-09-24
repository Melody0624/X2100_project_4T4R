#ifndef _FB_H_
#define _FB_H_

#include <list.h>

struct fb_dev;

enum fb_fmt {
    fb_fmt_RGB555,
    fb_fmt_RGB565,
    fb_fmt_RGB888,
    fb_fmt_ARGB8888,
    fb_fmt_NV12,
    fb_fmt_NV21,
    fb_fmt_yuv422,
    /*仅在旋转模式时，writeback使用*/
    fb_fmt_ROTATE,
};

enum lcdc_layer_order {
    lcdc_layer_top,
    lcdc_layer_bottom,
    lcdc_layer_0,
    lcdc_layer_1,
    lcdc_layer_2,
    lcdc_layer_3,
};

enum lcdc_rotate_angle {
    ROTATE_0,
    ROTATE_90,
    ROTATE_180,
    ROTATE_270,
};


enum fb_csc_type {
    FB_CSC_BT601_TV_RANGE,
    FB_CSC_BT601_FULL_RANGE,
    FB_CSC_BT709_TV_RANGE,
    FB_CSC_BT709_FULL_RANGE,
};

struct lcdc_layer {
    enum fb_fmt fb_fmt;

    unsigned int xres;
    unsigned int yres;

    unsigned int xpos;
    unsigned int ypos;

    enum lcdc_layer_order layer_order;
    int layer_enable;

    struct {
        void *mem;
        unsigned int stride; // 单位： 字节
    } rgb;

    struct {
        void *mem;
        unsigned int stride; // 单位： 字节
    } y;

    struct {
        void *mem;
        unsigned int stride; // 单位： 字节
    } uv;

    struct {
        unsigned char enable;
        unsigned char value;
    } alpha;

    struct {
        unsigned char enable;
        unsigned int xres;
        unsigned int yres;
    } scaling;

    enum fb_csc_type convert_type;
};

struct fb_info {
    void *fb_mem;
    enum fb_fmt fb_fmt;
    unsigned int xres;
    unsigned int yres;
    unsigned int bytes_per_line;
    unsigned int bytes_per_frame;
    unsigned int frame_count;
};

struct fb_ops {
    void (*fb_enable)(struct fb_dev *fbdev);
    int (*fb_set_config)(struct fb_dev *fbdev, struct lcdc_layer *cfg);
    void (*fb_enable_config)(struct fb_dev *fbdevg);
    void (*fb_disable_config)(struct fb_dev *fbdev);
    void (*fb_pan_display)(struct fb_dev *fbdev, unsigned int layer_index);
    void (*fb_disable)(struct fb_dev *fbdev);
    void (*fb_get_info)(struct fb_dev *fbdev, struct fb_info *info);
    int (*fb_is_enable)(struct fb_dev *fbdev);
    void (*fb_hareware_lock)(struct fb_dev *fbdev);
    void (*fb_hareware_unlock)(struct fb_dev *fbdev);
};

struct fb_handle {
    struct fb_dev *fbdev;
    struct fb_ops *ops;
    struct list_head node;
    const char *name;
};

void fb_init(void);

void fb_core_suspend(void);

void fb_core_resume(void);

/*
 * ESD检测判断屏幕是否正常显示,检测方法及顺序如下:
 * 1.te detect 2.read reg 3.error report
 *
 * return:
 * 成功返回 0    (可继续隔一定时长循环执行)
 * 失败返回错误值 (可调用fb_core_suspend + fb_core_resume进行恢复)
*/
int fb_esd_check_work_error(void);

void fb_regiser(const char *name, struct fb_dev *fbdev, struct fb_ops *ops);

struct fb_handle *fb_open(const char *name);

void fb_enable(struct fb_handle *handle);

void fb_disable(struct fb_handle *handle);

void fb_pan_display(struct fb_handle *handle, unsigned int frame_index);

void fb_get_info(struct fb_handle *handle, struct fb_info *info);

void fb_hareware_lock(struct fb_handle *handle);

void fb_hareware_unlock(struct fb_handle *handle);

int fb_is_enable(struct fb_handle *handle);

int fb_set_config(struct fb_handle *handle, struct lcdc_layer *cfg);

struct list_head *fb_get_device_list(void);

void fb_disable_config(struct fb_handle *handle);

void fb_enable_config(struct fb_handle *handle);

unsigned int fb_bytes_per_pixel(enum fb_fmt fb_fmt);

unsigned int fb_bits_per_pixel(enum fb_fmt fb_fmt);

/* 导出fb资源配置，供linux继承 */
void fb_export_config(void);

#endif /* _FB_H_ */