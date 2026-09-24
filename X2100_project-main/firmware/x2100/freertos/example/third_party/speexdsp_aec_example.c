#include <common.h>
#include <errno.h>
#include <os.h>

#include <driver/gpio.h>
#include <driver/pcm.h>

/**
 * for AD10x only
 * Using third_party/speexdsp, third_party/notch_filter(optional)
 * this example emulate a 3-channel uav device
 *  channel 0 : loopback stream, which simultaneously read playback stream's data
 *  channel 1 : capture stream, which recording enviromental sound
 *  channel 2 : algorithm stream, which has already been denoised and echo-cancelled
 * to use this demo
 *  simply connect usb cable to computer and play sound using this uac device
 *  simultaneously recording 3-channel audio stream using wave monitor software like Audacity
*/

#ifndef CONFIG_SPEEXDSP
#error CONFIG_SPEEXDSP not configure!!
#endif

#ifndef CONFIG_USB_GADGET_UAC1
#error CONFIG_USB_GADGET_UAC1 not configured!!
#endif

#ifdef CONFIG_NOTCH_FILTER
#define NOTCH_FILTER
#endif

#ifdef NOTCH_FILTER
#include "notch_filter/notch_filter.h"
#endif

#include "speex/speex_echo.h"
#include "speex/speex_preprocess.h"
#include "usb/gadget_uac1.h"

#define BUF_SIZE_MS     100
#define PKT_SIZE_MS     10

#define TEST_RATE       48000

static const struct gadget_id uac1_id = {
    .vendor_id = 0x1d6b,
    .product_id = 0x0101
};

static void uac1_connect_callback(int connect)
{
    printf("%s %d\n", __func__, connect);
}

/* uac playback */
static int gpio_pb_enable = GPIO_PB(13);
static struct pcm_device *playback_codec;

static volatile int playback_on;
static thread_waiter_t playback_waiter;

static struct pcm_params playback_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

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
    u32 playback_len;
    u8 *playback_buffer;

    struct pcm_device *playback_dai = pcm_get("aic-playback");
    assert(playback_dai);
    playback_codec = pcm_get("icodec-playback");
    assert(playback_codec);

    printf("playback thread start\n");

    pcm_enable(playback_codec, &playback_params);

    pcm_enable(playback_dai, &playback_params);

    pcm_start(playback_codec);

    // 使能功放引脚
    if (gpio_pb_enable >= 0)
        gpio_direction_output(gpio_pb_enable, 1);

    /* max rate buf size */
    playback_len = TEST_RATE * pcm_frame_size(&playback_params) / 1000 * PKT_SIZE_MS;
    playback_buffer = malloc(playback_len);
    assert(playback_buffer);

    while(1) {
        while (!playback_on)
            thread_waiter_wait(&playback_waiter);

        pcm_start(playback_dai);

        while (playback_on) {
            len = gadget_uac1_playback_read(playback_buffer, playback_len);
            if (len > 0)
                pcm_write_frame(playback_dai, playback_buffer, len / pcm_frame_size(&playback_params));
            else
                msleep(PKT_SIZE_MS);
        }

        pcm_stop(playback_dai);
    }

    pcm_disable(playback_dai);

    pcm_disable(playback_codec);

    // 关闭功放引脚
    if (gpio_pb_enable >= 0)
        gpio_direction_output(gpio_pb_enable, 0);
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

#define SPEEX_ECHO_FILTER_PKT   3

