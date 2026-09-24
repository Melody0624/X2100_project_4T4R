#include <stdio.h>
#include <os.h>
#include <driver/backlight.h>
#include <driver/fb.h>
#include <driver/camera.h>
#include <common.h>

#include <usb/host_uvc.h>
#include <little_things.h>

#include <jpegd_decoder.h>
#include <malloc.h>
#include <driver/cache.h>
#include <limits.h>

#if CONFIG_JPEG_TURBO
#include "jpeg-turbo/turbojpeg.h"

#define ALIGN_DOWN(X, n) ((X) - ((X)%(n)))

static tjhandle handle = NULL;
#endif
/*
    某些场景下解码和刷屏同时使用buf，可能会出现显示异常。
    可尝试启用 DECODE_USE_OTHER_BUF，解码和刷屏将使用不同的buf。
*/
// #define DECODE_USE_OTHER_BUF

#ifdef DECODE_USE_OTHER_BUF
struct jpegd_decoder_output *nv12[2];
#else
struct jpegd_decoder_output *nv12[1];
#endif

static thread_ptr_t uvc_jpeg_display_thread;

static void uvc_insert_wakeup_display(u32 devices_bit)
{
    thread_wakeup(uvc_jpeg_display_thread);
}

static int uvc_fb_init(struct fb_handle **fb, struct fb_info *fb_info)
{
    struct backlight *fb_backlight;

    fb_backlight = backlight_open("backlight_pwm0");
    if (!fb_backlight)
        printf("backlight_open fail.\n");
    else
        backlight_set_brightness(fb_backlight, fb_backlight->max_brightness);

    *fb = fb_open("fb0");
    if (!(*fb)) {
        printf("open fb0 error!\n");
        return -1;
    }

    fb_enable(*fb);
    memset(fb_info, 0, sizeof(struct fb_info));
    fb_get_info(*fb, fb_info);

    printf("opened fb w[%d] h[%d]\n", fb_info->xres, fb_info->yres);
    return 0;
}

static int get_fmt_first_index(struct uvc_host_video *video, unsigned int need_format, u8 *get_format_index, u8 *get_frame_index)
{
    int w = 0;
    int h = 0;
    char *fmt = NULL;
    int format_index = 0, frame_index = 0;
    int format_firts_index = -1, frame_firts_index = -1;

    /* 找到所需格式的首个分辨率 */
    for (format_index = 0; format_index < video->nformats; format_index++) {
        for (frame_index = 0; frame_index < video->formats[format_index].nframes; frame_index++) {
            fmt = (char *)&(video->formats[format_index].fcc);
            w = video->formats[format_index].frames[frame_index].wWidth;
            h = video->formats[format_index].frames[frame_index].wHeight;

            printf("\tformat[%d] frame[%d] w[%d] h[%d] fmt[%s]\n", format_index, frame_index, w, h, fmt);

            if (video->formats[format_index].fcc != need_format)
                continue;

            if ((format_firts_index == -1) || (frame_firts_index == -1)) {
                format_firts_index = format_index;
                frame_firts_index = frame_index;
            }
        }
    }

    if ((format_firts_index == -1) || (frame_firts_index == -1)) {
        /* 格式不匹配，导致没有记录分辨率 */
        fmt = (char *)&need_format;
        printf("[%s] format is not found in this uvc!\n", fmt);
        return -1;
    }

    *get_format_index = format_firts_index;
    *get_frame_index = frame_firts_index;
    return 0;
}

