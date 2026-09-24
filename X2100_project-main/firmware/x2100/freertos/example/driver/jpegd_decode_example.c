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

/* 数据居中写入fb */
void draw_data_to_fb_center(struct fb_info *fb_info, struct jpegd_decoder_output *out)
{
    if (!fb_info->fb_mem)
        return;

    int display_width = min(out->actual_width, (int)fb_info->xres);
    int display_height = min(out->actual_height, (int)fb_info->yres);
    int fb_x_offset = (fb_info->xres - display_width) / 2;
    int fb_y_offset = (fb_info->yres - display_height) / 2;
    int jpeg_x_offset = (out->actual_width - display_width) / 2;
    int jpeg_y_offset = (out->actual_height - display_height) / 2;

    void *fb_rgb = fb_info->fb_mem + fb_info->xres * 4 * fb_y_offset + fb_x_offset * 4;
    void *jpeg_rgb = out->data + out->width * 4 * jpeg_y_offset + jpeg_x_offset * 4;

    //逐行拷贝进fb
    for (int h = 0; h < display_height; h++) {
        memcpy(fb_rgb, jpeg_rgb, display_width * 4);
        fb_rgb += fb_info->xres * 4;
        jpeg_rgb += out->width * 4;
    }
}

/* 此demo只验证解码可行性 (将任意尺寸jpeg图片解码后居中显示) */
void jpegd_decode_display_to_fb_center(void)
{
    struct fb_info fb_info;
    struct fb_handle *fb;
    struct backlight *lcd_pwm;
    struct jpegd_decoder *decoder;
    struct jpegd_decoder_output *out;

    // 初始化背光
    lcd_pwm = backlight_open(CONFIG_PWM_BACKLIGHT0_NAME);
    if (!lcd_pwm) {
        printf("Unable to open lcd_pwm!\n");
        return;
    }

    // 初始化fb
    fb = fb_open("fb0");
    if (!fb) {
        printf("Unable to open fb0!\n");
        return;
    }
    fb_get_info(fb, &fb_info);
    fb_enable(fb);

    struct jpegd_decoder_param param = {
        .width = WIDTH,
        .height = HEIGHT,
        .out_fmt = JPEGD_PIX_FMT_BGRA_8888,
        .crop = 0,
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

    draw_data_to_fb_center(&fb_info, out);
    /*------------------------------------------------------------------*/

    //刷屏,开启背光
    fb_pan_display(fb, 0);
    backlight_set_brightness(lcd_pwm, backlight_get_maxbrightness(lcd_pwm));

decode_err:
    jpegd_decoder_free_output_buf(out);
    jpegd_decoder_deinit(decoder);
}
