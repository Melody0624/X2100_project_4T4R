#include <stdio.h>
#include <cmyk_to_rgb.h>
#include <string.h>
#include <stdlib.h>
#include "jpeglib.h"
#include "jerror.h"

#include <driver/fb.h>
#include <driver/adc.h>
#include <common.h>
#include <os.h>

#include <include_bin.h>

INCBIN(test_display, "example/resource/test.jpeg");

int backlight_is_open = 0;

/* 初始化fb */
static void enbale_fb_thread(void *data)
{
    struct fb_handle *fb_handle = (struct fb_handle *)data;

    fb_enable(fb_handle);
}

static struct fb_handle *fb_init_display(struct fb_info *info)
{
    struct fb_handle *fb_handle;

    fb_handle = fb_open("fb0");
    if(fb_handle == NULL) {
        printf("fb0 = NULL\n");
        return NULL;
    }

    fb_get_info(fb_handle, info);
    if (info->fb_fmt != fb_fmt_RGB888 && info->fb_fmt != fb_fmt_ARGB8888 ) {
        printf("fb_init_display: this just support rgb888! you should change this demo\n");
        return NULL;
    }

    // 开启线程
    thread_create("enbale_fb_thread", 1024, enbale_fb_thread, fb_handle);

    return fb_handle;
}

static void open_backlight(void)
{
    int brightness;
    struct backlight *lcd_pwm;

    if (backlight_is_open)
        return;

    // 开启屏幕背光
    lcd_pwm = backlight_open("backlight_gpio0");
    if (lcd_pwm == NULL) {
        printf("backlight_open fail.\n");
        return;
    }

    brightness = backlight_get_maxbrightness(lcd_pwm);
    printf("max brightness = %d\n", brightness);

    backlight_set_brightness(lcd_pwm, brightness);

    brightness = backlight_get_brightness(lcd_pwm);
    printf("brightness = %d\n", brightness);

    backlight_is_open = 1;
}

void jpeg_display_to_fb(struct fb_info *info, struct fb_handle *fb_handle, const unsigned char *jpeg_data, unsigned int jpeg_size,
    int fb_x_offset, int fb_y_offset, unsigned int jpeg_x_offset, unsigned int jpeg_y_offset, unsigned int display_width, unsigned int display_height)
{
    int xres;
    int yres;
    int width;
    int height;
    struct jpeg_error_mgr jerr;
    struct jpeg_decompress_struct cinfo;
    unsigned int row_stride;
    JSAMPARRAY buffer;

    // 绑定标准错误处理结构
    cinfo.err = jpeg_std_error(&jerr);

    // 初始化JPEG对象
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);

    // 读取图像信息
    (void) jpeg_read_header(&cinfo, TRUE);

    // 设定解压缩参数，此处我们将图像长宽缩小为原图的1/2，目前支持1/1,1/2,1/4,1/8
    cinfo.scale_num=1;
    cinfo.scale_denom=1;

    // 开始解压缩图像
    (void) jpeg_start_decompress(&cinfo);

    if (jpeg_x_offset >= cinfo.output_width) {
        return;
    }
    if (jpeg_y_offset >= cinfo.output_height) {
        return;
    }

    xres = info->xres;
    yres = info->yres;

    if (fb_x_offset < 0) {
        if (fb_x_offset + display_width <= 0)
            return;

        if (fb_x_offset + (cinfo.output_width - jpeg_x_offset) <= 0)
            return;

        if (cinfo.output_width - jpeg_x_offset < display_width)
            width = (cinfo.output_width - jpeg_x_offset) + fb_x_offset;
        else
            width = display_width + fb_x_offset;

        jpeg_x_offset -= fb_x_offset;
        fb_x_offset = 0;
    } else {
        if (fb_x_offset >= xres)
            return;

        if (fb_x_offset >= xres - display_width)
            display_width = xres - fb_x_offset;

        width = min(display_width, cinfo.output_width - jpeg_x_offset);
    }

    if (fb_y_offset < 0) {
        if (fb_y_offset + display_height <= 0)
            return;

        if (fb_y_offset + (cinfo.output_height - jpeg_y_offset) <= 0)
            return;

        if (cinfo.output_height - jpeg_y_offset < display_height)
            height = (cinfo.output_height - jpeg_y_offset) + fb_y_offset;
        else
            height = display_height + fb_y_offset;

        jpeg_y_offset -= fb_y_offset;
        fb_y_offset = 0;
    } else {
        if (fb_y_offset >= yres)
            return;

        if (display_height >= yres - fb_y_offset)
            display_height = yres - fb_y_offset;

        height = min(display_height, cinfo.output_height - jpeg_y_offset);
    }

    // 分配缓冲区空间
    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    unsigned char *jpeg_rgb_buffer = NULL;
    if (cinfo.out_color_space == JCS_CMYK) {
        jpeg_rgb_buffer = (unsigned char *)malloc(row_stride * sizeof(unsigned char));
    }

    // 定位到fb应该显示的第一行
    unsigned int *fb_mem = info->fb_mem + info->bytes_per_line * fb_y_offset;

    // 定位到jpeg应该显示的第一行（即跳过图片不显示的上半部分）
    int w,h;
    for (h = 0; h < jpeg_y_offset; h++) {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
    }

    // 逐行解码jpeg图片，并拷贝到fb
    for (h = 0; h < height; h++) {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
        unsigned char *jpeg_rgb = buffer[0];

        if (cinfo.out_color_space == JCS_CMYK) {
            cmyk_to_rgb24((unsigned int *)buffer[0], jpeg_rgb_buffer, row_stride, row_stride, cinfo.output_width, 1);
            jpeg_rgb = jpeg_rgb_buffer;
        }

        unsigned int *fb_rgb = fb_mem;
        fb_rgb += fb_x_offset;
        jpeg_rgb += jpeg_x_offset * 3;

        for (w = 0; w < width; w++) {
            unsigned int r = jpeg_rgb[w*3 + 0];
            unsigned int g = jpeg_rgb[w*3 + 1];
            unsigned int b = jpeg_rgb[w*3 + 2];

            fb_rgb[w] = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
        }

        fb_mem = (void *)fb_mem + info->bytes_per_line;
    }

    // 将jpeg解码的行定位到最后，否则jpeg库会报错退出
    cinfo.output_scanline = cinfo.output_height;

    // 结束解压缩操作
    (void) jpeg_finish_decompress(&cinfo);

    // 释放资源
    if (cinfo.out_color_space == JCS_CMYK) {
        free(jpeg_rgb_buffer);
    }

    // 销毁JPEG对象
    jpeg_destroy_decompress(&cinfo);

    // 等待 fb 开启
    while (!fb_is_enable(fb_handle));

    // 显示
    fb_pan_display(fb_handle, 0);

    // 打开背光
    open_backlight();
}

