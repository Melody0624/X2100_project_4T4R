
#include <common.h>
#include <os.h>
#include <errno.h>
#include <usb/gadget_uvc_uac1.h>
#include <driver/camera_isp.h>
#include <driver/isp_tuning.h>

#include <driver/gpio.h>
#include <driver/pcm.h>

#include <helix/helix_jpeg_encoder.h>

#define WEBCAM_VENDOR_ID        0x1d6b    /* Linux Foundation */
#define WEBCAM_PRODUCT_ID        0x0102    /* Webcam A/V gadget */

static struct gadget_id usb_id = {
    .vendor_id = WEBCAM_VENDOR_ID,
    .product_id = WEBCAM_PRODUCT_ID,
};

/* ---- uvc ---- */
static unsigned char uvc_stream_on;
static struct uvc_buffer uvc_buf;
static unsigned char uvc_buf_use;

static thread_waiter_t uvc_stream_waiter;
static thread_waiter_t uvc_buf_waiter;

static camera_hd_t *camera_handle;

/* fps large to small Sort*/
static const unsigned int mjpeg_frame_fps[] = {
    30,
};

/* Frame size small to large sort*/
static struct uvc_frame_config uvc_mjpeg_frames[] = {
    {
        .width = 1920,
        .height = 1080,
        .fps_num = ARRAY_SIZE(mjpeg_frame_fps),
        .frame_fps = mjpeg_frame_fps,
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


static const struct uvc_device_config uvc_config = {
    .format_num = ARRAY_SIZE(uvc_frame_format),
    .formats = uvc_frame_format,
};

static struct frame_image_format uvc_output_fmt = {
    .width              = 1920,
    .height             = 1080,
    .pixel_format       = CAMERA_PIX_FMT_NV12,

    .scaler.enable      = 0,
    .scaler.width       = 1920,
    .scaler.height      = 1080,

    .crop.enable        = 0,
    .crop.top           = 0,
    .crop.left          = 0,
    .crop.width         = 1920,
    .crop.height        = 1080,

