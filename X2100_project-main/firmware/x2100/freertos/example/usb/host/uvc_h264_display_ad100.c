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

#include <lib/nalu_buf.h>

/*
    某些场景下解码和刷屏同时使用buf，可能会出现显示异常。
    可尝试启用 DECODE_USE_OTHER_BUF，解码和刷屏将使用不同的buf。
*/
// #define DECODE_USE_OTHER_BUF

#ifdef DECODE_USE_OTHER_BUF
struct felix_h264_output *decode_output[2];
#else
struct felix_h264_output *decode_output[1];
#endif

static thread_ptr_t uvc_h264_display_thread;

static void uvc_insert_wakeup_display(u32 devices_bit)
{
    thread_wakeup(uvc_h264_display_thread);
}

static struct fb_handle *uvc_fb_init(const char *fb_name, const char *backlight_name)
{
    struct fb_handle *fb;
    struct backlight *fb_backlight;

    assert(backlight_name);
    fb_backlight = backlight_open(backlight_name);
    if (!fb_backlight)
        printf("backlight_open fail.\n");
    else
        backlight_set_brightness(fb_backlight, fb_backlight->max_brightness);

    assert(fb_name);
    fb = fb_open(fb_name);
    if (!fb) {
        printf("open fb error!\n");
        return NULL;
    }

    fb_enable(fb);
    fb_enable_config(fb);
    return fb;
}

