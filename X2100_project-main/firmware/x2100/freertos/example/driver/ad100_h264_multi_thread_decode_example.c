#include <stdio.h>
#include <stdlib.h>
#include <printf.h>
#include <common.h>
#include <malloc.h>
#include <assert.h>
#include <include_bin.h>
#include <driver/cache.h>

#include <driver/fb.h>
#include <driver/backlight.h>
#include <lib/nalu_buf.h>
#include <felix/felix_h264_decoder.h>


INCBIN(stream1, "example/resource/720_1252.h264");
#define WIDTH1 720
#define HEIGHT1 1252

INCBIN(stream2, "example/resource/test_720p.h264");
#define WIDTH2    1280
#define HEIGHT2   720

static int backlight_init(void)
{
    struct backlight *lcd_pwm;
    lcd_pwm = backlight_open("backlight_pwm0");
    if (!lcd_pwm)
        return -1;

    backlight_set_brightness(lcd_pwm, 100);

    return 0;
}

static void fb_display_video1(struct fb_handle *fb, struct felix_h264_output *out)
{
    struct fb_info fb_info;
    fb_get_info(fb, &fb_info);

    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xres = out->width - (out->crop_left + out->crop_right),
        .yres = out->height - (out->crop_top + out->crop_bottom),
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_0,
        .layer_enable = 1,

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },

        .y = {
            .mem = out->y_mem + out->crop_top * out->width + out->crop_left,
            .stride = out->width,
        },

        .uv = {
            .mem = out->uv_mem + (out->crop_top * out->width / 2) + out->crop_left,
            .stride = out->width,
        },
    };

    /* nv12数据 mem最低要求8字节对齐 */
    if (out->crop_left % 8) {
        printf("crop_left %d must be aligned in 8\n", out->crop_left);
        return;
    }

    if (out->crop_top % 2) {
        printf("crop_top %d must be aligned in 2\n", out->crop_top);
        return;
    }

    /* 检查显示窗口的宽 */
    if (layer_cfg.xres > 2047)
        layer_cfg.xres = 2047;

    /* 检查显示窗口的高 */
    if (layer_cfg.yres > 2047)
        layer_cfg.yres = 2047;

    if (layer_cfg.xres != fb_info.xres || layer_cfg.yres != fb_info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = fb_info.xres;
        layer_cfg.scaling.yres = fb_info.yres;
    }

    if (!fb_set_config(fb, &layer_cfg))
        fb_pan_display(fb, 0);
}

static void fb_display_video2(struct fb_handle *fb, struct felix_h264_output *out)
{
    struct fb_info fb_info;
    fb_get_info(fb, &fb_info);

    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xres = out->width - (out->crop_left + out->crop_right),
        .yres = out->height - (out->crop_top + out->crop_bottom),
        .xpos = fb_info.xres / 4,//居中显示
        .ypos = fb_info.yres / 4,//居中显示

        .layer_order = lcdc_layer_1,
        .layer_enable = 1,

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },

        .y = {
            .mem = out->y_mem + out->crop_top * out->width + out->crop_left,
            .stride = out->width,
        },

        .uv = {
            .mem = out->uv_mem + (out->crop_top * out->width / 2) + out->crop_left,
            .stride = out->width,
        },
    };

    /* nv12数据 mem最低要求8字节对齐 */
    if (out->crop_left % 8) {
        printf("crop_left %d must be aligned in 8\n", out->crop_left);
        return;
    }

    if (out->crop_top % 2) {
        printf("crop_top %d must be aligned in 2\n", out->crop_top);
        return;
    }

    /* 检查显示窗口的宽 */
    if (layer_cfg.xres > 2047)
        layer_cfg.xres = 2047;

    /* 检查显示窗口的高 */
    if (layer_cfg.yres > 2047)
        layer_cfg.yres = 2047;

    if (layer_cfg.xres != fb_info.xres || layer_cfg.yres != fb_info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = fb_info.xres / 2;
        layer_cfg.scaling.yres = fb_info.yres / 2;
    }

    if (!fb_set_config(fb, &layer_cfg))
        fb_pan_display(fb, 0);
}

