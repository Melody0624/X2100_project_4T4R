#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <printf.h>
#include <common.h>
#include <unistd.h>
#include <malloc.h>
#include <include_bin.h>
#include <driver/cache.h>
#include <driver/fb.h>
#include <driver/backlight.h>
#include <jpegd_decoder.h>

INCBIN(jpg, "example/resource/test.jpeg");
#define WIDTH 658
#define HEIGHT 411

struct fb_info fb_info;
struct fb_handle *fb;

enum fb_fmt format_jpeg_to_fb(enum jpegd_pix_fmt jpeg_fmt) {
    enum fb_fmt fb_fmt = 0;

    switch (jpeg_fmt) {
    case JPEGD_PIX_FMT_NV12: fb_fmt = fb_fmt_NV12; break;
    case JPEGD_PIX_FMT_NV21: fb_fmt = fb_fmt_NV21; break;
    case JPEGD_PIX_FMT_YUYV: fb_fmt = fb_fmt_yuv422; break;
    case JPEGD_PIX_FMT_BGRA_8888: fb_fmt = fb_fmt_ARGB8888; break;
    default:
        fprintf(stderr, "FB dont't support this format to scale\n");
        break;
    }

    if (!fb_fmt)
        exit(-1);

    return fb_fmt;
}

static void fb_pan_display_with_fmt(struct jpegd_decoder_param *param, struct jpegd_decoder_output *output)
{
    struct lcdc_layer layer_cfg = {
        .fb_fmt = format_jpeg_to_fb(param->out_fmt),
        .xres = output->actual_width, 
        .yres = output->actual_height,
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_0,
        .layer_enable = 1,

        .y = {
            .mem = (void *)output->data,
            .stride = output->width,
        },

        .uv = {
            .mem = (void *)output->data + output->width * output->height,
            .stride = output->width,
        },

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },
    };

    if (layer_cfg.fb_fmt == fb_fmt_yuv422) {
        layer_cfg.rgb.mem = (void *)output->data;
        layer_cfg.rgb.stride = output->width * 2;
    }

    if (layer_cfg.fb_fmt == fb_fmt_ARGB8888) {
        layer_cfg.rgb.mem = (void *)output->data;
        layer_cfg.rgb.stride = output->width * 4;
    }

    if (layer_cfg.xres != fb_info.xres || layer_cfg.yres != fb_info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = fb_info.xres;//缩放到全屏显示
        layer_cfg.scaling.yres = fb_info.yres;//缩放到全屏显示
    }

    if (!fb_set_config(fb, &layer_cfg))
        fb_pan_display(fb, 0);
}

/* 此demo支持任意尺寸的jpeg图片解码后，直通fb(支持多格式)缩放到全屏幕显示 */
void ad100_jpegd_decode_and_display_test(void)
{
    // 初始化fb
    fb = fb_open("fb0");
    if (fb == NULL) {
        printf("open fb0 error!\n");
        return;
    }

    fb_enable(fb);
    fb_get_info(fb, &fb_info);
    fb_enable_config(fb);

    // 初始化背光
    struct backlight *lcd_pwm;
    lcd_pwm = backlight_open("backlight_pwm0");
    if (!lcd_pwm) {
        printf("Unable to open lcd_pwm!\n");
        return;
    }

    struct jpegd_decoder *decoder;
    struct jpegd_decoder_output *out;

    struct jpegd_decoder_param param = {
        .width = WIDTH,
        .height = HEIGHT,
        .out_fmt = JPEGD_PIX_FMT_NV12,//支持NV12/NV21、YUYV、BGRA格式的直通
        .crop = 0,//不对非16对齐的宽高进行裁切，由fb来裁切,速度更快
    };

    decoder = jpegd_decoder_init(&param);
    if (!decoder)
        return;

    out = jpegd_decoder_alloc_output_buf(decoder);

    int ret = jpegd_decoder_decode(decoder, (void *)jpgData, jpgSize, out);
    if (ret < 0) {
        printf("jpeg decode failed\n");
        goto decode_err;
    }

    fb_pan_display_with_fmt(&param, out);

    //刷屏,开启背光
    fb_pan_display(fb, 0);
    backlight_set_brightness(lcd_pwm, backlight_get_maxbrightness(lcd_pwm));

decode_err:
    jpegd_decoder_free_output_buf(out);
    jpegd_decoder_deinit(decoder);
}