
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
#include "ns_core.h"

#define WEBCAM_VENDOR_ID        0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID        0x0102    /* Webcam A/V gadget */

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
        .width = 800,
        .height = 480,
        .fps_num = ARRAY_SIZE(frame_fps),
        .frame_fps = frame_fps,
    },
    {
        .width = 1280,
        .height = 720,
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
    int out_size;
    void *out_buf;
    struct uvc_buffer uvc_buf;
    unsigned int sensor_width;
    unsigned int sensor_height;
    camera_hd_t *camera_handle;
    struct camera_info *info;
    struct frame_image_format fmt;
    char fmt_a, fmt_b, fmt_c, fmt_d;
    unsigned long  flags;

    struct helix_jpeg_encoder_param jpeg_param;
    struct helix_jpeg_encoder *encoder;

    /* uvc init */
    camera_handle = isp_detect(0, 0);
    assert(camera_handle);

    /* 获取摄像头原始宽高 */
    info = isp_get_sensor_info(camera_handle);
    assert(info);
    sensor_width = info->width;
    sensor_height = info->height;

    while (1) {
        while (!uvc_stream_on)
            thread_waiter_wait(&uvc_stream_waiter);

        memset(&fmt, 0, sizeof(fmt));
        spin_lock_irqsave(&uvc_format_lock, flags);
        fmt.width              = uvc_width;
        fmt.height             = uvc_height;
        fmt.pixel_format       = CAMERA_PIX_FMT_NV12;
        fmt.scaler.width       = uvc_width;
        fmt.scaler.height      = uvc_height;
        fmt.frame_nums         = 2;

        if ((uvc_width != sensor_width) || (uvc_height != sensor_height))
            fmt.scaler.enable = 1;

        spin_unlock_irqrestore(&uvc_format_lock, flags);

        ret = isp_set_format(camera_handle, &fmt);
        assert(!ret);

        ret = isp_request_buffer(camera_handle, &fmt);
        assert(!ret);

        info = isp_get_info(camera_handle);
        assert(info);
        fmt_a = (char)(info->data_fmt >> 0);
        fmt_b = (char)(info->data_fmt >> 8);
        fmt_c = (char)(info->data_fmt >> 16);
        fmt_d = (char)(info->data_fmt >> 24);
        printf("channel         = %s\n", (char *)camera_handle->ptr);
        printf("sensor_name     = %s\n", info->name);
        printf("width           = %d\n", info->width);
        printf("height          = %d\n", info->height);
        printf("fps             = %d\n", info->fps);
        printf("data_fmt        = %c%c%c%c\n", fmt_a, fmt_b, fmt_c, fmt_d);
        printf("line_length     = %d\n", info->line_length);
        printf("frame_size      = %d\n", info->frame_size);
        printf("frame_align_size= %d\n", info->frame_align_size);

        jpeg_param.width = info->width;
        jpeg_param.height = info->height;

        if (jpeg_param.height < 720)
            jpeg_param.compress_quality = 80;
        else
            jpeg_param.compress_quality = 60;

        encoder = helix_jpeg_encoder_init(&jpeg_param);
        assert(encoder);

        out_size = info->width * info->height;
        out_buf = memalign(256, ALIGN(out_size, cache_line_size()));
        assert(out_buf);

        ret = isp_power_on(camera_handle);
        assert(!ret);

        ret = isp_stream_on(camera_handle);
        assert(!ret);

        while (uvc_stream_on) {
            void *mem = isp_wait_frame(camera_handle);
            if (mem) {
                size = helix_jpeg_encoder_encode(encoder, mem, out_buf, out_size);
                isp_put_frame(camera_handle, mem);

                if (size > 0) {
                    uvc_buf.mem = out_buf;
                    uvc_buf.length = size;
                    uvc_buf.complete = uvc_buf_complete;

                    /* 如果uvc的宽高与摄像头宽高不相等,重新初始化 */
                    if ((fmt.width != uvc_width) || (fmt.height != uvc_height))
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
            if ((fmt.width != uvc_width) || (fmt.height != uvc_height))
                break;
        }

        isp_stream_off(camera_handle);
        isp_power_off(camera_handle);
        isp_free_buffer(camera_handle);

        free(out_buf);
        helix_jpeg_encoder_deinit(encoder);
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
    int out_size;
    void *out_buf;
    struct uvc_buffer uvc_buf;
    unsigned int sensor_width;
    unsigned int sensor_height;
    camera_hd_t *camera_handle;
    struct camera_info *info;
    struct frame_image_format fmt;
    char fmt_a, fmt_b, fmt_c, fmt_d;
    unsigned long  flags;

    void *rotator_vaddr;
    struct rotator_config_data rotator_config;
    struct helix_h264_param h264_param;
    struct helix_h264_encoder *encoder;

    camera_handle = isp_detect(0, 1);
    assert(camera_handle);

    /* 获取摄像头原始宽高 */
    info = isp_get_sensor_info(camera_handle);
    assert(info);
    sensor_width = info->width;
    sensor_height = info->height;

    while (1) {
        while (!uvc_h264_stream_on)
            thread_waiter_wait(&uvc_h264_stream_waiter);

        /* 需要90度旋转 宽高互换 */
        memset(&fmt, 0, sizeof(fmt));
        spin_lock_irqsave(&uvc_h264_format_lock, flags);
        fmt.width              = uvc_h264_height;
        fmt.height             = uvc_h264_width;
        fmt.pixel_format       = CAMERA_PIX_FMT_NV12;
        fmt.scaler.width       = uvc_h264_height;
        fmt.scaler.height      = uvc_h264_width;
        fmt.frame_nums         = 2;

        if ((uvc_h264_height != sensor_width) || (uvc_h264_width != sensor_height))
            fmt.scaler.enable = 1;

        spin_unlock_irqrestore(&uvc_h264_format_lock, flags);

        ret = isp_set_format(camera_handle, &fmt);
        assert(!ret);

        ret = isp_request_buffer(camera_handle, &fmt);
        assert(!ret);

        info = isp_get_info(camera_handle);
        assert(info);

        fmt_a = (char)(info->data_fmt >> 0);
        fmt_b = (char)(info->data_fmt >> 8);
        fmt_c = (char)(info->data_fmt >> 16);
        fmt_d = (char)(info->data_fmt >> 24);
        printf("channel         = %s\n", (char *)camera_handle->ptr);
        printf("sensor_name     = %s\n", info->name);
        printf("width           = %d\n", info->width);
        printf("height          = %d\n", info->height);
        printf("fps             = %d\n", info->fps);
        printf("data_fmt        = %c%c%c%c\n", fmt_a, fmt_b, fmt_c, fmt_d);
        printf("line_length     = %d\n", info->line_length);
        printf("frame_size      = %d\n", info->frame_size);
        printf("frame_align_size= %d\n", info->frame_align_size);

        memset(&rotator_config, 0, sizeof(rotator_config));
        rotator_config.frame_height = info->height;
        rotator_config.frame_width = info->width;
        rotator_config.src_stride = info->width;
        rotator_config.dst_stride = info->height;
        rotator_config.src_fmt = ROTATOR_NV12;
        rotator_config.dst_fmt = ROTATOR_NV12;
        rotator_config.horizontal_mirror = ROTATOR_NO_MIRROR;
        rotator_config.vertical_mirror = ROTATOR_NO_MIRROR;
        rotator_config.rotate_angle = ROTATOR_ANGLE_90;

        /* 申请旋转使用的buf */
        rotator_vaddr = memalign(256, ALIGN(info->frame_size, cache_line_size()));
        assert(rotator_vaddr);

        /* 需要90度旋转 宽高互换 */
        h264_param.width = info->height;
        h264_param.height = info->width;
        h264_param.gop_size = info->fps;
        h264_param.bitrate_mode = HELIX_BITRATE_VBR;
        if (h264_param.width < 720)
            h264_param.bitrate = 100*1000;
        else
            h264_param.bitrate = 200*1000;

        out_size = info->width * info->height;
        out_buf = memalign(256, ALIGN(out_size, cache_line_size()));
        assert(out_buf);

        encoder = helix_h264_encoder_init(&h264_param);
        assert(encoder);

        ret = isp_power_on(camera_handle);
        assert(!ret);

        ret = isp_stream_on(camera_handle);
        assert(!ret);

        while (uvc_h264_stream_on) {
            void *mem = isp_wait_frame(camera_handle);
            if (mem) {
                rotator_config.src_buf = mem;
                rotator_config.dst_buf = rotator_vaddr;

                rotator_conversion(&rotator_config);

                isp_put_frame(camera_handle, mem);

                size = helix_h264_encoder_encode(encoder, rotator_vaddr, out_buf, out_size);
                if (size > 0) {
                    uvc_buf.mem = out_buf;
                    uvc_buf.length = size;
                    uvc_buf.complete = uvc_h264_buf_complete;

                    /* 如果uvc的宽高与摄像头宽高不相等,重新初始化  90度旋转宽高互换 */
                    if ((fmt.width != uvc_h264_height) || (fmt.height != uvc_h264_width))
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

            /* 如果uvc的宽高与摄像头宽高不相等,重新初始化  90度旋转宽高互换 */
            if ((fmt.width != uvc_h264_height) || (fmt.height != uvc_h264_width))
                break;
        }

        isp_stream_off(camera_handle);
        isp_power_off(camera_handle);
        isp_free_buffer(camera_handle);

        helix_h264_encoder_deinit(encoder);
        free(out_buf);
        free(rotator_vaddr);
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

static void uac1_connect_callback(int connect)
{
    printf("%s %d\n", __func__, connect);
}

/* uac playback */
#define GPIO_AMP_ENABLE      GPIO_PB(13)

#define BUF_SIZE_MS     100
#define PKT_SIZE_MS     10

static struct pcm_device *playback_codec;

static volatile int playback_on;
static thread_waiter_t playback_waiter;
static u32 playback_rate;

static u8 p_mute;
static thread_waiter_t p_mute_waiter;

static u16 p_volume = 60;
static thread_waiter_t p_volume_waiter;

static void p_mute_thread(void *data)
{
    while (1) {
        thread_waiter_wait(&p_mute_waiter);
        pcm_set_mute(playback_codec, p_mute);
    }
}

static int p_feature_mute(u8 request, void *buf, u16 len)
{
    u8 *data = (u8 *)buf;

    if (len < 1)
        return -EOPNOTSUPP;

    len = 1;

    switch (request)
    {
        case UAC_SET_CUR:
            p_mute = *data;
            thread_waiter_wakeup(&p_mute_waiter);
            break;
        case UAC_GET_CUR:
            *data = p_mute;
            break;
        default:
            return -EOPNOTSUPP;
    }

    return len;
}

static void p_volume_thread(void *data)
{
    while (1) {
        thread_waiter_wait(&p_volume_waiter);
        pcm_set_volume(playback_codec, p_volume);
    }
}

static int p_feature_volume(u8 request, void *buf, u16 len)
{
    u16 *data = (u16 *)buf;

    if (len < 2)
        return -EOPNOTSUPP;

    len = 2;

    switch (request)
    {
        case UAC_SET_CUR:
            p_volume = *data;
            thread_waiter_wakeup(&p_volume_waiter);
            break;
        case UAC_GET_CUR:
            *data = p_volume;
            break;
        case UAC_GET_MIN:
            *data = 0;
            break;
        case UAC_GET_MAX:
            *data = 100;
            break;
        case UAC_GET_RES:
            *data = 80;
            break;
        default:
            return -EOPNOTSUPP;
    }

    return len;
}

static int p_feature_callback(u8 type, u8 request, void *buf, u16 len)
{
    int ret = -EOPNOTSUPP;

    switch (type)
    {
        case UAC_FU_MUTE:
            ret = p_feature_mute(request, buf, len);
            break;
        case UAC_FU_VOLUME:
            ret = p_feature_volume(request, buf, len);
            break;
        default:
            break;
    }

    return ret;
}

static int uac1_start_playback_cb(u32 rate)
{
    playback_rate = rate;
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
    u8 *playback_buffer;
    struct pcm_device *playback_dai;

    struct pcm_params params = {
        .channels = 1,
        .pcm_data_fmt = pcm_fmt_S16LE,
        .pcm_sample_rate = pcm_rate_48000,
        .pcm_interface = pcm_interface_i2s,
        .i2s_frame_mode = i2s_LR_mode,
        .i2s_bclk_direction = i2s_bclk_codec_master,
        .i2s_frame_direction = i2s_frame_codec_master,
    };

    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        assert(!gpio_request(GPIO_AMP_ENABLE, "AMP_ENABLE"));
        gpio_set_func(GPIO_AMP_ENABLE , GPIO_OUTPUT0);
    }

    playback_dai = pcm_get("aic0-playback");
    assert(playback_dai);
    playback_codec = pcm_get("icodec-playback");
    assert(playback_codec);

    while (1) {
        while (!playback_on)
            thread_waiter_wait(&playback_waiter);

        printf("playback start\n");

        rate = playback_rate;
        switch (rate) {
            case 8000:
                params.pcm_sample_rate = pcm_rate_8000;
                break;
            case 16000:
                params.pcm_sample_rate = pcm_rate_16000;
                break;
            case 32000:
                params.pcm_sample_rate = pcm_rate_32000;
                break;
            case 48000:
                params.pcm_sample_rate = pcm_rate_48000;
                break;
            default:
                panic("Unsupported sampling rate %d\n", rate);
        }

        frame_size = pcm_frame_size(&params);

        playback_len = rate * frame_size / 1000 * PKT_SIZE_MS;
        playback_buffer = malloc(playback_len);
        assert(playback_buffer);

        ret = pcm_private_ctrl(playback_dai, "sysclk-set-rate", rate * 256);
        assert(!ret);
        ret = pcm_private_ctrl(playback_dai, "sysclk-set-output", 1);
        assert(!ret);

        ret = pcm_enable(playback_codec, &params);
        assert(!ret);
        ret = pcm_enable(playback_dai, &params);
        assert(!ret);

        pcm_set_volume(playback_codec, p_volume);

        ret = pcm_start(playback_codec);
        assert(!ret);
        ret = pcm_start(playback_dai);
        assert(!ret);

        // 使能功放引脚
        if (gpio_is_valid(GPIO_AMP_ENABLE))
            gpio_set_value(GPIO_AMP_ENABLE, 1);

        while (playback_on) {
            playback_len = rate * frame_size / 1000 * PKT_SIZE_MS;
            len = 0;
            while (len < playback_len) {
                ret = gadget_uac1_playback_read(playback_buffer + len, playback_len - len);
                if (ret == 0) {
                    msleep(PKT_SIZE_MS);
                } else if (ret < 0) {
                    printf("gadget_uac1_playback_read error %d\n", ret);
                    break;
                }

                len += ret;
            }

            if (len != playback_len)
                break;

            playback_len = playback_len / frame_size;
            len = 0;
            while (len < playback_len) {
                ret = pcm_write_frame(playback_dai, playback_buffer + len * frame_size, playback_len - len);
                if (ret < 0) {
                    printf("pcm_write_frame error %d\n", ret);
                    break;
                }

                len += ret;
            }
        }

        // 关闭功放引脚
        if (gpio_is_valid(GPIO_AMP_ENABLE))
            gpio_set_value(GPIO_AMP_ENABLE, 0);

        pcm_stop(playback_dai);
        pcm_stop(playback_codec);

        pcm_disable(playback_dai);
        pcm_disable(playback_codec);

        ret = pcm_private_ctrl(playback_dai, "sysclk-set-output", 0);
        assert(!ret);

        free(playback_buffer);

        printf("playback stop\n");
    }
}

/* uac capture */
static struct pcm_device *capture_codec;

static volatile int capture_on;
static thread_waiter_t capture_waiter;
static u32 capture_rate;

static u8 c_mute;
static thread_waiter_t c_mute_waiter;

static u16 c_volume = 60;
static thread_waiter_t c_volume_waiter;

static void c_mute_thread(void *data)
{
    while (1) {
        thread_waiter_wait(&c_mute_waiter);
        pcm_set_mute(capture_codec, c_mute);
    }
}

static int c_feature_mute(u8 request, void *buf, u16 len)
{
    u8 *data = (u8 *)buf;

    if (len < 1)
        return -EOPNOTSUPP;

    len = 1;

    switch (request)
    {
        case UAC_SET_CUR:
            c_mute = *data;
            thread_waiter_wakeup(&c_mute_waiter);
            break;
        case UAC_GET_CUR:
            *data = c_mute;
            break;
        default:
            return -EOPNOTSUPP;
    }

    return len;
}

static void c_volume_thread(void *data)
{
    while (1) {
        thread_waiter_wait(&c_volume_waiter);
        pcm_set_volume(capture_codec, c_volume);
    }
}

static int c_feature_volume(u8 request, void *buf, u16 len)
{
    u16 *data = (u16 *)buf;

    if (len < 2)
        return -EOPNOTSUPP;

    len = 2;

    switch (request)
    {
        case UAC_SET_CUR:
            c_volume = *data;
            thread_waiter_wakeup(&c_volume_waiter);
            break;
        case UAC_GET_CUR:
            *data = c_volume;
            break;
        case UAC_GET_MIN:
            *data = 0;
            break;
        case UAC_GET_MAX:
            *data = 100;
            break;
        case UAC_GET_RES:
            *data = 80;
            break;
        default:
            return -EOPNOTSUPP;
    }

    return len;
}

static int c_feature_callback(u8 type, u8 request, void *buf, u16 len)
{
    int ret = -EOPNOTSUPP;

    switch (type)
    {
        case UAC_FU_MUTE:
            ret = c_feature_mute(request, buf, len);
            break;
        case UAC_FU_VOLUME:
            ret = c_feature_volume(request, buf, len);
            break;
        default:
            break;
    }

    return ret;
}

static int uac1_start_capture_cb(u32 rate)
{
    capture_rate = rate;
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

static void Ns_Process(NoiseSuppressionC* handle_ns, float *sp, float *out, int cell_num,
        short* spframe,short* outframe,int size)
{
	int i,j,k;
	const float *const *p = (const float* const*)&sp;
	float *const *q = &out;

	k = size/(cell_num*2);

	if (k == 0)
		return;

	for (i = 0;i < k;i++){
		for(j = 0;j < cell_num;j++){
			sp[j] = spframe[j+(i*cell_num)];
		}
		memset(out,0,cell_num*sizeof(float));

        WebRtcNs_AnalyzeCore(handle_ns, *p);
        WebRtcNs_ProcessCore(handle_ns, p, 1, q);

 		for(j = 0;j < cell_num;j++){
			outframe[j+(i*cell_num)] = out[j];
		}
	}
}

static void pcm_capture_thread(void *data)
{
    int ret;
    int len;
    u32 rate;
    u32 frame_size;
    u32 capture_len;
    u8 *capture_buffer;
    struct pcm_device *capture_dai;

    int cell_num;
    float *ns_sp, *ns_out;
    NoiseSuppressionC* handle_ns;

    struct pcm_params params = {
        .channels = 1,
        .pcm_data_fmt = pcm_fmt_S16LE,
        .pcm_sample_rate = pcm_rate_48000,
        .pcm_interface = pcm_interface_i2s,
        .i2s_frame_mode = i2s_LR_mode,
        .i2s_bclk_direction = i2s_bclk_codec_master,
        .i2s_frame_direction = i2s_frame_codec_master,
    };

    capture_dai = pcm_get("aic0-icodec-capture");
    assert(capture_dai);
    capture_codec = pcm_get("icodec-capture");
    assert(capture_codec);

    while (1) {
        while (!capture_on)
            thread_waiter_wait(&capture_waiter);

        printf("capture start\n");

        rate = capture_rate;
        switch (rate) {
            case 8000:
                cell_num = 80;
                params.pcm_sample_rate = pcm_rate_8000;
                break;
            case 16000:
                cell_num = 160;
                params.pcm_sample_rate = pcm_rate_16000;
                break;
            case 32000:
                cell_num = 160;
                params.pcm_sample_rate = pcm_rate_32000;
                break;
            case 48000:
                cell_num = 160;
                params.pcm_sample_rate = pcm_rate_48000;
                break;
            default:
                panic("Unsupported sampling rate %d\n", rate);
        }

        frame_size = pcm_frame_size(&params);

        capture_len = rate * frame_size / 1000 * PKT_SIZE_MS;
        capture_buffer = malloc(capture_len);
        assert(capture_buffer);

        ns_sp = (float*)malloc(cell_num * sizeof(float));
        assert(ns_sp);

        ns_out = (float*)malloc(cell_num * sizeof(float));
        assert(ns_out);

        /* 降噪算法目前只支持S16LE */
        assert(frame_size == 2);

        handle_ns = malloc(sizeof(NoiseSuppressionC));
        assert(handle_ns);
        ret = WebRtcNs_InitCore(handle_ns, rate);
        assert(!ret);
        ret = WebRtcNs_set_policy_core(handle_ns, 2); /* 降噪等级 */
        assert(!ret);

        ret = pcm_private_ctrl(capture_dai, "sysclk-set-rate", rate * 256);
        assert(!ret);
        ret = pcm_private_ctrl(capture_dai, "sysclk-set-output", 1);
        assert(!ret);

        /* amic 打开偏置电压 */
        ret = pcm_private_ctrl(capture_codec, "bias-on", 1);
        assert(!ret);

        /*
        * 开始录音
        */
        ret = pcm_enable(capture_dai, &params);
        assert(!ret);
        ret = pcm_enable(capture_codec, &params);
        assert(!ret);

        pcm_set_volume(capture_codec, c_volume);

        ret = pcm_start(capture_codec);
        assert(!ret);
        ret = pcm_start(capture_dai);
        assert(!ret);

        while (capture_on) {
            capture_len = rate / 1000 * PKT_SIZE_MS;
            len = 0;
            while (len < capture_len) {
                ret = pcm_read_frame(capture_dai, capture_buffer + len * frame_size, capture_len - len);
                if (ret == 0) {
                    msleep(PKT_SIZE_MS);
                } else if (ret < 0) {
                    printf("pcm_read_frame error %d\n", ret);
                    break;
                }

                len += ret;
            }

            Ns_Process(handle_ns, ns_sp, ns_out, cell_num, (short*)(capture_buffer), (short*)capture_buffer, capture_len * frame_size);

            capture_len = capture_len * frame_size;
            len = 0;
            while (len < capture_len) {
                ret = gadget_uac1_capture_write(capture_buffer + len, capture_len - len);
                if (ret < 0) {
                    printf("gadget_uac1_capture_write error %d\n", ret);
                    break;
                }

                len += ret;
            }
        }


        pcm_stop(capture_codec);
        pcm_stop(capture_dai);

        pcm_disable(capture_codec);
        pcm_disable(capture_dai);

        ret = pcm_private_ctrl(capture_codec, "bias-on", 0);
        assert(!ret);

        ret = pcm_private_ctrl(capture_dai, "sysclk-set-output", 0);
        assert(!ret);

        free(handle_ns);
        free(ns_out);
        free(ns_sp);

        free(capture_buffer);
        printf("capture stop\n");
    }
}

static struct uac1_params uac1_param = {
    /* playback */
    .p_chmask = UAC_CH_LAYOUT_MONO,
    .p_ssize = 2,
    .p_srate = 16000,
    .p_feature = UAC_CONTROL_BIT(UAC_FU_MUTE) | UAC_CONTROL_BIT(UAC_FU_VOLUME),
    .p_feature_callback = p_feature_callback,
    .start_playback_callback = uac1_start_playback_cb,
    .stop_playback_callback = uac1_stop_playback_cb,

    /* capture */
    .c_chmask = UAC_CH_LAYOUT_MONO,
    .c_ssize = 2,
    .c_srate = 16000,
    .c_feature = UAC_CONTROL_BIT(UAC_FU_MUTE) | UAC_CONTROL_BIT(UAC_FU_VOLUME),
    .c_feature_callback = c_feature_callback,
    .start_capture_callback = uac1_start_capture_cb,
    .stop_capture_callback = uac1_stop_capture_cb,

    .buffer_size_ms = BUF_SIZE_MS,
    .connect_cb = uac1_connect_callback,
};

int gadget_usb_uvc_uac1_test(void)
{
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
    thread_waiter_init(&p_mute_waiter);
    thread_waiter_init(&p_volume_waiter);
    thread_waiter_init(&playback_waiter);
    thread_create("p_mute_thread", 1024, p_mute_thread, NULL);
    thread_create("p_volume_thread", 1024, p_volume_thread, NULL);
    thread_create("pcm_playback_thread", 4096, pcm_playback_thread, NULL);

    /* uac capture init */
    thread_waiter_init(&c_mute_waiter);
    thread_waiter_init(&c_volume_waiter);
    thread_waiter_init(&capture_waiter);
    thread_create("c_mute_thread", 1024, c_mute_thread, NULL);
    thread_create("c_volume_thread", 1024, c_volume_thread, NULL);
    thread_create("pcm_capture_thread", 8192, pcm_capture_thread, NULL); /* 降噪算法需要更大栈 */

    gadget_uvc_n_uac1_init(&usb_id, uvc_config, ARRAY_SIZE(uvc_config), &uac1_param);

    return 0;
}
