#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <printf.h>
#include <common.h>
#include <unistd.h>
#include <malloc.h>
#include <include_bin.h>
#include <driver/fb.h>
#include <driver/backlight.h>
#include <driver/cache.h>
#include <os.h>

#include <jpege_encoder.h>
#include <jpegd_decoder.h>


INCBIN(raw, "example/resource/720x720_nv12.yuv");
#define WIDTH 720
#define HEIGHT 720

/* 此demo只验证编码可行性 (将与屏幕尺寸相等raw图先编码后解码，再刷屏显示) */
void jpege_encode_decode_display_to_fb(void)
{
    struct fb_info fb_info;
    struct fb_handle *fb;
    struct backlight *lcd_pwm;
    struct jpege_encoder *encoder;
    struct jpege_encoder_output *en_out;
    struct jpegd_decoder *decoder;
    struct jpegd_decoder_output *de_out;


    // 初始化背光
    lcd_pwm = backlight_open(CONFIG_GPIO_BACKLIGHT0_NAME);
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

    /*----------------------编码 encode----------------------------------*/
    struct jpege_encoder_param en_param = {
        .width = WIDTH,
        .height = HEIGHT,
        .in_fmt = JPEGE_PIX_FMT_NV12,
        .quality = 90,
    };

    encoder = jpege_encoder_init(&en_param);
    if (!encoder)
        return;

    en_out = jpege_encoder_alloc_output_buf(encoder);

    int ret = jpege_encoder_encode(encoder, (void *)rawData, rawSize, en_out);
    if (ret < 0) {
        printf("jpeg encode failed\n");
        goto encode_err;
    }
    /*-----------解码 (数据直通fb, 图片宽和屏宽需相等, 直通fb仅支持BGRA)----------*/
    struct jpegd_decoder_param de_param = {
        .width = en_out->width,
        .height = en_out->height,
        .out_fmt = JPEGD_PIX_FMT_BGRA_8888,
        .crop = 0,
    };

    decoder = jpegd_decoder_init(&de_param);
    if (!decoder)
        return;

    de_out = (struct jpegd_decoder_output *)malloc(sizeof(struct jpegd_decoder_output));
    de_out->data = fb_info.fb_mem;
    de_out->data_size = fb_info.bytes_per_frame;

    ret = jpegd_decoder_decode(decoder, en_out->data, en_out->data_size, de_out);
    if (ret < 0) {
        printf("jpeg decode failed\n");
        goto decode_err;
    }
    /*------------------------------------------------------------------*/

    //刷屏,开启背光
    fb_pan_display(fb, 0);
    backlight_set_brightness(lcd_pwm, backlight_get_maxbrightness(lcd_pwm));


decode_err:
    free(de_out);
    jpegd_decoder_deinit(decoder);

encode_err:
    jpege_encoder_free_output_buf(en_out);
    jpege_encoder_deinit(encoder);
}