static int uvc_init(u8 uvc_dev_index, struct uvc_host_video *video, struct uvc_video_format *video_format)
{
    int ret = 0;
    char *fmt = NULL;
    int w = 0, h = 0;
    u32 frame_size = 0;
    u8 format_index = 0, frame_index = 0;

    ret = usb_host_uvc_open(uvc_dev_index, NULL);
    if (ret) {
        printf("usb_host_uvc_open uvc[%d] fail!\n", uvc_dev_index);
        return ret;
    }

    printf("opened dev[%d] video num %d\n", uvc_dev_index, usb_host_uvc_get_video_num(uvc_dev_index));

    memset(video, 0, sizeof(struct uvc_host_video));
    ret = usb_host_uvc_get_video(uvc_dev_index, 0, video);
    if (ret) {
        printf("uvc get video fail %d\n", ret);
        goto err_close_uvc;
    }

    ret = get_fmt_first_index(video, V4L2_PIX_FMT_MJPEG, &format_index, &frame_index);
    if (ret) {
        printf("get_fmt_first_index fail!\n");
        goto err_close_uvc;
    }

    w = video->formats[format_index].frames[frame_index].wWidth;
    h = video->formats[format_index].frames[frame_index].wHeight;
    printf("select format[%d] frame[%d] w[%d] h[%d]\n", format_index, frame_index, w, h);

    ret = usb_host_uvc_set_format(uvc_dev_index, video, format_index, frame_index, 0);
    if (ret) {
        printf("uvc set format fail %d\n", ret);
        goto err_close_uvc;
    }

    memset(video_format, 0, sizeof(struct uvc_video_format));
    ret = usb_host_uvc_get_format(uvc_dev_index, video, video_format);
    if (ret) {
        printf("uvc get format fail %d\n", ret);
        goto err_close_uvc;
    }

    fmt = (char *)&(video_format->fcc);
    printf("using uvc format[%d] frame[%d] w[%d] h[%d] fmt[%s]\n", format_index,
            frame_index, video_format->width, video_format->height, fmt);

    if (!video_format->bpp)
        video_format->bpp = 8;

    frame_size = video_format->width * video_format->height * video_format->bpp / 8;

    ret = usb_host_uvc_request_buffer(uvc_dev_index, video, 3, frame_size);
    if (ret) {
        printf("uvc request buffer fail %d\n", ret);
        goto err_close_uvc;
    }

    ret = usb_host_uvc_stream_on(uvc_dev_index, video);
    if (ret) {
        printf("uvc stream on fail %d\n", ret);
        goto err_free_buffer;
    }
    return ret;

err_free_buffer:
    usb_host_uvc_free_buffer(uvc_dev_index, video);
err_close_uvc:
    usb_host_uvc_close(uvc_dev_index);
    printf("uvc%d close\n", uvc_dev_index);
    return ret;
}

#ifdef CONFIG_JPEG_TURBO
static int turbo_jpeg_decoder_decode(void *src_buf, unsigned int src_size,
                                void *dst_buf, int *width, int *height)
{
    int ret;

    if (!src_buf || !dst_buf || src_size <= 0)
        return -EINVAL;

    if (!handle)
        handle = tjInitDecompress();

    tjDecompressHeader(handle, src_buf, src_size, width, height);

    ret = tjDecompressToYUV2(handle, (unsigned char *) src_buf, src_size,
                            (unsigned char *) dst_buf, *width, 4, *height, TJFLAG_FASTDCT);

    if (ret < 0)
        ret = tjGetErrorCode(handle);

    if (ret != 0) {
        printf("[%d]: %s\n", tjGetErrorCode(handle), tjGetErrorStr2(handle));

        tjDestroy(handle);
        handle = NULL;
    }

    return ret;
}

static void copy_raw_bytes(void *dst, int dst_linesize, void *src, int src_linesize, int height, int width)
{
    if (dst == src && dst_linesize == src_linesize)
        return;

    if (dst_linesize == src_linesize && width == dst_linesize) {
        memcpy(dst, src, width*height);
        return;
    }

    int i;
    for (i = 0; i < height; i++) {
        memcpy(dst, src, width);
        src += src_linesize;
        dst += dst_linesize;
    }
}

static int yuv422p_to_nv12(uint8_t **s_data, uint32_t *s_linesize,
                           uint8_t **d_data, uint32_t *d_linesize, int width, int height)
{
    int i,j;

    uint8_t *dst = d_data[0];
    int dst_linesize = d_linesize[0];

    void *src = s_data[0];
    int src_linesize = s_linesize[0];

    copy_raw_bytes(dst, dst_linesize, src, src_linesize, height, width);

    dst_linesize = d_linesize[1];

    src_linesize = s_linesize[1];
    uint8_t *u0 = s_data[1];
    uint8_t *u1 = s_data[1] + src_linesize;
    uint8_t *uv = d_data[1];

    for (i = 0; i < height/2; i++) {
        for (j = 0; j < ALIGN_DOWN(width/2, 4); j+=4) {
            uint8_t a0 = u0[j+0];
            uint8_t a1 = u0[j+1];
            uint8_t a2 = u0[j+2];
            uint8_t a3 = u0[j+3];
            uint8_t b0 = u1[j+0];
            uint8_t b1 = u1[j+1];
            uint8_t b2 = u1[j+2];
            uint8_t b3 = u1[j+3];

            uv[j*2+0] = ((unsigned short)a0 + b0)/2;
            uv[j*2+2] = ((unsigned short)a1 + b1)/2;
            uv[j*2+4] = ((unsigned short)a2 + b2)/2;
            uv[j*2+6] = ((unsigned short)a3 + b3)/2;
        }
        for (; j < width/2; j++)
            uv[j*2] = ((unsigned short)u0[j] + u1[j])/2;

        u0 += src_linesize;
        u1 += src_linesize;
        uv += dst_linesize;
    }

    src_linesize = s_linesize[2];
    uint8_t *v0 = s_data[2];
    uint8_t *v1 = s_data[2] + src_linesize;
    uv = d_data[1] + 1;

    for (i = 0; i < height/2; i++) {
        for (j = 0; j < ALIGN_DOWN(width/2, 4); j+=4) {
            uint8_t a0 = v0[j+0];
            uint8_t a1 = v0[j+1];
            uint8_t a2 = v0[j+2];
            uint8_t a3 = v0[j+3];
            uint8_t b0 = v1[j+0];
            uint8_t b1 = v1[j+1];
            uint8_t b2 = v1[j+2];
            uint8_t b3 = v1[j+3];

            uv[j*2+0] = ((unsigned short)a0 + b0)/2;
            uv[j*2+2] = ((unsigned short)a1 + b1)/2;
            uv[j*2+4] = ((unsigned short)a2 + b2)/2;
            uv[j*2+6] = ((unsigned short)a3 + b3)/2;
        }
        for (; j < width/2; j++)
            uv[j*2] = ((unsigned short)v0[j] + v1[j])/2;

        v0 += src_linesize;
        v1 += src_linesize;
        uv += dst_linesize;
    }

    return 0;
}