static void uvc_fb_pan_display(struct fb_handle *fb, struct felix_h264_output *out)
{
    struct fb_info fb_info;

    fb_get_info(fb, &fb_info);

    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xres = out->width - (out->crop_left + out->crop_right),
        .yres = out->height - (out->crop_top + out->crop_bottom),
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_bottom,
        .layer_enable = 1,

        .y = {
            .mem = out->y_mem + out->crop_top * out->width + out->crop_left,
            .stride = out->width,
        },

        .uv = {
            .mem = out->uv_mem + (out->crop_top * out->width / 2) + out->crop_left,
            .stride = out->width,
        },

        .alpha = {
            .enable = 0,
            .value = 0xff,
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

    /* 将解码的图像内容拉伸显示到屏幕 */
    if (layer_cfg.xres != fb_info.xres || layer_cfg.yres != fb_info.yres) {
        layer_cfg.scaling.enable = 1;
        layer_cfg.scaling.xres = fb_info.xres;
        layer_cfg.scaling.yres = fb_info.yres;
    }

    if (!fb_set_config(fb, &layer_cfg))
        fb_pan_display(fb, 0);
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

    ret = get_fmt_first_index(video, V4L2_PIX_FMT_H264, &format_index, &frame_index);
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

int parse_nal_unit_info(nal_t *nalu_info, unsigned char *data, unsigned int data_len,
                 int *h264_w, int *h264_h)
{
    int ret = 0;

    /* 临时数据用于处理nalu数据 */
    unsigned char *rbsp_buf = malloc(data_len);
    if (!rbsp_buf) {
        printf("malloc rbsp_buf fail\n");
        return -ENOMEM;
    }

    /* 剔除防止竞争字，解析 SPS PPS 的更多的信息 */
    ret = nalu_data_parse(nalu_info, data, data_len, rbsp_buf, data_len);
    if (ret) {
        printf("The H264 data of this uvc device is illegal!\n");
        free(rbsp_buf);
        return ret;
    }

    free(rbsp_buf);

    /* 解析H264分辨率信息依赖前面的 nalu_data_parse 解析结果 */
    nalu_sps_parse_wh(&nalu_info->sps, h264_w, h264_h);
    printf("parsed H264 w[%d] h[%d] fps[%d]\n", *h264_w, *h264_h, nalu_sps_parse_fps(&nalu_info->sps));
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

    int count = 0;
    int buf_index = 0;
    struct felix_h264_output *output = NULL;
    struct felix_h264_decoder *decoder = NULL;
    struct felix_h264_decoder_param h264_decoder_param = {0};
    int buf_num = sizeof(decode_output) / sizeof (struct felix_h264_output *);

    struct fb_handle *fb = uvc_fb_init("fb0", "backlight_pwm0");
    if (!fb) {
        printf("uvc_fb_init fail!\n");
        return;
    }

    unsigned char *src_image = NULL;
    int src_image_len = 0;
    int nalu_len = 0;
    void *decode_src = NULL;

    struct nalu_buf *nalu_buf = NULL;
    struct nalu_unit nalu_unit = {0};
    int h264_w = 0;
    int h264_h = 0;
    int write_len = 0;
    int read_len = 0;
    int check_h264 = 0;
    nal_t nalu_info = {0};
    int grop_len = 0;
    int wait_for_idr = 0;

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
        ret = uvc_init(uvc_dev_index, &video, &video_format);
        if (ret) {
            i++;
            continue;
        }

        /* AD100 H264 解码要求 宽/高 16对齐 */
        h264_decoder_param.width = ALIGN(video_format.width, 16);
        /* 某些 uvc设备获取出来的分辨率信息中高未对齐, 但其码流中的编码信息是对齐的 */
        h264_decoder_param.height = ALIGN(video_format.height, 16);
        decoder = felix_h264_decoder_init(&h264_decoder_param);
        if (!decoder) {
            printf("felix_h264_decoder_init fail!\n");
            goto err_stop_uvc;
        }
        printf("h264 decoder set [%dx%d]\n", h264_decoder_param.width, h264_decoder_param.height);

        src_image_len = ALIGN(h264_decoder_param.width * h264_decoder_param.height, cache_line_size());
        nalu_len = src_image_len;
        if (nalu_len < 128*1024)
            nalu_len = 128*1024;

        /* 初始化划分nalu_unit的缓冲区 */
        nalu_buf = nalu_buf_init(nalu_len);
        if (!nalu_buf) {
            printf("nalu_buf_init fail!\n");
            goto err_stop_decoder;
        }

        /* 这里先根据枚举得到的分辨率信息进行申请空间，作为解码的源图片存储空间，解码器要求该地址256对齐 */
        src_image = (unsigned char *)memalign(256, src_image_len);
        if (!src_image) {
            printf("src_image alloc fail!\n");
            goto err_deinit_nalu_buf;
        }

        for (buf_index = 0; buf_index < buf_num; buf_index++) {
            decode_output[buf_index] = felix_h264_decoder_alloc_output_buf(decoder);
            if (!decode_output[buf_index]) {
                printf("malloc mem for decode_output[%d] fail!\n", buf_index);
                goto err_free_output_buf;
            }
        }
        printf("using nv12 buf num[%d]!\n", buf_num);

        count = 0;
        h264_w = 0;
        h264_h = 0;
        grop_len = 0;
        write_len = 0;
        disconnect = 0;
        check_h264 = 0;
        wait_for_idr = 1;
        while (1) {
            /* 一些uvc设备提供第一帧时间可能会较长 */
            ret = usb_host_uvc_get_buffer(uvc_dev_index, &video, &buf, 5000);
            if (ret) {
                printf("uvc get buffer fail %d\n", ret);
                goto err_free_output_buf;
            }

            if (buf->state == UVC_BUF_STATE_ERROR && buf->bytesused == 0) {
                disconnect = 1;
            } else if (buf->state == UVC_BUF_STATE_DONE) {
                /* 交替使用不同buf解码刷屏 */
                if (buf_num > 1)
                    output = decode_output[count%2];
                else
                    output = decode_output[0];

                write_len = 0;
                while (write_len < buf->bytesused) {

                    ret = nalu_buf_write(nalu_buf, buf->mem+write_len, buf->bytesused-write_len);
                    write_len += ret;

                    /* 尽可能将所有可用的nalu消费掉，保证图像时效性、以及缓冲区足够后续的数据写入解析 */
                    while (1) {
                        /* 这里 src_image_len 传入代表接收缓冲的长度 */
                        read_len = nalu_buf_read_unit(nalu_buf, &nalu_unit, src_image+grop_len, src_image_len-grop_len);
                        if (read_len <= 0)
                            break;

                        /* 只做一次H264数据解析，获取分辨率信息 */
                        if (!check_h264) {
                            if (nalu_unit.nal_unit_type == NALU_TYPE_SPS) {
                                ret = parse_nal_unit_info(&nalu_info, src_image, read_len, &h264_w, &h264_h);
                                if (ret)
                                    goto err_put_uvc_buf;

                                /* 某些uvc设备枚举出的分辨率信息和其实际的H264流中给出的h264流分辨率新有出入，请确认 */
                                if (h264_decoder_param.width < h264_w || h264_decoder_param.height < h264_h) {
                                    /* 解析出来的H264分辨率等信息和枚举的不一致 */
                                    printf("h264 decoder using w[%d] h[%d], but actually h264 w[%d] h[%d]!\n",
                                            h264_decoder_param.width, h264_decoder_param.height, h264_w, h264_h);
                                    goto err_put_uvc_buf;
                                }

                                check_h264 = 1;
                            }
                        }


                        /* 仅I帧/P帧为解码帧，其他均不可直接解码 */
                        if (nalu_unit.nal_unit_type != NALU_TYPE_IDR && nalu_unit.nal_unit_type != NALU_TYPE_SLICE) {
                            /* sps/pps需要累加, 其他类型则忽略不处理*/
                            if (nalu_unit.nal_unit_type == NALU_TYPE_SPS || nalu_unit.nal_unit_type == NALU_TYPE_PPS)
                                grop_len += read_len;

                            continue;
                        }

                        if (!check_h264) {
                            grop_len = 0;
                            continue;
                        }

                        if (wait_for_idr) {
                            /* 解码出错后等待下一帧IDR，SPS/PPS已在上面累加 */
                            if (nalu_unit.nal_unit_type == NALU_TYPE_IDR)
                                wait_for_idr = 0;
                            else
                                continue;
                        }

                        /* 解码器要求源图片地址是256对齐 */
                        decode_src = src_image;
                        ret = felix_h264_decoder_decode(decoder, decode_src, grop_len+read_len, output);
                        grop_len = 0;
                        if (ret) {
                            wait_for_idr = 1;
                            continue;
                        }

                        if (output->got_frame) {
                            uvc_fb_pan_display(fb, output);

                            /* 交替使用buf 进行解码刷屏 */
                            if (buf_num > 1)
                                count++;
                        }
                    } /* end while nalu_buf_read_unit */
                } /* end while (write_len < buf->bytesused) */
            } /* end buf->state == UVC_BUF_STATE_DONE */

            ret = usb_host_uvc_put_buffer(uvc_dev_index, &video, buf);
            if (ret) {
                printf("uvc put buffer fail %d\n", ret);
                goto err_free_output_buf;
            }

            if (disconnect)
                goto err_free_output_buf;
        } /* end while usb_host_uvc_get_buffer */

err_put_uvc_buf:
        usb_host_uvc_put_buffer(uvc_dev_index, &video, buf);
err_free_output_buf:
        for (buf_index--; buf_index >= 0; buf_index--) {
            felix_h264_decoder_free_output_buf(decode_output[buf_index]);
            decode_output[buf_index] = NULL;
        }

        free(src_image);
err_deinit_nalu_buf:
        nalu_buf_deinit(nalu_buf);
err_stop_decoder:
        felix_h264_decoder_deinit(decoder);
err_stop_uvc:
        usb_host_uvc_stream_off(uvc_dev_index, &video);
        usb_host_uvc_free_buffer(uvc_dev_index, &video);
        usb_host_uvc_close(uvc_dev_index);
        printf("uvc%d close\n", uvc_dev_index);
wait_wakeup:
        thread_wait();
    }
}

void ad100_uvc_h264_display_test(void)
{
    uvc_h264_display_thread = thread_create("uvc_h264_display_thread", 16*1024, uvc_h264_to_nv12_display_test, NULL);
    if (uvc_h264_display_thread)
        usb_host_uvc_register_callback(uvc_insert_wakeup_display);
}