void pcm_capture_thread(void *data)
{
    struct pcm_device *l_dai = pcm_get("aic-loopback");
    struct pcm_device *c_dai = pcm_get("aic-capture");
    struct pcm_device *c_codec = pcm_get("icodec-capture");

    int i, f_size;
    u32 len;
    u32 size;
    s16 *cap_buf;
    s16 *lop_buf;
    s16 *aec_buf;
    s16 *uac_buf;

    int rate = TEST_RATE;

    assert(c_dai);
    assert(c_codec);
    assert(l_dai);

    SpeexEchoState *speex_echo_state;
    SpeexPreprocessState *speex_preprocess_state;

    /*
     * "aic-loopback" pcm device MUST set same params with capture
     */
    pcm_enable(l_dai, &capture_params);
    pcm_enable(c_dai, &capture_params);
    pcm_enable(c_codec, &capture_params);

    f_size = pcm_frame_size(&capture_params);
    size = pcm_data_sample_rate(capture_params.pcm_sample_rate) * PKT_SIZE_MS / 1000;
    len = size * f_size;
    cap_buf = malloc(len);
    assert(cap_buf);
    lop_buf = malloc(len);
    assert(lop_buf);
    aec_buf = malloc(len);
    assert(aec_buf);
    uac_buf = malloc(len * 3);
    assert(uac_buf);

    speex_echo_state = speex_echo_state_init(size, size * SPEEX_ECHO_FILTER_PKT);
    speex_preprocess_state = speex_preprocess_state_init(size, TEST_RATE);
    speex_echo_ctl(speex_echo_state, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    i = 1;
    speex_preprocess_ctl(speex_preprocess_state, SPEEX_PREPROCESS_SET_DENOISE, &i);
    speex_preprocess_ctl(speex_preprocess_state, SPEEX_PREPROCESS_SET_ECHO_STATE, speex_echo_state);

#ifdef NOTCH_FILTER
    /* eliminate power noise */
    float freq[] = {1000, 2000, 3000};
    struct notch_filter *nf = notch_filter_init(rate, 1.2, freq, ARRAY_SIZE(freq));
    assert(nf);
#endif

    pcm_start(c_codec);
    /*
     * "aic-loopback" pcm device MUST call pcm_start BEFORE "aic-capture" as follow
     *      or aec may invalidate
     */
    pcm_start(l_dai);
    pcm_start(c_dai);

    while (1) {
        pcm_read_frame(l_dai, lop_buf, size);
        pcm_read_frame(c_dai, cap_buf, size);

#ifdef NOTCH_FILTER
        for (i = 0; i < size; i ++)
            cap_buf[i] = notch_filter_process(nf, cap_buf[i]);
#endif

        speex_echo_cancellation(speex_echo_state, (void *)cap_buf, (void *)lop_buf, (void *)aec_buf);
        speex_preprocess_run(speex_preprocess_state, (void *)aec_buf);

        // copy loopback/capture/aec data into 3 channels
        for (i = 0; i < size; i ++) {
            uac_buf[i * 3 + 0] = lop_buf[i];
            uac_buf[i * 3 + 1] = cap_buf[i];
            uac_buf[i * 3 + 2] = aec_buf[i];
        }

        gadget_uac1_capture_write((void *)uac_buf, len * 3);
    }

    pcm_disable(c_codec);
    pcm_disable(c_dai);

    pcm_disable(l_dai);

    free(cap_buf);
}

struct uac1_params uac1_param = {
    /* playback */
    .p_chmask = UAC_CH_LAYOUT_MONO,
    .p_ssize = 2,
    .p_srate = TEST_RATE,
    .start_playback_callback = uac1_start_playback_cb,
    .stop_playback_callback = uac1_stop_playback_cb,

    /* capture */
    .c_chmask = UAC_CH_LAYOUT_2_1,
    .c_ssize = 2,
    .c_srate = TEST_RATE,

    .buffer_size_ms = BUF_SIZE_MS,
    .connect_cb = uac1_connect_callback,
};

int speexdsp_aec_test(void)
{
    thread_waiter_init(&playback_waiter);
    thread_create("pcm_capture_thread", 4096, pcm_capture_thread, NULL);
    thread_create("pcm_playback_thread", 4096, pcm_playback_thread, NULL);
    gadget_uac1_init(&uac1_id, &uac1_param);

    return 0;
}