static void convert_yuv422p_to_nv12(void *src_buf, void *dst_buf, int width, int height)
{
    uint8_t *s_data[3], *d_data[2];
    uint32_t s_linesize[3], d_linesize[2];

    s_data[0] = (uint8_t *)src_buf;
    s_linesize[0] = width;

    s_data[1] = s_data[0] + width * height;
    s_linesize[1] = width;

    s_data[2] = s_data[1] + (width/2) * height;
    s_linesize[2] = width;

    d_data[0] = (uint8_t *)dst_buf;
    d_linesize[0] = width;

    d_data[1] = d_data[0] + width * height;
    d_linesize[1] = width;

    yuv422p_to_nv12(s_data, s_linesize, d_data, d_linesize, width, height);
}
#endif

static void uvc_jpeg_to_nv12_display_test(void *pdata)
{
    int i = 0;
    int ret = 0;
    u32 uvc_dev = 0;
    u8 disconnect = 0;
    u8 uvc_dev_index = 0;
    struct uvc_buffer *buf = NULL;
    struct uvc_host_video video = {0};
    struct uvc_video_format video_format= {0};

    int count = 0;
    int buf_index = 0;
    struct jpegd_decoder *decoder = NULL;
    int buf_num = sizeof(nv12) / sizeof (void *);
    struct jpegd_decoder_output *decode_out = NULL;
    struct jpegd_decoder_param jpeg_decoder_param = {0};

    struct fb_handle *fb = NULL;
    struct fb_info fb_info = {0};

    u8 *jpeg_code = NULL;

    ret = uvc_fb_init(&fb, &fb_info);
    if (ret) {
        printf("uvc_fb_init fail!\n");
        return ;
    }

    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_bottom,
        .layer_enable = 1,

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },

        /* 将解码的图像内容拉伸显示到屏幕 */
        .scaling.enable = 1,
        .scaling.xres = fb_info.xres,
        .scaling.yres = fb_info.yres,
    };


    i = 0;
    while (1) {
        uvc_dev = usb_host_uvc_get_devices_bit();
        for (; i < 32; i++) {
            if (uvc_dev & (1 << i)) {
                break;
            }
        }

        if (i == 32) {
            i = 0;
            goto wait_wakeup;
        }

        uvc_dev_index = i;
        mdelay(500);
        ret = uvc_init(uvc_dev_index, &video, &video_format);
        if (ret) {
            i++;
            continue;
        }

        jpeg_decoder_param.width = video_format.width;
        jpeg_decoder_param.height = video_format.height;
        jpeg_decoder_param.out_fmt = JPEGD_PIX_FMT_NV12,

        decoder = jpegd_decoder_init(&jpeg_decoder_param);
        if (!decoder) {
            printf("helix_jpeg_decoder_init fail!\n");
            goto err_stop_uvc;
        }
        printf("jpeg decoder set [%dx%d]\n", jpeg_decoder_param.width, jpeg_decoder_param.height);

        for (buf_index = 0; buf_index < buf_num; buf_index++) {
            nv12[buf_index] = jpegd_decoder_alloc_output_buf(decoder);
            if (!nv12[buf_index]) {
                printf("malloc mem for nv12[%d] fail!\n", buf_index);
                goto err_stop_decoder;
            }
        }
        printf("using nv12 buf num[%d]!\n", buf_num);

#ifdef CONFIG_JPEG_TURBO
        int turbo_decode_width = 0;
        int turbo_decode_height = 0;
        unsigned int turbo_decode_data_size = tjBufSizeYUV2(video_format.width, 4, video_format.height, TJSAMP_422);
        void *turbo_decode_data = memalign(4096, ALIGN(turbo_decode_data_size, cache_line_size()));
        if (!turbo_decode_data)
            printf("malloc mem for turbo-jpeg decoder fail!\n");
#endif

        count = 0;
        disconnect = 0;
        while (1) {
            ret = usb_host_uvc_get_buffer(uvc_dev_index, &video, &buf, 5000);
            if (ret) {
                printf("uvc get buffer fail %d\n", ret);
                goto err_stop_decoder;
            }

            if (buf->state == UVC_BUF_STATE_ERROR && buf->bytesused == 0) {
                disconnect = 1;
            } else if (buf->state == UVC_BUF_STATE_DONE) {
                /* 交替使用不同buf解码刷屏 */
                buf_num > 1 ? (decode_out = nv12[count%2]) : (decode_out = nv12[0]);

                jpeg_code = buf->mem;
                if (jpeg_code[0] != 0xff || jpeg_code[1] != 0xd8)
                    goto skip_display;

                /* 判断JPEG数据是否以 0xffd9 结尾 */
                if (jpeg_code[buf->bytesused-2] != 0xff || jpeg_code[buf->bytesused-1] != 0xd9) {
                    /* 部分UVC设备在JPEG数据末尾补 0x00, 更正该类设备的实际传输数据长度 */
                    if (jpeg_code[buf->bytesused-3] == 0xff && \
                        jpeg_code[buf->bytesused-2] == 0xd9) {
                            buf->bytesused -= 1;
                            goto display;
                        }
                    goto skip_display;
                }
display:
                ret = jpegd_decoder_decode(decoder, buf->mem, buf->bytesused, decode_out);

                if (ret) {
#ifndef CONFIG_JPEG_TURBO
                    printf("jpeg decode frame fail, skip display!\n");
                    goto skip_display;
#else
                    if (!turbo_decode_data) {
                        printf("jpeg decode frame fail, skip display!\n");
                        goto skip_display;
                    }

                    printf("jpeg decode frame fail, try software decoder!\n");

                    ret = turbo_jpeg_decoder_decode(buf->mem, buf->bytesused,
                        turbo_decode_data, &turbo_decode_width, &turbo_decode_height);

                    if (ret < 0)
                        goto skip_display;

                    decode_out->width = turbo_decode_width;
                    decode_out->height = turbo_decode_height;
                    decode_out->actual_width = turbo_decode_width;
                    decode_out->actual_height = turbo_decode_height;
                    convert_yuv422p_to_nv12(turbo_decode_data, decode_out->data,
                        decode_out->width, decode_out->height);
#endif
                }

                if (fb) {
                    layer_cfg.xres = decode_out->actual_width;
                    layer_cfg.yres = decode_out->actual_height;

                    if (layer_cfg.xres > 2047)
                        layer_cfg.xres = 2047;
                    if (layer_cfg.yres > 2047)
                        layer_cfg.yres = 2047;

                    layer_cfg.y.mem = (void *)virt_to_phys(decode_out->data);
                    layer_cfg.uv.mem = (void *)virt_to_phys(decode_out->data)+decode_out->width*decode_out->height;

                    layer_cfg.y.stride = decode_out->width;
                    layer_cfg.uv.stride = decode_out->width;

                    if (!fb_set_config(fb, &layer_cfg)) {
                        fb_enable_config(fb);
                        fb_pan_display(fb, 0);
                    }

                    count++;
                }
            }

skip_display:
            ret = usb_host_uvc_put_buffer(uvc_dev_index, &video, buf);
            if (ret) {
                printf("uvc put buffer fail %d\n", ret);
                goto err_stop_decoder;
            }

            if (disconnect)
                goto err_stop_decoder;
        }

        usb_host_uvc_put_buffer(uvc_dev_index, &video, buf);
err_stop_decoder:
#ifdef CONFIG_JPEG_TURBO
        if (turbo_decode_data)
            free(turbo_decode_data);

        if (handle) {
            tjDestroy(handle);
            handle = NULL;
        }
#endif
        jpegd_decoder_deinit(decoder);
        for (buf_index--; buf_index >= 0; buf_index--)
            jpegd_decoder_free_output_buf(nv12[buf_index]);
err_stop_uvc:
        usb_host_uvc_stream_off(uvc_dev_index, &video);
        usb_host_uvc_free_buffer(uvc_dev_index, &video);
        usb_host_uvc_close(uvc_dev_index);
        printf("uvc%d close\n", uvc_dev_index);
wait_wakeup:
        thread_wait();
    }
}

void uvc_jpeg_display_test(void)
{
    uvc_jpeg_display_thread = thread_create("uvc_jpeg_display_thread", 8*1024, uvc_jpeg_to_nv12_display_test, NULL);
    if (uvc_jpeg_display_thread)
        usb_host_uvc_register_callback(uvc_insert_wakeup_display);
}