    .frame_nums         = 2,
};


static void uvc_connect_callback(int connect)
{
    printf("uvc_connect_callback %d\n", connect);
}

static void uvc_format_callback(const struct uvc_video_format *format)
{
    char *data = (char *)&format->fcc;
    printf("uvc format: %c%c%c%c, width %d, height %d, fps %d\n", data[0], data[1], data[2], data[3], format->width,  format->height,  format->fps);
}

static int uvc_stream_callback(int enable)
{
    printf("uvc_stream_callback %d\n", enable);
    uvc_stream_on = enable;
    if (enable)
        thread_waiter_wakeup(&uvc_stream_waiter);

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
    int out_size;
    void *out_buf;
    struct helix_jpeg_encoder *encoder;
    char *device_name;
    struct camera_info *info;
    struct frame_image_format fmt;

    info = isp_get_info(camera_handle);
    device_name = (char *)camera_handle->ptr;

    char fmt_a = (char)(info->data_fmt >> 0);
    char fmt_b = (char)(info->data_fmt >> 8);
    char fmt_c = (char)(info->data_fmt >> 16);
    char fmt_d = (char)(info->data_fmt >> 24);
    printf("channel         = %s\n", device_name);
    printf("sensor_name     = %s\n", info->name);
    printf("width           = %d\n", info->width);
    printf("height          = %d\n", info->height);
    printf("fps             = %d\n", info->fps);
    printf("data_fmt        = %c%c%c%c\n", fmt_a, fmt_b, fmt_c, fmt_d);
    printf("line_length     = %d\n", info->line_length);
    printf("frame_size      = %d\n", info->frame_size);
    printf("frame_align_size= %d\n", info->frame_align_size);

    ret = isp_get_format(camera_handle, &fmt);
    assert(!ret);

    struct helix_jpeg_encoder_param jpeg_param = {
        .compress_quality = 50,
        .width = fmt.width,
        .height = fmt.height,
    };
    encoder = helix_jpeg_encoder_init(&jpeg_param);
    assert(encoder);

    int out_buf_size = fmt.width * fmt.height * 3 / 2;
    out_size = out_buf_size;
    out_buf = (unsigned char *)malloc(out_buf_size);
    assert(out_buf);

    uvc_buf_use = 0;
    uvc_buf.complete = uvc_buf_complete;
    uvc_buf.length = fmt.frame_size;

    while (1) {
        while (!uvc_stream_on)
            thread_waiter_wait(&uvc_stream_waiter);

        ret = isp_power_on(camera_handle);
        assert(!ret);

        ret = isp_stream_on(camera_handle);
        assert(!ret);

        while (uvc_stream_on) {
            void *mem = isp_wait_frame(camera_handle);
            if (mem) {
                void *y_mem = mem;
                void *uv_mem = mem + info->line_length * info->height;

                out_size = helix_jpeg_encoder_encode_nv12_separate(encoder, y_mem, uv_mem, out_buf, out_buf_size);

                isp_put_frame(camera_handle, mem);

                if (out_size > 0) {
                    uvc_buf.mem = out_buf;
                    uvc_buf.length = out_size;
                    uvc_buf_use = 1;
                    ret = gadget_uvc_write(&uvc_buf, 1, -1);
                    if (ret)
                        uvc_buf_use = 0;
                } else {
                    printf("hw jpeg fail %d\n", out_size);
                }
            }

            while (uvc_buf_use)
                thread_waiter_wait(&uvc_buf_waiter);
        }

        isp_stream_off(camera_handle);
        isp_power_off(camera_handle);
    }
    free(out_buf);
    helix_jpeg_encoder_deinit(encoder);
}

struct uvc_callback callback = {
    .format_cb = uvc_format_callback,
    .stream_cb = uvc_stream_callback,
    .connect_cb = uvc_connect_callback,
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

static struct pcm_device *playback_dai;
static struct pcm_device *playback_codec;

static volatile int playback_on;
static thread_waiter_t playback_waiter;

static u8 p_mute;
static thread_waiter_t p_mute_waiter;

static u16 p_volume = 80;
static thread_waiter_t p_volume_waiter;

static struct pcm_params playback_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

void p_mute_thread(void *data)
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

void p_volume_thread(void *data)
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


void pcm_playback_thread(void *data)
{
    int len;
    int frame;
    u32 playback_len;
    u8 *playback_buffer;

    /* max rate buf size, max sample rate 48000 */
    playback_len = 48000 * pcm_frame_size(&playback_params) / 1000 * PKT_SIZE_MS;
    playback_buffer = malloc(playback_len);
    assert(playback_buffer);

    while (1) {
        while (!playback_on)
            thread_waiter_wait(&playback_waiter);

        printf("playback start\n");

        pcm_enable(playback_codec, &playback_params);

        pcm_enable(playback_dai, &playback_params);

        pcm_set_volume(playback_codec, p_volume);

        pcm_start(playback_codec);

        pcm_start(playback_dai);

        // 使能功放引脚
        if (gpio_is_valid(GPIO_AMP_ENABLE))
            gpio_set_value(GPIO_AMP_ENABLE, 1);

        while (playback_on) {
            playback_len = pcm_data_sample_rate(playback_params.pcm_sample_rate) * pcm_frame_size(&playback_params) / 1000 * PKT_SIZE_MS;

            len = gadget_uac1_playback_read(playback_buffer, playback_len);
            if (len == 0) {
                msleep(PKT_SIZE_MS);
            } else if (len > 0) {
                len = len / pcm_frame_size(&playback_params);
                frame = pcm_write_frame(playback_dai, playback_buffer, len);
                if (frame < 0)
                    printf("pcm_write_frame error %d\n", frame);
                else if (frame != len)
                    printf("pcm_write_frame frame(%d) != uvc_frame(%d)\n", frame, len);
            } else {
                printf("gadget_uac1_playback_read error %d\n", len);
            }
        }

        // 关闭功放引脚
        if (gpio_is_valid(GPIO_AMP_ENABLE))
            gpio_set_value(GPIO_AMP_ENABLE, 0);

        pcm_disable(playback_dai);

        pcm_disable(playback_codec);

        printf("playback stop\n");
    }

}

/* uac capture */
static struct pcm_device *capture_dai;
static struct pcm_device *capture_codec;

static volatile int capture_on;
static thread_waiter_t capture_waiter;

static u8 c_mute;
static thread_waiter_t c_mute_waiter;

static u16 c_volume = 68;
static thread_waiter_t c_volume_waiter;

static struct pcm_params capture_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

void c_mute_thread(void *data)
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

void c_volume_thread(void *data)
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

void pcm_capture_thread(void *data)
{
    int len;
    int frame;
    u32 capture_len;
    u8 *capture_buffer;

    /* max rate buf size, max sample rate 48000 */
    capture_len = 48000 * pcm_frame_size(&capture_params) / 1000 * PKT_SIZE_MS;
    capture_buffer = malloc(capture_len);
    assert(capture_buffer);

    while (1) {
        while (!capture_on)
            thread_waiter_wait(&capture_waiter);

        printf("capture start\n");

        /*
        * 开始录音
        */
        pcm_enable(capture_dai, &capture_params);
        pcm_enable(capture_codec, &capture_params);

        pcm_set_volume(capture_codec, c_volume);

        pcm_start(capture_codec);
        pcm_start(capture_dai);

        while (capture_on) {
            frame = pcm_read_frame(capture_dai, capture_buffer, capture_len / pcm_frame_size(&capture_params));
            if (frame == 0) {
                msleep(PKT_SIZE_MS);
            } else if (frame > 0) {
                frame = frame * pcm_frame_size(&capture_params);
                len = gadget_uac1_capture_write(capture_buffer, frame);
                if (len < 0)
                    printf("gadget_uac1_capture_write error %d\n", len);
                else if (len != frame)
                    printf("gadget_uac1_capture_write len(%d) != capture_len(%d)\n", len, frame);

            } else {
                printf("pcm_read_frame error %d\n", frame);
            }
        }

        pcm_disable(capture_codec);
        pcm_disable(capture_dai);

        printf("capture stop\n");
    }

    free(capture_buffer);
}

struct uac1_params uac1_param = {
    /* playback */
    .p_chmask = UAC_CH_LAYOUT_MONO,
    .p_ssize = 2,
    .p_srate = 48000,
    .p_feature = UAC_CONTROL_BIT(UAC_FU_MUTE) | UAC_CONTROL_BIT(UAC_FU_VOLUME),
    .p_feature_callback = p_feature_callback,
    .start_playback_callback = uac1_start_playback_cb,
    .stop_playback_callback = uac1_stop_playback_cb,

