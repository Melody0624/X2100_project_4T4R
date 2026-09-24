
#include <common.h>
#include <os.h>
#include <errno.h>
#include <spinlock.h>
#include <driver/cache.h>

#include <usb/gadget_uvc_n_uac1.h>
#include <driver/camera_isp.h>
#include <driver/isp_tuning.h>

#include <driver/gpio.h>
#include <driver/pcm.h>

#include <driver/rotator.h>
#include <helix/helix_h264_encoder.h>
#include <helix/helix_jpeg_encoder.h>

#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"
#include "fisheye_calibration.h"
#include "notch_filter.h"

#include <include_bin.h>

INCBIN(logo_480_40_nv12, "vendor/logo_480_40_nv12.yuv");
INCBIN(logo_720_80_nv12, "vendor/logo_720_80_nv12.yuv");

// #define NOTCH_FILTER

#define WEBCAM_VENDOR_ID        0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID        0x0102    /* Webcam A/V gadget */

#define CAMERA_INDEX    0
#define CAMERA_H264_CHANNEL     0       /* H264主要使用大分辨率，使用主通道0 */
#define CAMERA_MJPEG_CHANNEL     1      /* mjpeg主要使用小分辨率，使用次通道1 */
#define CAMERA_KEEP_CHANNEL     2       /* 用于保持isp运行，使用次次通道2 */

/* 最少两帧才能确保帧率 */
#define CAMERA_H264_FRAME_NUMS   2   /* h264 摄像头帧缓存 */
#define CAMERA_MJPEG_FRAME_NUMS   2   /* mjpeg 摄像头帧缓存 */

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

/* fps large to small Sort*/
static const unsigned int frame_fps[] = {
    30,
};

static void uvc_connect_callback(uint32_t uvc_id, int connect)
{
    printf("uvc_id %d, %s %d\n", uvc_id, __func__, connect);
}

static struct fc_main fc_main_720p_param = {
    .input_width = 1072,
    .input_height = 1072,

    .output_width = 720,
    .output_height = 720,

    .focus_src_x = 550,
    .focus_src_y = 530,

    .focus_dst_x = 360,
    .focus_dst_y = 360,

    .calibration_params[0] = 420,
    .calibration_params[1] = 0.0018,

    .calibration_ratio = 2.0,
};

static struct fc_lookdown fc_lookdown_720p_param = {
    .input_width = 1072,
    .input_height = 1072,

    .output_width = 720,
    .output_height = 480,

    .focus_src_x = 550,
    .focus_src_y = 530,

    .calibration_ratio = 2.0,
    .start_radius = 700,
    .end_radius = 1072,
    .degree_range = 3.1415926*3/4.0,
    .nonlinear_param = 0.5,
};

static struct fc_main fc_main_480p_param = {
    .input_width = 720,
    .input_height = 720,

    .output_width = 480,
    .output_height = 480,

    .focus_src_x = 370,
    .focus_src_y = 350,

    .focus_dst_x = 240,
    .focus_dst_y = 240,

    .calibration_params[0] = 250,
    .calibration_params[1] = 0.004,

    .calibration_ratio = 2.0,
};

static struct fc_lookdown fc_lookdown_480p_param = {
    .input_width = 720,
    .input_height = 720,

    .output_width = 480,
    .output_height = 280,

    .focus_src_x = 370,
    .focus_src_y = 350,

    .calibration_ratio = 2.0,
    .start_radius = 510,
    .end_radius = 720,
    .degree_range = 3.1415926*2.5/4.0,
    .nonlinear_param = 0.25,
};

/* ---- uvc ---- */
static unsigned char uvc_stream_on;
static unsigned char uvc_buf_use;

static thread_waiter_t uvc_stream_waiter;
static thread_waiter_t uvc_buf_waiter;

static unsigned int uvc_width;
static unsigned int uvc_height;

static DEFINE_SPINLOCK(uvc_format_lock);

