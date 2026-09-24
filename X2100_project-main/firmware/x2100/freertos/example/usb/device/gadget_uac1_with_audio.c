#include <common.h>
#include <usb/gadget_uac1.h>
#include <errno.h>
#include <os.h>

#include <driver/gpio.h>
#include <driver/pcm.h>

#define BUF_SIZE_MS     100
#define PKT_SIZE_MS     10

static const struct gadget_id uac1_id = {
    .vendor_id = 0x1d6b,
    .product_id = 0x0101
};

static unsigned int p_srates[] = {
    8000, 16000, 48000,
};

static void uac1_connect_callback(int connect)
{
    printf("%s %d\n", __func__, connect);
}

/* uac playback */
static int gpio_pa_enable = GPIO_PB(9);
static struct pcm_device *playback_codec;

static volatile int playback_on;
static thread_waiter_t playback_waiter;

static u32 playback_rate;

static u8 p_mute;
static thread_waiter_t p_mute_waiter;

static u16 p_volume = 60;
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
    s16 tmp;
    while (1) {
        thread_waiter_wait(&p_volume_waiter);
        tmp = p_volume;
        if (tmp == 0x8000) {
            pcm_set_volume(playback_codec, 0);
        } else if (tmp < 0) {
            tmp = -tmp;
            pcm_set_volume(playback_codec, 50 - (tmp / 656));
        } else {
            pcm_set_volume(playback_codec, 50 + (tmp / 656));
        }
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
            *data = 0x8001;
            break;
        case UAC_GET_MAX:
            *data = 0x7FFF;
            break;
        case UAC_GET_RES:
            *data = 0x0001;
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

void pcm_playback_thread(void *data)
{
    int len;
    u32 playback_len;
    u8 *playback_buffer;
    u32 rate;
    s16 tmp;

    struct pcm_device *playback_dai = pcm_get("aic-playback");
    assert(playback_dai);
    playback_codec = pcm_get("icodec-playback");
    assert(playback_codec);

    while(1) {
        while (!playback_on)
            thread_waiter_wait(&playback_waiter);

        printf("playback thread start\n");

        rate = playback_rate;
        /* 设置采样率 */
        switch (rate) {
            case 8000:
                playback_params.pcm_sample_rate = pcm_rate_8000;
                break;
            case 16000:
                playback_params.pcm_sample_rate = pcm_rate_16000;
                break;
            case 48000:
                playback_params.pcm_sample_rate = pcm_rate_48000;
                break;
            default:
                panic("Unsupported sampling rate %d\n", rate);
        }

        /* 注意！
        * 如果为外部codec，则必须在使能aic之前使能。
        * 不是外部则无此规定。
        */
        pcm_enable(playback_codec, &playback_params);

        pcm_enable(playback_dai, &playback_params);

        pcm_start(playback_codec);

        pcm_start(playback_dai);

        tmp = p_volume;
        if (tmp == 0x8000) {
            pcm_set_volume(playback_codec, 0);
        } else if (tmp < 0) {
            tmp = -tmp;
            pcm_set_volume(playback_codec, 50 - (tmp / 656));
        } else {
            pcm_set_volume(playback_codec, 50 + (tmp / 656));
        }

        // 使能功放引脚
        if (gpio_pa_enable >= 0)
            gpio_direction_output(gpio_pa_enable, 1);

        /* max rate buf size */
        playback_len = 48000 * pcm_frame_size(&playback_params) / 1000 * PKT_SIZE_MS;
        playback_buffer = malloc(playback_len);
        assert(playback_buffer);

        while (playback_on) {
            /* uac获取数据之前检查采样率，如果被修改需要重新初始化 */
            if (rate != playback_rate)
                break;

            playback_len = pcm_data_sample_rate(playback_params.pcm_sample_rate) * pcm_frame_size(&playback_params) / 1000 * PKT_SIZE_MS;

            len = gadget_uac1_playback_read(playback_buffer, playback_len);
            if (len > 0)
                pcm_write_frame(playback_dai, playback_buffer, len / pcm_frame_size(&playback_params));
            else
                msleep(PKT_SIZE_MS);
        }

        // 关闭功放引脚
        if (gpio_pa_enable >= 0)
            gpio_direction_output(gpio_pa_enable, 0);

        pcm_disable(playback_dai);

        pcm_disable(playback_codec);
    }
}

/* uac capture */
static struct pcm_params capture_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

void pcm_capture_thread(void *data)
{
    struct pcm_device *c_dai = pcm_get("aic-capture");
    struct pcm_device *c_codec = pcm_get("icodec-capture");
    int i, j;
    u32 len;
    u8 *buffer;

    assert(c_dai);
    assert(c_codec);

    pcm_private_ctrl(c_dai, "sysclk-set-rate", 48000 * 768);
    pcm_private_ctrl(c_dai, "sysclk-set-output", 1);

    /*
     * 注意!
     *
     * 对于外接的模拟mic
     * 内部codec需要提前设置enable的时候是否要打开偏置电压
     *
     * 如果是回采电路，则不需要打开偏置电压
     */
    pcm_private_ctrl(c_codec, "bias-on", 1);
    printf("We are bias-on, please know that\n");

    /*
     * 开始录音
     * 内部codec实际上只能录制一个通道的数据在左声道
     */
    pcm_enable(c_dai, &capture_params);
    pcm_enable(c_codec, &capture_params);

    len = pcm_data_sample_rate(capture_params.pcm_sample_rate) * pcm_frame_size(&capture_params) / 1000 * PKT_SIZE_MS;
    buffer = malloc(len);
    assert(buffer);

    pcm_start(c_codec);
    pcm_start(c_dai);

    while (1) {
        pcm_read_frame(c_dai, buffer, len / pcm_frame_size(&capture_params));

        /* 右声道拷贝到左声道 */
        for (i = 0; i < len / pcm_frame_size(&capture_params); i++) {
            for (j = 0; j < pcm_frame_size(&capture_params) / 2; j++)
                buffer[i * pcm_frame_size(&capture_params) + j] = buffer[i * pcm_frame_size(&capture_params) + (pcm_frame_size(&capture_params) / 2) + j];
        }

        gadget_uac1_capture_write(buffer, len);
    }

    pcm_disable(c_codec);
    pcm_disable(c_dai);

    free(buffer);
}

struct uac1_params uac1_param = {
    /* playback */
    .p_chmask = UAC_CH_LAYOUT_MONO,
    .p_ssize = 2,
    .p_srate_num = ARRAY_SIZE(p_srates),
    .p_srates = p_srates,
    .p_srate = 48000,
    .p_feature = UAC_CONTROL_BIT(UAC_FU_MUTE) | UAC_CONTROL_BIT(UAC_FU_VOLUME),
    .p_feature_callback = p_feature_callback,
    .start_playback_callback = uac1_start_playback_cb,
    .stop_playback_callback = uac1_stop_playback_cb,

    /* capture */
    .c_chmask = UAC_CH_LAYOUT_MONO,
    .c_ssize = 2,
    .c_srate = 48000,

    .buffer_size_ms = BUF_SIZE_MS,
    .connect_cb = uac1_connect_callback,
};

int gadget_usb_uac1_audio_test(void)
{
    thread_create("pcm_capture_thread", 4096, pcm_capture_thread, NULL);

    thread_waiter_init(&p_mute_waiter);
    thread_waiter_init(&p_volume_waiter);
    thread_waiter_init(&playback_waiter);
    thread_create("p_mute_thread", 1024, p_mute_thread, NULL);
    thread_create("p_volume_thread", 1024, p_volume_thread, NULL);
    thread_create("pcm_playback_thread", 4096, pcm_playback_thread, NULL);
    gadget_uac1_init(&uac1_id, &uac1_param);

    return 0;
}
