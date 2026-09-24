#include <stdio.h>
#include <driver/pcm.h>
#include <common.h>
#include <driver/gpio.h>
#include <wav_utils.h>

#include <include_bin.h>
INCBIN(audio, "example/resource/end_playing.aiff");

// 以 x2600_vast_v10 板子为例
// icodec 播放 end_playing.aiff 音频, 同时 dmic 录音, 录音结束且播放结束后播放录到的数据, 播放结束退出

static struct pcm_params playback_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

static struct pcm_params capture_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_dmic,
};

static volatile int playback_ok = 0;
static int gpio_spk_enable = GPIO_PB(5);

void write_thread_func(void *data)
{
    struct pcm_device *dai = pcm_get("aic-playback");
    struct pcm_device *codec = pcm_get("icodec-playback");

    pcm_enable(codec, &playback_params);

    pcm_enable(dai, &playback_params);

    pcm_start(codec);

    pcm_start(dai);

    if (gpio_spk_enable >= 0)
        gpio_direction_output(gpio_spk_enable, 1);

    void *audio = wav_pcm_data((void *) audioData);
    int frames = wav_pcm_frames((void *) audioData);

    pcm_write_frame(dai, (void *)audio, frames);

    pcm_disable(dai);

    pcm_disable(codec);

    if (gpio_spk_enable >= 0)
        gpio_direction_output(gpio_spk_enable, 0);

    playback_ok = 1;
    printf("We are playback_ok\n");
}

static unsigned int buffer[5 * 16 * 1024];

void dmic_aic_pcm_test(void)
{
    struct pcm_device *dai = pcm_get("dmic-capture");

    if (gpio_spk_enable >= 0)
        gpio_request(gpio_spk_enable, "spk_enable");

    thread_create("write_thread", 4096, write_thread_func, NULL);

    pcm_enable(dai, &capture_params);
    pcm_start(dai);

    pcm_read_frame(dai, buffer, sizeof(buffer)/pcm_frame_size(&capture_params));

    pcm_stop(dai);
    pcm_disable(dai);

    while (!playback_ok)
        msleep(1);

    printf("We are capture_ok\n");

    struct pcm_device *pdai = pcm_get("aic-playback");
    struct pcm_device *codec = pcm_get("icodec-playback");

    pcm_enable(pdai, &playback_params);

    pcm_enable(codec, &playback_params);

    if (gpio_spk_enable >= 0)
        gpio_direction_output(gpio_spk_enable, 1);

    pcm_start(codec);

    pcm_start(pdai);

    pcm_write_frame(pdai, (void *)buffer, sizeof(buffer) / pcm_frame_size(&playback_params));

    pcm_disable(pdai);

    pcm_disable(codec);

    if (gpio_spk_enable >= 0)
        gpio_direction_output(gpio_spk_enable, 0);

    printf("We are playback_ok again\n");
}
