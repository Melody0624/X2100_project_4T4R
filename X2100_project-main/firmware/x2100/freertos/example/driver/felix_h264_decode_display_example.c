#include <stdio.h>
#include <printf.h>
#include <common.h>
#include <os.h>
#include <stdlib.h>
#include <malloc.h>
#include <include_bin.h>
#include <assert.h>
#include <driver/cache.h>

#include <felix/felix_h264_decoder.h>
#include <lib/nalu_buf.h>

#include <driver/backlight.h>
#include <devices/pwm_backlight.h>

#include <driver/fb.h>
#include <common.h>

/**
 * 提取视频文件中的h264 流可在pc端用ffmpeg 命令实现：
 * ffmpeg -i input.mp4 -c:v libx264 -profile:v baseline -an -f h264 output.h264
**/

// #define DECODE_1080

#ifdef DECODE_1080
INCBIN(h264, "example/resource/test_1080p.h264");
#define VIDEO_WIDTH    1920
#define VIDEO_HEIGHT   1088
#else
INCBIN(h264, "example/resource/test_720p.h264");
#define VIDEO_WIDTH    1280
#define VIDEO_HEIGHT   720
#endif


static struct fb_info fb_info;
static struct fb_handle *fb;

static void fb_display_video(int width, int height, void *y)
{
    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xres = VIDEO_WIDTH,
        .yres = VIDEO_HEIGHT,
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_0,
        .layer_enable = 1,

        .y = {
            .mem = y,
            .stride = width,
        },

        .uv = {
            .mem = y + width * height,
            .stride = width,
        },

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },
    };

    if (layer_cfg.xres != fb_info.xres || layer_cfg.yres != fb_info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = fb_info.xres;
        layer_cfg.scaling.yres = fb_info.yres;
    }

    if (!fb_set_config(fb, &layer_cfg))
        fb_pan_display(fb, 0);
}

void felix_h264_decode_and_display_test(void)
{
    fb = fb_open("fb0");
    if (fb == NULL) {
        printf("open fb0 error!\n");
        return;
    }

    fb_enable(fb);
    fb_get_info(fb, &fb_info);
    fb_enable_config(fb);

    struct backlight *lcd_pwm;
    lcd_pwm = backlight_open("backlight_pwm0");
    if (lcd_pwm) {
        backlight_set_brightness(lcd_pwm, 100);
    }

    struct felix_h264_decoder_param param = {
        .width =  ALIGN(VIDEO_WIDTH, 128),
        .height = ALIGN(VIDEO_HEIGHT, 16),
    };

    struct felix_h264_decoder *decoder = felix_h264_decoder_init(&param);
    assert(decoder);

    struct felix_h264_output *nv12[2];
    nv12[0] = memalign(256, ALIGN(param.width*param.height*3/2, cache_line_size()));
    nv12[1] = memalign(256, ALIGN(param.width*param.height*3/2, cache_line_size()));
    assert(nv12[0] && nv12[1]);

    int decode_image_sz = ALIGN(VIDEO_WIDTH * VIDEO_HEIGHT, cache_line_size());
    unsigned char *decode_image = (unsigned char *)memalign(256, decode_image_sz);
    assert(decode_image);

    int nalu_buf_sz = decode_image_sz;
    if (nalu_buf_sz < 128*1024)
        nalu_buf_sz = 128*1024;

    struct nalu_buf *nalu_buf = nalu_buf_init(nalu_buf_sz);
    struct nalu_unit nalu_unit = {0};
    assert(nalu_buf);

    unsigned char *data = (unsigned char *)h264Data;
    int size = 0;
    int count = 0;
    int decode_len = 0;
    int write_len = 0;
    int ret = 0;
    int grop_len = 0;

    while (write_len < h264Size) {
        /* 本次写入的长度大小 */
        size = h264Size - write_len;
        ret = nalu_buf_write(nalu_buf, data+write_len, size);
        if (ret > 0)
            write_len += ret;

        /* 解析 nalu unit */
        while(1) {
            decode_len = nalu_buf_read_unit(nalu_buf, &nalu_unit, decode_image+grop_len, decode_image_sz-grop_len);
            if (decode_len <= 0)
                break;

decode_nalu_unit:
            /* 仅I帧/P帧为解码帧，其他均不可直接解码 */
            if (nalu_unit.nal_unit_type != NALU_TYPE_IDR && nalu_unit.nal_unit_type != NALU_TYPE_SLICE) {
                /* sps/pps需要累加, 其他类型则忽略不处理*/
                if (nalu_unit.nal_unit_type == NALU_TYPE_SPS || nalu_unit.nal_unit_type == NALU_TYPE_PPS)
                    grop_len += decode_len;

                continue;
            }

            ret = felix_h264_decoder_decode(decoder, decode_image, grop_len+decode_len, nv12[count]);
            grop_len = 0;
            if (ret) {
                printf("decode fail, skip display!\n");
                continue;
            }

            fb_display_video(VIDEO_WIDTH, VIDEO_HEIGHT, nv12[count]);
            count = !count;
        }
    }

    grop_len = 0;
    /* 获取缓冲区中剩余的最后一个 nalu_unit, 因为是根据两个开始码(001\0001)之间来计算划分nalu_unit, 因此最后一个需要特殊处理 */
    decode_len = nalu_buf_read_tail(nalu_buf, &nalu_unit, decode_image, decode_image_sz);
    if (decode_len > 0)
        goto decode_nalu_unit;

    felix_h264_decoder_deinit(decoder);

    nalu_buf_deinit(nalu_buf);
    free(decode_image);

    fb_disable(fb);

    free(nv12[0]);
    free(nv12[1]);
}