    /* capture */
    .c_chmask = UAC_CH_LAYOUT_MONO,
    .c_ssize = 2,
    .c_srate = 48000,
    .c_feature = UAC_CONTROL_BIT(UAC_FU_MUTE) | UAC_CONTROL_BIT(UAC_FU_VOLUME),
    .c_feature_callback = c_feature_callback,
    .start_capture_callback = uac1_start_capture_cb,
    .stop_capture_callback = uac1_stop_capture_cb,

    .buffer_size_ms = BUF_SIZE_MS,
    .connect_cb = uac1_connect_callback,
};

int gadget_usb_uvc_uac1_test(int index, int channel)
{
    int ret;
    char *device_name;
    struct camera_info *sensor_info;

    /* uvc init */

    camera_handle = isp_detect(index, channel);
    if (!camera_handle) {
        printf("mscaler%d-ch%d not found camera\n",index , channel);
        goto isp_detect_error;
    }

    device_name = (char *)camera_handle->ptr;

    if (!uvc_output_fmt.scaler.enable && !uvc_output_fmt.crop.enable) {
        /* 输出设置为Sensor分辨率 */
        sensor_info = isp_get_sensor_info(camera_handle);
        if (sensor_info) {
            uvc_output_fmt.width = sensor_info->width;
            uvc_output_fmt.height = sensor_info->height;
        }
    }

    uvc_mjpeg_frames[0].width = uvc_output_fmt.width;
    uvc_mjpeg_frames[0].height = uvc_output_fmt.height;

    ret = isp_set_format(camera_handle, &uvc_output_fmt);
    if (ret < 0) {
        printf("%s set format failed\n", device_name);
        goto isp_set_fmt_error;
    }

    ret = isp_request_buffer(camera_handle, &uvc_output_fmt);
    if (ret < 0) {
        printf("%s requset buffer failed\n", device_name);
        goto isp_request_buf_error;
    }

    thread_waiter_init(&uvc_buf_waiter);
    thread_waiter_init(&uvc_stream_waiter);
    thread_create("usb gadget uvc thread", 8192, usb_gadget_uvc_thread, NULL);

    /* uac init */
    /* uac playback init */
    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        assert(!gpio_request(GPIO_AMP_ENABLE, "AMP_ENABLE"));
        gpio_set_func(GPIO_AMP_ENABLE , GPIO_OUTPUT0);
    }

    playback_dai = pcm_get("aic0-playback");
    assert(playback_dai);
    playback_codec = pcm_get("icodec-playback");
    assert(playback_codec);

    pcm_private_ctrl(playback_dai, "sysclk-set-rate", 48000 * 256);
    pcm_private_ctrl(playback_dai, "sysclk-set-output", 1);
    pcm_set_volume(playback_codec, p_volume);

    thread_waiter_init(&p_mute_waiter);
    thread_waiter_init(&p_volume_waiter);
    thread_waiter_init(&playback_waiter);
    thread_create("p_mute_thread", 1024, p_mute_thread, NULL);
    thread_create("p_volume_thread", 1024, p_volume_thread, NULL);
    thread_create("pcm_playback_thread", 4096, pcm_playback_thread, NULL);

    /* uac capture init */
    capture_dai = pcm_get("aic0-icodec-capture");
    assert(capture_dai);
    capture_codec = pcm_get("icodec-capture");
    assert(capture_codec);

    pcm_private_ctrl(capture_dai, "sysclk-set-rate", 48000 * 256);
    pcm_private_ctrl(capture_dai, "sysclk-set-output", 1);

    /* amic 打开偏置电压 */
    pcm_private_ctrl(capture_codec, "bias-on", 1);

    pcm_set_volume(capture_codec, c_volume);

    thread_waiter_init(&c_mute_waiter);
    thread_waiter_init(&c_volume_waiter);
    thread_waiter_init(&capture_waiter);
    thread_create("c_mute_thread", 1024, c_mute_thread, NULL);
    thread_create("c_volume_thread", 1024, c_volume_thread, NULL);
    thread_create("pcm_capture_thread", 4096, pcm_capture_thread, NULL);

    gadget_uvc_uac1_init(&usb_id, &uvc_config, &callback, &uac1_param);

    return 0;

isp_request_buf_error:
isp_set_fmt_error:
    isp_release(camera_handle);
isp_detect_error:
    return -1;
}