/* Frame size small to large sort*/
static struct uvc_frame_config uvc_mjpeg_frames[] = {
    {
        .width = 480,
        .height = 800,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 720,
        .height = 1280,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 720,
        .height = 720,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 1072,
        .height = 1072,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Format small to large sort according to pixel size */
static const struct uvc_format_config uvc_frame_format[]= {
    {
        .fcc = V4L2_PIX_FMT_MJPEG,
        .bpp = 8,
        .frames_num = ARRAY_SIZE(uvc_mjpeg_frames),
        .frames = uvc_mjpeg_frames,
    },
};

static void uvc_format_callback(uint32_t uvc_id, const struct uvc_video_format *format)
{
    unsigned long  flags;
    char *data = (char *)&format->fcc;

    spin_lock_irqsave(&uvc_format_lock, flags);
    uvc_width = format->width;
    uvc_height = format->height;
    spin_unlock_irqrestore(&uvc_format_lock, flags);
    printf("%s: %c%c%c%c, width %d, height %d, fps %d\n", __func__, data[0], data[1], data[2], data[3], format->width, format->height, format->fps);
}

static int uvc_stream_callback(uint32_t uvc_id, int enable)
{
    uvc_stream_on = enable;
    if (enable)
        thread_waiter_wakeup(&uvc_stream_waiter);

    printf("%s %d\n", __func__, enable);
    return 0;
}

static void uvc_buf_complete(struct uvc_buffer *buf)
{
    uvc_buf_use = 0;
    if (buf->state != UVC_BUF_STATE_DONE)
        printf("%s: data not transmitted\n", __func__);

    thread_waiter_wakeup(&uvc_buf_waiter);
}

static void usb_gadget_uvc_thread(void *data)
{
    int ret;
    int size;
    void *fc_buf;
    int out_size;
    void *out_buf;
    struct uvc_buffer uvc_buf;
    unsigned int width;
    unsigned int height;
    camera_hd_t *camera_handle;
    struct camera_info *info;
    struct frame_image_format fmt;
    unsigned long flags;

    struct helix_jpeg_encoder_param jpeg_param;
    struct helix_jpeg_encoder *encoder;

    int logo_size;
    const void *logo_buf;
    int fisheye_calibration;
    struct fc_main *fc_main_param = NULL;
    struct fc_lookdown *fc_lookdown_param = NULL;

    /* uvc init */
    camera_handle = isp_detect(CAMERA_INDEX, CAMERA_MJPEG_CHANNEL);
    assert(camera_handle);

    /* 获取摄像头原始宽高 */
    info = isp_get_sensor_info(camera_handle);
    assert(info);

    while (1) {
        while (!uvc_stream_on)
            thread_waiter_wait(&uvc_stream_waiter);

        spin_lock_irqsave(&uvc_format_lock, flags);
        width = uvc_width;
        height = uvc_height;
        spin_unlock_irqrestore(&uvc_format_lock, flags);

        memset(&fmt, 0, sizeof(fmt));

        if (width == 480 && height == 800) {
            fisheye_calibration = 1;
            logo_size = 480*40;
            logo_buf = logo_480_40_nv12Data;
            fc_main_param = &fc_main_480p_param;
            fc_lookdown_param = &fc_lookdown_480p_param;
        } else if (width == 720 && height == 1280) {
            fisheye_calibration = 1;
            logo_size = 720*80;
            logo_buf = logo_720_80_nv12Data;
            fc_main_param = &fc_main_720p_param;
            fc_lookdown_param = &fc_lookdown_720p_param;
        } else {
            fisheye_calibration = 0;
            fmt.width              = width;
            fmt.height             = height;
        }

        if (fisheye_calibration) {
            fmt.width              = fc_main_param->input_width;
            fmt.height             = fc_main_param->input_height;

            size = ALIGN(width * height * 3 / 2, cache_line_size());
            fc_buf = memalign(256, size);
            assert(fc_buf);
            memcpy(fc_buf + width * width, logo_buf, logo_size);
            memcpy(fc_buf + width * height + width * width / 2, logo_buf + logo_size, logo_size / 2);
        } else {
            fc_buf = NULL;
        }

        if (fmt.height <= 720) {
            fmt.scaler.enable = 1;
            fmt.scaler.width = 1280;
            fmt.scaler.height = 720;
            fmt.crop.enable = 1;
            fmt.crop.top = (fmt.scaler.height - fmt.height) / 2;
            fmt.crop.left = (fmt.scaler.width - fmt.width) / 2;
            fmt.crop.width = fmt.width;
            fmt.crop.height = fmt.height;
        } else {
            fmt.crop.enable = 1;
            fmt.crop.top = (info->height - fmt.height) / 2;
            fmt.crop.left = (info->width - fmt.width) / 2;
            fmt.crop.width = fmt.width;
            fmt.crop.height = fmt.height;
        }

        fmt.pixel_format       = CAMERA_PIX_FMT_NV12;
        fmt.frame_nums         = CAMERA_MJPEG_FRAME_NUMS;

        ret = isp_set_format(camera_handle, &fmt);
        assert(!ret);

        ret = isp_request_buffer(camera_handle, &fmt);
        assert(!ret);

        ret = isp_power_on(camera_handle);
        assert(!ret);

        ret = isp_stream_on(camera_handle);
        assert(!ret);

        jpeg_param.width = width;
        jpeg_param.height = height;
        jpeg_param.compress_quality = 80;

        out_size = width * height;
        out_buf = memalign(256, ALIGN(out_size, cache_line_size()));
        assert(out_buf);

        encoder = helix_jpeg_encoder_init(&jpeg_param);
        assert(encoder);

        while (uvc_stream_on) {
            void *mem = isp_wait_frame(camera_handle);
            if (mem) {
                if (fisheye_calibration) {
                    fc_main_process_coarse(fc_main_param, mem, mem + (fmt.width * fmt.height), fc_buf, fc_buf + (width * height));
                    fc_lookdown_process_coarse(fc_lookdown_param, mem, mem + (fmt.width * fmt.height),
                            fc_buf + width * (height - fc_lookdown_param->output_height),
                            fc_buf + (width * height) + width * (height - fc_lookdown_param->output_height) / 2);

                    isp_put_frame(camera_handle, mem);
                    size = helix_jpeg_encoder_encode(encoder, fc_buf, out_buf, out_size);
                } else {
                    size = helix_jpeg_encoder_encode(encoder, mem, out_buf, out_size);
                    isp_put_frame(camera_handle, mem);
                }

                if (size > 0) {
                    uvc_buf.mem = out_buf;
                    uvc_buf.length = size;
                    uvc_buf.complete = uvc_buf_complete;

                    /* 如果uvc的宽高与摄像头宽高不相等,重新初始化 */
                    if ((width != uvc_width) || (height != uvc_height))
                        break;

                    uvc_buf_use = 1;
                    ret = gadget_uvc_n_write(0, &uvc_buf, 1, -1);
                    if (ret)
                        uvc_buf_use = 0;
                } else {
                    printf("jpeg encoding failed %d\n", size);
                }
            }

            while (uvc_buf_use)
                thread_waiter_wait(&uvc_buf_waiter);

            /* 如果uvc的宽高与摄像头宽高不相等,重新初始化 */
            if ((width != uvc_width) || (height != uvc_height))
                break;
        }

        helix_jpeg_encoder_deinit(encoder);
        free(out_buf);

        isp_stream_off(camera_handle);
        isp_power_off(camera_handle);
        isp_free_buffer(camera_handle);

        if (fisheye_calibration)
            free(fc_buf);
    }

    isp_release(camera_handle);
}

static struct uvc_callback uvc_callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
    .connect_cb = uvc_connect_callback,
};

/* ---- uvc h264 ---- */
static unsigned char uvc_h264_stream_on;
static unsigned char uvc_h264_buf_use;

static thread_waiter_t uvc_h264_stream_waiter;
static thread_waiter_t uvc_h264_buf_waiter;

static unsigned int uvc_h264_width;
static unsigned int uvc_h264_height;

static DEFINE_SPINLOCK(uvc_h264_format_lock);

/* Frame size small to large sort*/
static struct uvc_frame_config uvc_h264_frames[] = {
    {
        .width = 480,
        .height = 800,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 720,
        .height = 1280,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 720,
        .height = 720,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 1072,
        .height = 1072,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
};

/* Format small to large sort according to pixel size */
static const struct uvc_format_config uvc_h264_frame_format[]= {
    {
        .fcc = V4L2_PIX_FMT_H264,
        .bpp = 8,
        .frames_num = ARRAY_SIZE(uvc_h264_frames),
        .frames = uvc_h264_frames,
    },
};

static void uvc_h264_format_callback(uint32_t uvc_id, const struct uvc_video_format *format)
{
    unsigned long  flags;
    char *data = (char *)&format->fcc;

    spin_lock_irqsave(&uvc_h264_format_lock, flags);
    uvc_h264_width = format->width;
    uvc_h264_height = format->height;
    spin_unlock_irqrestore(&uvc_h264_format_lock, flags);

    printf("%s: %c%c%c%c, width %d, height %d, fps %d\n", __func__, data[0], data[1], data[2], data[3], format->width, format->height, format->fps);
}

static int uvc_h264_stream_callback(uint32_t uvc_id, int enable)
{
    uvc_h264_stream_on = enable;
    if (enable)
        thread_waiter_wakeup(&uvc_h264_stream_waiter);

    printf("%s %d\n", __func__, enable);
    return 0;
}

static void uvc_h264_buf_complete(struct uvc_buffer *buf)
{
    uvc_h264_buf_use = 0;
    if (buf->state != UVC_BUF_STATE_DONE)
        printf("%s: data not transmitted\n", __func__);

    thread_waiter_wakeup(&uvc_h264_buf_waiter);
}

static void usb_gadget_uvc_h264_thread(void *data)
{
    int ret;
    int size;
    void *fc_buf;
    int out_size;
    void *out_buf;
    struct uvc_buffer uvc_buf;
    unsigned int width;
    unsigned int height;
    camera_hd_t *camera_handle;
    struct camera_info *info;
    struct frame_image_format fmt;
    unsigned long  flags;

    struct helix_h264_param h264_param;
    struct helix_h264_encoder *encoder;

    int logo_size;
    const void *logo_buf;
    int fisheye_calibration;
    struct fc_main *fc_main_param = NULL;
    struct fc_lookdown *fc_lookdown_param = NULL;

    camera_handle = isp_detect(CAMERA_INDEX, CAMERA_H264_CHANNEL);
    assert(camera_handle);

    /* 获取摄像头原始宽高 */
    info = isp_get_sensor_info(camera_handle);
    assert(info);

    while (1) {
        while (!uvc_h264_stream_on)
            thread_waiter_wait(&uvc_h264_stream_waiter);

        spin_lock_irqsave(&uvc_h264_format_lock, flags);
        width = uvc_h264_width;
        height = uvc_h264_height;
        spin_unlock_irqrestore(&uvc_h264_format_lock, flags);

        memset(&fmt, 0, sizeof(fmt));

        if (width == 480 && height == 800) {
            fisheye_calibration = 1;
            logo_size = 480*40;
            logo_buf = logo_480_40_nv12Data;
            fc_main_param = &fc_main_480p_param;
            fc_lookdown_param = &fc_lookdown_480p_param;
        } else if (width == 720 && height == 1280) {
            fisheye_calibration = 1;
            logo_size = 720*80;
            logo_buf = logo_720_80_nv12Data;
            fc_main_param = &fc_main_720p_param;
            fc_lookdown_param = &fc_lookdown_720p_param;
        } else {
            fisheye_calibration = 0;
            fmt.width              = width;
            fmt.height             = height;
        }

        if (fisheye_calibration) {
            fmt.width              = fc_main_param->input_width;
            fmt.height             = fc_main_param->input_height;

            size = ALIGN(width * height * 3 / 2, cache_line_size());
            fc_buf = memalign(256, size);
            assert(fc_buf);
            memcpy(fc_buf + width * width, logo_buf, logo_size);
            memcpy(fc_buf + width * height + width * width / 2, logo_buf + logo_size, logo_size / 2);
        } else {
            fc_buf = NULL;
        }

        if (fmt.height <= 720) {
            fmt.scaler.enable = 1;
            fmt.scaler.width = 1280;
            fmt.scaler.height = 720;
            fmt.crop.enable = 1;
            fmt.crop.top = (fmt.scaler.height - fmt.height) / 2;
            fmt.crop.left = (fmt.scaler.width - fmt.width) / 2;
            fmt.crop.width = fmt.width;
            fmt.crop.height = fmt.height;
        } else {
            fmt.crop.enable = 1;
            fmt.crop.top = (info->height - fmt.height) / 2;
            fmt.crop.left = (info->width - fmt.width) / 2;
            fmt.crop.width = fmt.width;
            fmt.crop.height = fmt.height;
        }
        fmt.pixel_format       = CAMERA_PIX_FMT_NV12;
        fmt.frame_nums         = CAMERA_H264_FRAME_NUMS;

        ret = isp_set_format(camera_handle, &fmt);
        assert(!ret);

        ret = isp_request_buffer(camera_handle, &fmt);
        assert(!ret);

        ret = isp_power_on(camera_handle);
        assert(!ret);

        ret = isp_stream_on(camera_handle);
        assert(!ret);

        h264_param.width = width;
        h264_param.height = height;
        h264_param.gop_size = 30;
        h264_param.bitrate_mode = HELIX_BITRATE_VBR;
        h264_param.bitrate = 2000*1000;

        out_size = width * height;
        out_buf = memalign(256, ALIGN(out_size, cache_line_size()));
        assert(out_buf);

        encoder = helix_h264_encoder_init(&h264_param);
        assert(encoder);

        while (uvc_h264_stream_on) {
            void *mem = isp_wait_frame(camera_handle);
            if (mem) {
                if (fisheye_calibration) {
                    fc_main_process_coarse(fc_main_param, mem, mem + (fmt.width * fmt.height), fc_buf, fc_buf + (width * height));
                    fc_lookdown_process_coarse(fc_lookdown_param, mem, mem + (fmt.width * fmt.height),
                            fc_buf + width * (height - fc_lookdown_param->output_height),
                            fc_buf + (width * height) + width * (height - fc_lookdown_param->output_height) / 2);

                    isp_put_frame(camera_handle, mem);
                    size = helix_h264_encoder_encode(encoder, fc_buf, out_buf, out_size);
                } else {
                    size = helix_h264_encoder_encode(encoder, mem, out_buf, out_size);
                    isp_put_frame(camera_handle, mem);
                }

                if (size > 0) {
                    uvc_buf.mem = out_buf;
                    uvc_buf.length = size;
                    uvc_buf.complete = uvc_h264_buf_complete;

                    /* 如果uvc的宽高与摄像头宽高不相等,重新初始化 */
                    if ((width != uvc_h264_width) || (height != uvc_h264_height))
                        break;

                    uvc_h264_buf_use = 1;
                    ret = gadget_uvc_n_write(1, &uvc_buf, 1, -1);
                    if (ret)
                        uvc_h264_buf_use = 0;
                } else {
                    printf("hw h264 fail %d\n", size);
                }
            }

            while (uvc_h264_buf_use)
                thread_waiter_wait(&uvc_h264_buf_waiter);

            /* 如果uvc的宽高与摄像头宽高不相等,重新初始化 */
            if ((width != uvc_h264_width) || (height != uvc_h264_height))
                break;
        }

        helix_h264_encoder_deinit(encoder);
        free(out_buf);

        isp_stream_off(camera_handle);
        isp_power_off(camera_handle);
        isp_free_buffer(camera_handle);

        if (fisheye_calibration)
            free(fc_buf);
    }
}

static struct uvc_callback uvc_h264_callback = {
    .format_cb = uvc_h264_format_callback,
    .stream_cb = uvc_h264_stream_callback,
    .connect_cb = uvc_connect_callback,
};

static const struct uvc_device_config uvc_config[2] = {
    [0] = {
        .format_num = ARRAY_SIZE(uvc_frame_format),
        .formats = uvc_frame_format,
        .callback = &uvc_callback,
    },
    [1] = {
        .format_num = ARRAY_SIZE(uvc_h264_frame_format),
        .formats = uvc_h264_frame_format,
        .callback = &uvc_h264_callback,
    },
};

/* ---- uac ---- */
#define BUF_SIZE_MS     100
#define PKT_SIZE_MS     10

static int uac1_start_playback_cb(u32 rate);
static void uac1_stop_playback_cb(void);
static int uac1_start_capture_cb(u32 rate);
static void uac1_stop_capture_cb(void);

static void uac1_connect_callback(int connect)
{
    printf("%s %d\n", __func__, connect);
}

static struct uac1_params uac1_param = {
    /* playback */
    .p_chmask = UAC_CH_LAYOUT_MONO,
    .p_ssize = 2,
    .p_srate = 16000,
    .start_playback_callback = uac1_start_playback_cb,
    .stop_playback_callback = uac1_stop_playback_cb,

