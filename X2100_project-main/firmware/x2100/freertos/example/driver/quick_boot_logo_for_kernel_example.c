#include <stdio.h>
#include <cmyk_to_rgb.h>
#include <string.h>
#include <stdlib.h>
#include "jpeglib.h"
#include "jerror.h"

#include <driver/fb.h>
#include <driver/irq.h>
#include <cpu/cpu.h>
#include <driver/backlight.h>
#include <os.h>

#include <include_bin.h>
INCBIN(jpeg_display, "example/resource/test.jpeg");

static struct fb_info info;
static struct fb_handle *fb0_handle;

/* 初始化fb */
static void enbale_fb_thread(void *data)
{
    fb_enable(fb0_handle);
}

int jpeg_display_to_fb(void)
{
    int brightness;
    struct backlight *lcd_pwm;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPARRAY buffer;
    int row_stride;

    fb0_handle = fb_open("fb0");
    if(fb0_handle == NULL)
        printf("fb0 = NULL\n");

    fb_get_info(fb0_handle, &info);
    if (info.fb_fmt != fb_fmt_RGB888 && info.fb_fmt != fb_fmt_ARGB8888 ) {
        printf("jpeg_display_to_fb: this just support rgb888! you should change this demo\n");
        return -1;
    }

    // 开启线程
    thread_create("enbale_fb_thread", 1024, enbale_fb_thread, NULL);

    // 绑定标准错误处理结构
    cinfo.err = jpeg_std_error(&jerr);

    // 初始化JPEG对象
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo,jpeg_displayData, jpeg_displaySize);

    // 读取图像信息
    (void) jpeg_read_header(&cinfo, TRUE);

    // 设定解压缩参数，此处我们将图像长宽缩小为原图的1/2，目前支持1/1,1/2,1/4,1/8
    cinfo.scale_num=1;
    cinfo.scale_denom=1;

    // 开始解压缩图像
    (void) jpeg_start_decompress(&cinfo);

    // 分配缓冲区空间
    row_stride = cinfo.output_width * cinfo.output_components;
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

    int display_width = min(cinfo.output_width, info.xres);
    int display_height = min(cinfo.output_height, info.yres);

    int fb_x_offset = (info.xres - display_width) / 2;
    int fb_y_offset = (info.yres - display_height) / 2;

    int jpeg_x_offset = (cinfo.output_width - display_width) / 2;
    int jpeg_y_offset = (cinfo.output_height - display_height) / 2;

    unsigned char *jpeg_rgb_buffer = NULL;
    if (cinfo.out_color_space == JCS_CMYK) {
        jpeg_rgb_buffer = (unsigned char *)malloc(row_stride * sizeof(unsigned char));
    }

    // 定位到fb应该显示的第一行
    unsigned int *fb_mem = info.fb_mem + info.bytes_per_line * fb_y_offset;

    // 定位到jpeg应该显示的第一行（即跳过图片不显示的上半部分）
    int w,h;
    for (h = 0; h < jpeg_y_offset; h++) {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
    }

    // 逐行解码jpeg图片，并拷贝到fb
    for (h = 0; h < display_height; h++) {
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
        unsigned char *jpeg_rgb = buffer[0];

        if (cinfo.out_color_space == JCS_CMYK) {
            cmyk_to_rgb24((unsigned int *)buffer[0], jpeg_rgb_buffer, row_stride, row_stride, cinfo.output_width, 1);
            jpeg_rgb = jpeg_rgb_buffer;
        }

        unsigned int *fb_rgb = fb_mem;
        fb_rgb += fb_x_offset;
        jpeg_rgb += jpeg_x_offset * 3;

        for (w = 0; w < display_width; w++) {
            unsigned int r = jpeg_rgb[w*3 + 0];
            unsigned int g = jpeg_rgb[w*3 + 1];
            unsigned int b = jpeg_rgb[w*3 + 2];

            fb_rgb[w] = (0xff << 24) | (r << 16) | (g << 8) | (b << 0);
        }

        fb_mem = (void *)fb_mem + info.bytes_per_line;
    }

    // 将jpeg解码的行定位到最后，否则jpeg库会报错退出
    cinfo.output_scanline = cinfo.output_height;

    // 等待 fb 开启
    while (!fb_is_enable(fb0_handle)) {
        usleep(300);
    }

    // 显示
    fb_pan_display(fb0_handle, 0);

    // 开启屏幕背光
    lcd_pwm = backlight_open("backlight_pwm0");
    if (lcd_pwm == NULL) {
        printf("backlight_open fail.\n");
    }

    brightness = backlight_get_maxbrightness(lcd_pwm);
    printf("max brightness = %d\n", brightness);

    backlight_set_brightness(lcd_pwm, brightness);

    brightness = backlight_get_brightness(lcd_pwm);
    printf("brightness = %d\n", brightness);

    // 结束解压缩操作
    (void) jpeg_finish_decompress(&cinfo);

    // 释放资源
    if (cinfo.out_color_space == JCS_CMYK) {
        free(jpeg_rgb_buffer);
    }
    jpeg_destroy_decompress(&cinfo);

    return 0;
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

static void show_logo(void *data)
{
    jpeg_display_to_fb();

    release_all_resources();

#ifdef CONFIG_XBURST
    jump_to_uboot(); //xbust1 系列 rtos for uboot 需调用该函数
#endif

#ifdef CONFIG_XBURST2
    arch_shutdown_current_cpu();
#endif
}

void quick_boot_logo_for_kernel(void)
{
    thread_create("load kernel thread", 8192, show_logo, NULL);
}