void jpeg_display_center_to_fb(struct fb_info *info, struct fb_handle *fb_handle, const unsigned char *jpeg_data, unsigned int jpeg_size, unsigned int jpeg_width, unsigned int jpeg_height)
{
    int display_width = min(jpeg_width, info->xres);
    int display_height = min(jpeg_height, info->yres);

    int fb_x_offset = (info->xres - display_width) / 2;
    int fb_y_offset = (info->yres - display_height) / 2;

    int jpeg_x_offset = (jpeg_width - display_width) / 2;
    int jpeg_y_offset = (jpeg_height - display_height) / 2;

    jpeg_display_to_fb(info, fb_handle, jpeg_data, jpeg_size, fb_x_offset, fb_y_offset, jpeg_x_offset, jpeg_y_offset, display_width, display_height);
}

/*************************************************************/
static void release_all_resources(void)
{
    // lcd
    disable_irq(IRQ_LCD);
    release_irq(IRQ_LCD);

    // intc
    arch_deinit_cpu();
    disable_irq(IRQ_V_IP2);
    release_irq(IRQ_V_IP2);

    // ost
    disable_irq(IRQ_V_IP4);
    release_irq(IRQ_V_IP4);

}

#ifdef CONFIG_XBURST
extern void jump_to_uboot(void);
#endif

static void show_logo(struct fb_info *info, struct fb_handle *fb_handle)
{
    memset(info->fb_mem, 0x0, info->bytes_per_frame);
    jpeg_display_center_to_fb(info, fb_handle, test_displayData, test_displaySize, 658, 411);

    release_all_resources();

#ifdef CONFIG_XBURST
    jump_to_uboot(); //xbust1 系列 rtos for uboot 需调用该函数
#endif

#ifdef CONFIG_XBURST2
    arch_shutdown_current_cpu();
#endif
}

/*
 * unit: mV
 * x1830  vref voltage [ 900 - 1800 ]
 * x1520  vref voltage [ 3300 ]
 */
#define VREF_VOLTAGE        1800
#define SHUTDOWN_VOLTAGE    500

void detect_low_voltage_shutdown(void *data)
{
    unsigned int count = 5;
    unsigned int val;
    struct fb_info info;
    struct fb_handle *fb_handle;

    adc_init();
    // 初始化 fb
    fb_handle = fb_init_display(&info);
    if (fb_handle == NULL)
        return;

    while (count) {
        val = adc_read_data(0);
        printf("ADC sample voltage: %dmV\n", val * VREF_VOLTAGE / 1024);
        if ((val * VREF_VOLTAGE / 1024) < SHUTDOWN_VOLTAGE)
            break;
        msleep(20);
        count--;
    }

    adc_deinit();

    if (count == 0) {
        show_logo(&info, fb_handle);
        return;
    }

    memset(info.fb_mem, 0x0, info.bytes_per_frame);
    jpeg_display_center_to_fb(&info, fb_handle, test_displayData, test_displaySize, 658, 411);

    printf("Will power off in 5 seconds\n");
    msleep(5000);
    pm_power_off();
}

void low_voltage_auto_shutdown(void)
{
    thread_create("detect_low_voltage_shutdown", 2048, detect_low_voltage_shutdown, NULL);
}