    /* capture */
    .c_chmask = UAC_CH_LAYOUT_MONO,
    .c_ssize = 2,
    .c_srate = 16000,
    .start_capture_callback = uac1_start_capture_cb,
    .stop_capture_callback = uac1_stop_capture_cb,

    .buffer_size_ms = BUF_SIZE_MS,
    .connect_cb = uac1_connect_callback,
};

static struct pcm_params playback_params = {
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,

    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_16000,

    .channels = 1,
};

static struct pcm_params capture_params = {
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,

    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_16000,

    .channels = 1,
};

/* uac playback */
#define GPIO_AMP_ENABLE      GPIO_PB(13)
#define PLAYBACK_BUF_PKT    10

static volatile int playback_on;
static DEFINE_THREAD_WAITER(playback_waiter);

static void *playback_buffer;
static u32 playback_buf_read;
static u32 playback_buf_write;
static DEFINE_MUTEX(playback_lock);

static int uac1_start_playback_cb(u32 rate)
{
    assert(pcm_data_sample_rate(playback_params.pcm_sample_rate) == rate);
    playback_on = 1;
    thread_waiter_wakeup(&playback_waiter);

    printf("%s\n", __func__);

    return 0;
}

static void uac1_stop_playback_cb(void)
{
    playback_on = 0;

    printf("%s\n", __func__);
}

static void pcm_playback_thread(void *data)
{
    int ret;
    int len;
    u32 rate;
    u32 frame_size;
    u32 playback_len;
    u32 playback_size;
    void *playback_buf;
    void *playback_buf_cache;

    rate = uac1_param.p_srate;
    frame_size = uac1_param.p_ssize;
    playback_size = rate / 1000 * PKT_SIZE_MS;
    playback_len = playback_size * frame_size;
    playback_buffer = malloc(playback_len * PLAYBACK_BUF_PKT);
    assert(playback_buffer);

    playback_buf_cache = malloc(playback_len);
    assert(playback_buf_cache);

    while (1) {
        while (!playback_on)
            thread_waiter_wait(&playback_waiter);

        while (playback_on) {
            len = 0;
            while (len < playback_len) {
                ret = gadget_uac1_playback_read(playback_buf_cache + len, playback_len - len);
                if (ret == 0) {
                    msleep(PKT_SIZE_MS);
                } else if (ret < 0) {
                    printf("gadget_uac1_playback_read error %d\n", ret);
                    break;
                }

                len += ret;
            }

            if (len != playback_len) {
                printf("uvc playback abnormal\n");
                break;
            }

            mutex_lock(&playback_lock);
            while ((playback_buf_write - playback_buf_read) >= PLAYBACK_BUF_PKT) {
                playback_buf_read++;
                printf("playback overrun discard\n");
            }

            playback_buf = playback_buffer + (playback_buf_write % PLAYBACK_BUF_PKT) * playback_len;
            memcpy(playback_buf, playback_buf_cache, playback_len);
            playback_buf_write++;
            mutex_unlock(&playback_lock);
        }

    }
}

/* uac capture */
#define SPEEX_ECHO_FILTER_PKT   5
#define SPEEX_ECHO_DELAY_PKT    10

#define CAPTURE_BUF_PKT     10

#define CAPTURE_VOLUME          80
#define CAPTURE_ECHO_VOLUME     60
#define PLAYBACK_VOLUME         60

static volatile int capture_on;
static DEFINE_THREAD_WAITER(capture_waiter);

static void *capture_buffer;
static u32 capture_buf_read;
static u32 capture_buf_write;
static DEFINE_MUTEX(capture_lock);
static DEFINE_THREADCOND(capture_cond);

static int uac1_start_capture_cb(u32 rate)
{
    assert(pcm_data_sample_rate(capture_params.pcm_sample_rate) == rate);
    capture_on = 1;
    thread_waiter_wakeup(&capture_waiter);

    printf("%s\n", __func__);
    return 0;
}

static void uac1_stop_capture_cb(void)
{
    capture_on = 0;

    printf("%s\n", __func__);
}

static void uac_capture_thread(void *data)
{
    int ret;
    int len;
    void *uac_buf;
    void *capture_buf;
    u32 capture_len = uac1_param.c_srate / 1000 * PKT_SIZE_MS * uac1_param.c_ssize;

    uac_buf = malloc(capture_len);
    assert(uac_buf);

    while (1) {
        while (!capture_on)
            thread_waiter_wait(&capture_waiter);

        printf("capture start\n");

        while (capture_on) {
            mutex_lock(&capture_lock);

            while (capture_buf_write == capture_buf_read)
                thread_cond_wait(&capture_cond, &capture_lock);
            
            capture_buf = capture_buffer + (capture_buf_read % CAPTURE_BUF_PKT) * capture_len;
            memcpy(uac_buf, capture_buf, capture_len);
            capture_buf_read++;
            mutex_unlock(&capture_lock);

            len = 0;
            while (len < capture_len) {
                ret = gadget_uac1_capture_write(uac_buf + len, capture_len - len);
                if (ret < 0) {
                    printf("gadget_uac1_capture_write error %d\n", ret);
                    break;
                }

                len += ret;
            }
        }

        printf("capture stop\n");
    }
}

static void pcm_capture_thread(void *data)
{
    int i;
    int ret;
    int len;
    int rate;
    u32 frame_size;
    u32 capture_len;
    u32 capture_size;
    short *capture_buf;
    void *capture_buf_cache;

    u32 playback_enable;
    void *playback_buf;
    void *playback_buf_cache;

    u32 echo_start;
    u32 echo_buf_read;
    u32 echo_buf_write;
    void *echo_buf;
    void *echo_buffer;
    void *speex_buf;
    void *speex_buf_cache;

    struct pcm_device *capture_dai;
    struct pcm_device *capture_codec;

    struct pcm_device *playback_dai;
    struct pcm_device *playback_codec;

    SpeexEchoState *speex_echo_state;
    SpeexPreprocessState *speex_preprocess_state;

    assert(uac1_param.c_srate == uac1_param.p_srate);
    assert(uac1_param.c_ssize == uac1_param.p_ssize);

    rate = uac1_param.c_srate;
    frame_size = uac1_param.c_ssize;
    capture_size = rate / 1000 * PKT_SIZE_MS;
    capture_len = capture_size * frame_size;

    /* playback */
    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        assert(!gpio_request(GPIO_AMP_ENABLE, "AMP_ENABLE"));
        gpio_set_func(GPIO_AMP_ENABLE , GPIO_OUTPUT0);
    }