static void decode_stream1(void *args)
{
    //注:只有fb0/fb1支持缩放
    struct fb_handle *fb = fb_open("fb0");
    assert(fb);

    fb_enable(fb);
    fb_enable_config(fb);

    struct felix_h264_decoder_param param = {
        .width = WIDTH1,
        .height = HEIGHT1,
    };

    struct felix_h264_decoder *decoder = felix_h264_decoder_init(&param);
    assert(decoder);

    struct felix_h264_output *out[2];
    out[0] = felix_h264_decoder_alloc_output_buf(decoder);
    out[1] = felix_h264_decoder_alloc_output_buf(decoder);
    assert(out[0] && out[1]);

    int decode_image_sz = ALIGN(param.width*param.height, cache_line_size());
    unsigned char *decode_image = (unsigned char *)memalign(256, decode_image_sz);
    assert(decode_image);

    int nalu_buf_sz = decode_image_sz;
    if (nalu_buf_sz < 128*1024)
        nalu_buf_sz = 128*1024;

    struct nalu_buf *nalu_buf = nalu_buf_init(nalu_buf_sz);
    struct nalu_unit nalu_unit = {0};
    assert(nalu_buf);

    void *file_data = (void *)stream1Data;
    int size = 0;
    int index = 0;
    int decode_len = 0;
    int write_size = 0;
    int ret = 0;
    int grop_len = 0;

    while (write_size < stream1Size) {
        /* 本次写入的长度大小 */
        size = stream1Size - write_size;
        ret = nalu_buf_write(nalu_buf, file_data+write_size, size);
        if (ret > 0)
            write_size += ret;

        /* 解析获取 nalu unit */
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

            ret = felix_h264_decoder_decode(decoder, decode_image, decode_len+grop_len, out[index]);
            grop_len = 0;
            if (ret)
                break;

            if (out[index]->got_frame) {
                fb_display_video1(fb, out[index]);
                index = !index;
            }
        }
    } /* end while(write_size < stream2Size) */

    grop_len = 0;
    /* 获取缓冲区中剩余的最后一个 nalu_unit, 因为是根据两个开始码(001\0001)之间来计算划分nalu_unit, 因此最后一个需要特殊处理 */
    decode_len = nalu_buf_read_tail(nalu_buf, &nalu_unit, decode_image, decode_image_sz);
    if (decode_len > 0)
        goto decode_nalu_unit;

    nalu_buf_deinit(nalu_buf);
    free(decode_image);

    felix_h264_decoder_free_output_buf(out[0]);
    felix_h264_decoder_free_output_buf(out[1]);

    felix_h264_decoder_deinit(decoder);
}

static void decode_stream2(void *args)
{
    //注:只有fb0/fb1支持缩放
    struct fb_handle *fb = fb_open("fb1");
    assert(fb);

    fb_enable(fb);
    fb_enable_config(fb);

    struct felix_h264_decoder_param param = {
        .width = WIDTH2,
        .height = HEIGHT2,
    };

    struct felix_h264_decoder *decoder = felix_h264_decoder_init(&param);
    assert(decoder);

    struct felix_h264_output *out[2];
    out[0] = felix_h264_decoder_alloc_output_buf(decoder);
    out[1] = felix_h264_decoder_alloc_output_buf(decoder);
    assert(out[0] && out[1]);

    int decode_image_sz = ALIGN(param.width*param.height, cache_line_size());
    unsigned char *decode_image = (unsigned char *)memalign(256, decode_image_sz);
    assert(decode_image);

    int nalu_buf_sz = decode_image_sz;
    if (nalu_buf_sz < 128*1024)
        nalu_buf_sz = 128*1024;

    struct nalu_buf *nalu_buf = nalu_buf_init(nalu_buf_sz);
    struct nalu_unit nalu_unit = {0};
    assert(nalu_buf);

    void *file_data = (void *)stream2Data;
    int size = 0;
    int index = 0;
    int decode_len = 0;
    int write_size = 0;
    int ret = 0;
    int grop_len = 0;

    while (write_size < stream2Size) {
        /* 本次写入的长度大小 */
        size = stream2Size - write_size;
        ret = nalu_buf_write(nalu_buf, file_data+write_size, size);
        if (ret > 0)
            write_size += ret;

        /* 解析获取 nalu unit */
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

            ret = felix_h264_decoder_decode(decoder, decode_image, decode_len+grop_len, out[index]);
            grop_len = 0;
            if (ret)
                continue;

            if (out[index]->got_frame) {
                fb_display_video2(fb, out[index]);
                index = !index;
            }
        }
    } /* end while(write_size < stream2Size) */

    grop_len = 0;
    /* 获取缓冲区中剩余的最后一个 nalu_unit, 因为是根据两个开始码(001\0001)之间来计算划分nalu_unit, 因此最后一个需要特殊处理 */
    decode_len = nalu_buf_read_tail(nalu_buf, &nalu_unit, decode_image, decode_image_sz);
    if (decode_len > 0)
        goto decode_nalu_unit;

    nalu_buf_deinit(nalu_buf);
    free(decode_image);

    felix_h264_decoder_free_output_buf(out[0]);
    felix_h264_decoder_free_output_buf(out[1]);

    felix_h264_decoder_deinit(decoder);

    fb_disable_config(fb);
    fb_disable(fb);
}


void ad100_h264_multi_thread_decode_and_display(void)
{
    backlight_init();

    thread_create("decode display1", 4096, decode_stream1, NULL);
    thread_create("decode display2", 4096, decode_stream2, NULL);
}