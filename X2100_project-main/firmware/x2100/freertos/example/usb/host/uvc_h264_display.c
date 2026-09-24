#include <stdio.h>
#include <os.h>
#include <driver/backlight.h>
#include <driver/fb.h>
#include <driver/camera.h>
#include <common.h>

#include <usb/host_uvc.h>
#include <little_things.h>

#include <felix/felix_h264_decoder.h>
#include <malloc.h>
#include <driver/cache.h>
#include <limits.h>

static thread_ptr_t uvc_h264_display_thread;

static void uvc_insert_wakeup_display(u32 devices_bit)
{
    thread_wakeup(uvc_h264_display_thread);
}

static int uvc_fb_init(struct fb_handle **fb, struct fb_info *fb_info)
{
    struct backlight *fb_backlight;

    fb_backlight = backlight_open("backlight_gpio0");
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

static int get_best_resolution_index(struct uvc_host_video *video, struct fb_info *fb_info, u8 *get_format_index, u8 *get_frame_index)
{
    u8 format_index = 0, frame_index = 0;
    u8 frame_index_best = 0, format_index_best = 0;
    int w_best = 0, h_best = 0;
    u8 format_index_mid = 0, frame_index_mid = 0;
    int w_mid = 0, h_mid = 0;
    u8 format_index_max = 0, frame_index_max = 0;
    int w_max = 0, h_max = 0;

    int w = 0;
    int h = 0;
    int fb_max_len = fb_info->xres > fb_info->yres ? fb_info->xres : fb_info->yres;
    int mid_min_len = INT_MAX;
    int min_len = 0;
    char *fmt = NULL;

    for (format_index = 0; format_index < video->nformats; format_index++) {
        for (frame_index = 0; frame_index < video->formats[format_index].nframes; frame_index++) {
            fmt = (char *)&(video->formats[format_index].fcc);
            w = video->formats[format_index].frames[frame_index].wWidth;
            h = video->formats[format_index].frames[frame_index].wHeight;
            printf("format_index[%d] frame_index[%d] w[%d] h[%d] fmt[%c%c%c%c]\n", format_index, frame_index,
                                                            w, h, fmt[0], fmt[1], fmt[2], fmt[3]);
            if (video->formats[format_index].fcc != V4L2_PIX_FMT_H264) {
                continue;
            }
            /* 检查是否为恰好的分辨率 */
            if (w == fb_info->xres && h == fb_info->yres) {
                format_index_best = format_index;
                frame_index_best = frame_index;
                w_best = w;
                h_best = h;
            }
            /* 找到一个可以完全覆盖屏幕的较小分辨率，在找不到恰好的分辨率时使用. */
            min_len = w > h ? h : w;
            if (min_len >= fb_max_len) {
                if (mid_min_len > min_len) {
                    format_index_mid = format_index;
                    frame_index_mid = frame_index;
                    mid_min_len = min_len;
                    w_mid = w;
                    h_mid = h;
                }
            }
            /* 记录下最大分辨率信息，在找不到能完全覆盖屏幕的分辨率时使用. */
            if (w >= w_max && h >= h_max) {
                format_index_max = format_index;
                frame_index_max = frame_index;
                w_max = w;
                h_max = h;
            }
        }
    }
    printf("max configs format_index[%d] frame_index[%d] w[%d] h[%d]\n", format_index_max, frame_index_max, w_max, h_max);
    printf("mid configs format_index[%d] frame_index[%d] w[%d] h[%d]\n", format_index_mid, frame_index_mid, w_mid, h_mid);
    printf("best configs format_index[%d] frame_index[%d] w[%d] h[%d]\n", format_index_best, frame_index_best, w_best, h_best);

    if (w_best) {
        format_index = format_index_best;
        frame_index = frame_index_best;
        w = w_best;
        h = h_best;
    }
    else if (w_mid) {
        format_index = format_index_mid;
        frame_index = frame_index_mid;
        w = w_mid;
        h = h_mid;
    } else if (w_max) {
        format_index = format_index_max;
        frame_index = frame_index_max;
        w = w_max;
        h = h_max;
    } else {
        /* 格式不匹配，导致没有记录分辨率 */
        printf("h264 format is not found in this uvc!\n");
        return -1;
    }

    *get_format_index = format_index;
    *get_frame_index = frame_index;
    return 0;
}

static int uvc_init(u8 uvc_dev_index, struct uvc_host_video *video, struct uvc_video_format *video_format, struct fb_info *fb_info)
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

    ret = get_best_resolution_index(video, fb_info, &format_index, &frame_index);
    if (ret) {
        printf("get_best_resolution_index fail!\n");
        goto err_close_uvc;
    }

    w = video->formats[format_index].frames[frame_index].wWidth;
    h = video->formats[format_index].frames[frame_index].wHeight;

    printf("select format_index[%d] frame_index[%d] w[%d] h[%d]\n", format_index, frame_index, w, h);

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
    printf("using uvc format_index[%d] frame_index[%d] w[%d] h[%d] fmt[%c%c%c%c]\n", format_index,
            frame_index, video_format->width, video_format->height, fmt[0], fmt[1], fmt[2], fmt[3]);

    if (!video_format->bpp)
        video_format->bpp = 8;

    frame_size = video_format->width * video_format->height * video_format->bpp / 8;
    printf("uvc request buf size = %d\n", frame_size);

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

static void uvc_h264_to_nv12_display_test(void *pdata)
{
    int i = 0;
    int ret = 0;
    u32 uvc_dev = 0;
    u8 disconnect = 0;
    u8 uvc_dev_index = 0;
    struct uvc_buffer *buf = NULL;
    struct uvc_host_video video = {0};
    struct uvc_video_format video_format= {0};

    void *nv12_y = NULL;
    void *nv12_uv = NULL;
    struct felix_h264_decoder *decoder = NULL;
    struct felix_h264_decoder_param h264_decoder_param = {0};

    struct fb_handle *fb = NULL;
    struct fb_info fb_info = {0};
    int display_w = 0, display_h = 0;

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
        ret = uvc_init(uvc_dev_index, &video, &video_format, &fb_info);
        if (ret) {
            i++;
            continue;
        }

        /* H264 解码要求 宽128对齐 */
        h264_decoder_param.width = ALIGN(video_format.width, 128);
        /* 某些 uvc设备获取出来的分辨率信息中高未对齐, 但其码流中的编码信息是对齐的 */
        /* 具体对齐大小需要根据运行时解码提示信息来确定, 避免缓冲区溢出 */
        h264_decoder_param.height = ALIGN(video_format.height, 16);
        decoder = felix_h264_decoder_init(&h264_decoder_param);
        if (!decoder) {
            printf("felix_h264_decoder_init fail!\n");
            goto err_stop_uvc;
        }
        printf("h264 decoder set [%dx%d]\n", h264_decoder_param.width, h264_decoder_param.height);

        /* H264 解码 y、uv数据存放地址要求256对齐，这里分开申请 */
        nv12_y = memalign(256, ALIGN(h264_decoder_param.width*h264_decoder_param.height, cache_line_size()));
        if (!nv12_y) {
            printf("malloc mem for nv12_y fail!\n");
            goto err_stop_decoder;
        }
        nv12_uv = memalign(256, ALIGN(h264_decoder_param.width*h264_decoder_param.height, cache_line_size()));
        if (!nv12_uv) {
            printf("malloc mem for nv12_uv fail!\n");
            goto err_stop_decoder;
        }

        /* 避免显示范围超出屏幕分辨率, 并与实际分辨率保持一致 */
        display_w = video_format.width > fb_info.xres ? fb_info.xres : video_format.width;
        display_h = video_format.height > fb_info.yres ? fb_info.yres : video_format.height;
        printf("display set [%dx%d]\n", display_w, display_h);

        disconnect = 0;
        while (1) {
            ret = usb_host_uvc_get_buffer(uvc_dev_index, &video, &buf, 1000);
            if (ret) {
                printf("uvc get buffer fail %d\n", ret);
                goto err_stop_decoder;
            }

            if (buf->state == UVC_BUF_STATE_ERROR && buf->bytesused == 0) {
                disconnect = 1;
            } else if (buf->state == UVC_BUF_STATE_DONE) {
                ret = felix_h264_decoder_decode_nv12_separate(decoder, buf->mem, buf->bytesused, nv12_y, nv12_uv);
                if (ret) {
                    printf("h264 decode frame fail, skip display!\n");
                    goto skip_display;
                }

                if (fb) {
                    layer_cfg.xres = display_w;
                    layer_cfg.yres = display_h;
                    layer_cfg.y.mem = (void *)virt_to_phys(nv12_y);
                    layer_cfg.uv.mem = (void *)virt_to_phys(nv12_uv);

                    layer_cfg.y.stride = h264_decoder_param.width;
                    layer_cfg.uv.stride = h264_decoder_param.width;

                    if (!fb_set_config(fb, &layer_cfg)) {
                        fb_enable_config(fb);
                        fb_pan_display(fb, 0);
                    }
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
err_put_buf:
        usb_host_uvc_put_buffer(uvc_dev_index, &video, buf);
err_stop_decoder:
        felix_h264_decoder_deinit(decoder);
// err_free_nv12:
        if (nv12_y) {
            free(nv12_y);
            nv12_y = NULL;
        }
        if (nv12_uv) {
            free(nv12_uv);
            nv12_uv = NULL;
        }
err_stop_uvc:
        usb_host_uvc_stream_off(uvc_dev_index, &video);
        usb_host_uvc_free_buffer(uvc_dev_index, &video);
        usb_host_uvc_close(uvc_dev_index);
        printf("uvc%d close\n", uvc_dev_index);
wait_wakeup:
        thread_wait();
    }
}

void uvc_h264_display_test(void)
{
    uvc_h264_display_thread = thread_create("uvc_h264_display_thread", 8*1024, uvc_h264_to_nv12_display_test, NULL);

    if (uvc_h264_display_thread)
        usb_host_uvc_register_callback(uvc_insert_wakeup_display);
}