    playback_enable = 0;
    playback_buf_cache = malloc(capture_len);
    assert(playback_buf_cache);

    playback_dai = pcm_get("aic0-playback");
    assert(playback_dai);
    playback_codec = pcm_get("icodec-playback");
    assert(playback_codec);

    ret = pcm_private_ctrl(playback_dai, "sysclk-set-rate", rate * 256);
    assert(!ret);
    ret = pcm_private_ctrl(playback_dai, "sysclk-set-output", 1);
    assert(!ret);

    ret = pcm_enable(playback_codec, &playback_params);
    assert(!ret);
    ret = pcm_enable(playback_dai, &playback_params);
    assert(!ret);

    pcm_set_volume(playback_codec, PLAYBACK_VOLUME);

    ret = pcm_start(playback_codec);
    assert(!ret);
    ret = pcm_start(playback_dai);
    assert(!ret);

    pcm_set_mute(playback_codec, 1);

    /* echo */
    echo_start = 0;
    echo_buf_read = 0;
    echo_buf_write = 0;
    echo_buffer = malloc(capture_len * SPEEX_ECHO_DELAY_PKT);
    assert(echo_buffer);

    speex_buf_cache = malloc(capture_len);
    assert(speex_buf_cache);

    speex_echo_state = speex_echo_state_init(capture_size, capture_size * SPEEX_ECHO_FILTER_PKT);
    speex_preprocess_state = speex_preprocess_state_init(capture_size, rate);
    speex_echo_ctl(speex_echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    i = 1;
    speex_preprocess_ctl(speex_preprocess_state, SPEEX_PREPROCESS_SET_DENOISE, &i);

#ifdef NOTCH_FILTER
    /* eliminate power noise */
    float freq[] = {1000, 2000, 3000};
    struct notch_filter *nf = notch_filter_init(rate, 1.2, freq, ARRAY_SIZE(freq));
    assert(nf);
#endif

    /* capture */
    capture_buffer = malloc(capture_len * CAPTURE_BUF_PKT);
    assert(capture_buffer);

    capture_buf_cache = malloc(capture_len);
    assert(capture_buf_cache);

    capture_dai = pcm_get("aic0-icodec-capture");
    assert(capture_dai);
    capture_codec = pcm_get("icodec-capture");
    assert(capture_codec);

    ret = pcm_private_ctrl(capture_dai, "sysclk-set-rate", rate * 256);
    assert(!ret);
    ret = pcm_private_ctrl(capture_dai, "sysclk-set-output", 1);
    assert(!ret);

    ret = pcm_private_ctrl(capture_codec, "bias-on", 1);
    assert(!ret);

    ret = pcm_enable(capture_dai, &capture_params);
    assert(!ret);
    ret = pcm_enable(capture_codec, &capture_params);
    assert(!ret);

    pcm_set_volume(capture_codec, CAPTURE_VOLUME);

    ret = pcm_start(capture_codec);
    assert(!ret);
    ret = pcm_start(capture_dai);
    assert(!ret);

    thread_create("uac_capture_thread", 4096, uac_capture_thread, NULL);

    while (1) {
        len = 0;
        while (len < capture_size) {
            ret = pcm_read_frame(capture_dai, capture_buf_cache + len * frame_size, capture_size - len);
            if (ret == 0) {
                msleep(PKT_SIZE_MS);
            } else if (ret < 0) {
                printf("pcm_read_frame error %d\n", ret);
                break;
            }

            len += ret;
        }

        if (len != capture_size) {
            printf("capture pcm data abnormal\n");
        }

#ifdef NOTCH_FILTER
        capture_buf = capture_buf_cache;
        for (i = 0; i < capture_size; i++)
            capture_buf[i] = notch_filter_process(nf, capture_buf[i]);
#endif

        if (playback_on) {
            if (!playback_enable) {

                printf("playback start\n");

                pcm_set_mute(playback_codec, 0);

                // 使能功放引脚
                if (gpio_is_valid(GPIO_AMP_ENABLE))
                    gpio_set_value(GPIO_AMP_ENABLE, 1);

                pcm_set_volume(capture_codec, CAPTURE_ECHO_VOLUME);

                playback_enable = 1;
            }

            mutex_lock(&playback_lock);
            if (playback_buf_write == playback_buf_read) {
                playback_buf = playback_buffer + (playback_buf_write % PLAYBACK_BUF_PKT) * capture_len;
                memset(playback_buf, 0, capture_len);
                playback_buf_write++;
                // printf("playback underrun fill zero\n");
            }

            playback_buf = playback_buffer + (playback_buf_read % PLAYBACK_BUF_PKT) * capture_len;
            memcpy(playback_buf_cache, playback_buf, capture_len);
            playback_buf_read++;
            mutex_unlock(&playback_lock);

            len = 0;
            while (len < capture_size) {
                ret = pcm_write_frame(playback_dai, playback_buf_cache + len * frame_size, capture_size - len);
                if (ret < 0) {
                    printf("pcm_write_frame error %d\n", ret);
                    break;
                }

                len += ret;
            }

            if (len != capture_size) {
                printf("playback pcm data abnormal\n");
            }

            echo_buf = echo_buffer + (echo_buf_write % SPEEX_ECHO_DELAY_PKT) * capture_len;
            memcpy(echo_buf, playback_buf_cache, capture_len);
            echo_buf_write++;
        } else {
            if (playback_enable) {
                mutex_lock(&playback_lock);
                if (playback_buf_write == playback_buf_read) {
                    playback_enable = 0;
                } else {
                    playback_buf = playback_buffer + (playback_buf_read % PLAYBACK_BUF_PKT) * capture_len;
                    memcpy(playback_buf_cache, playback_buf, capture_len);
                    playback_buf_read++;
                }
                mutex_unlock(&playback_lock);

                if (playback_enable) {
                    len = 0;
                    while (len < capture_size) {
                        ret = pcm_write_frame(playback_dai, playback_buf_cache + len * frame_size, capture_size - len);
                        if (ret < 0) {
                            printf("pcm_write_frame error %d\n", ret);
                            break;
                        }

                        len += ret;
                    }

                    if (len != capture_size) {
                        printf("playback pcm data abnormal\n");
                    }

                    echo_buf = echo_buffer + (echo_buf_write % SPEEX_ECHO_DELAY_PKT) * capture_len;
                    memcpy(echo_buf, playback_buf_cache, capture_len);
                    echo_buf_write++;
                } else {
                    // 关闭功放引脚
                    if (gpio_is_valid(GPIO_AMP_ENABLE))
                        gpio_set_value(GPIO_AMP_ENABLE, 0);

                    pcm_set_mute(playback_codec, 1);

                    pcm_set_volume(capture_codec, CAPTURE_VOLUME);

                    printf("playback stop\n");
                }
            }
        }

        if (echo_buf_write - echo_buf_read >= SPEEX_ECHO_DELAY_PKT)
            echo_start = 1;

        if (echo_start) {
            echo_buf = echo_buffer + (echo_buf_read % SPEEX_ECHO_DELAY_PKT) * capture_len;
            speex_echo_cancellation(speex_echo_state, capture_buf_cache, echo_buf, speex_buf_cache);
            speex_preprocess_ctl(speex_preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE, speex_echo_state);
            speex_preprocess_run(speex_preprocess_state, speex_buf_cache);

            echo_buf_read++;
            if (echo_buf_write == echo_buf_read) {
                echo_start = 0;
                speex_echo_state_reset(speex_echo_state);
            }

            speex_buf = speex_buf_cache;
        } else {
            speex_preprocess_ctl(speex_preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE, NULL);
            speex_preprocess_run(speex_preprocess_state, capture_buf_cache);

            speex_buf = capture_buf_cache;
        }

        mutex_lock(&capture_lock);
        while ((capture_buf_write - capture_buf_read) >= CAPTURE_BUF_PKT)
            capture_buf_read++;

        capture_buf = capture_buffer + (capture_buf_write % CAPTURE_BUF_PKT) * capture_len;
        memcpy(capture_buf, speex_buf, capture_len);
        capture_buf_write++;
        thread_cond_signal(&capture_cond);
        mutex_unlock(&capture_lock);
    }

}

/* 上电提前开流，加速后面的开流时间和解决开关流图像偏色问题 */
static void camera_isp_open(void)
{
    int ret;
    camera_hd_t *camera_handle;
    struct frame_image_format fmt;

    camera_handle = isp_detect(CAMERA_INDEX, CAMERA_KEEP_CHANNEL);
    assert(camera_handle);

    /* 使用最小分辨率8*8 */
    memset(&fmt, 0, sizeof(fmt));
    fmt.width              = 8;
    fmt.height             = 8;
    fmt.pixel_format       = CAMERA_PIX_FMT_NV12;
    fmt.crop.enable       = 1;
    fmt.crop.width       = 8;
    fmt.crop.height      = 8;
    fmt.frame_nums         = 1;

    ret = isp_set_format(camera_handle, &fmt);
    assert(!ret);

    ret = isp_request_buffer(camera_handle, &fmt);
    assert(!ret);

    ret = isp_power_on(camera_handle);
    assert(!ret);

    ret = isp_stream_on(camera_handle);
    assert(!ret);
}

int gadget_usb_uvc_uac1_test(void)
{
    camera_isp_open();

    assert(uac1_param.c_srate == uac1_param.p_srate);
    assert(uac1_param.c_ssize == uac1_param.p_ssize);
    speex_echo_state = speex_echo_state_init(uac1_param.c_srate / 1000 * PKT_SIZE_MS, uac1_param.c_srate / 1000 * PKT_SIZE_MS * SPEEX_ECHO_FILTER_PKT);
    speex_preprocess_state = speex_preprocess_state_init(uac1_param.c_srate / 1000 * PKT_SIZE_MS, uac1_param.c_srate);
    speex_echo_ctl(speex_echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &uac1_param.c_srate);
    int i = 1;
    speex_preprocess_ctl(speex_preprocess_state, SPEEX_PREPROCESS_SET_DENOISE, &i);

    fc_main_pretreatment_coarse(&fc_main_480p_param);
    fc_lookdown_pretreatment_coarse(&fc_lookdown_480p_param);

    fc_main_pretreatment_coarse(&fc_main_720p_param);
    fc_lookdown_pretreatment_coarse(&fc_lookdown_720p_param);

    /* uvc init */
    thread_waiter_init(&uvc_buf_waiter);
    thread_waiter_init(&uvc_stream_waiter);
    thread_create("usb gadget uvc thread", 4096, usb_gadget_uvc_thread, NULL);

    /* uvc h264 init */
    thread_waiter_init(&uvc_h264_buf_waiter);
    thread_waiter_init(&uvc_h264_stream_waiter);
    thread_create("usb gadget uvc h264 thread", 4096, usb_gadget_uvc_h264_thread, NULL);

    /* uac init */
    /* uac playback init */
    thread_waiter_init(&playback_waiter);
    thread_create("pcm_playback_thread", 4096, pcm_playback_thread, NULL);

    /* uac capture init */
    thread_waiter_init(&capture_waiter);
    thread_create("pcm_capture_thread", 4096, pcm_capture_thread, NULL);

    gadget_uvc_n_uac1_init(&usb_id, uvc_config, ARRAY_SIZE(uvc_config), &uac1_param);

    return 0;
}
