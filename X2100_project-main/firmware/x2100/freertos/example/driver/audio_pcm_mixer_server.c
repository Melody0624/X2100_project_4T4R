#include <stdio.h>
#include <os.h>
#include <driver/pcm.h>
#include <include_bin.h>
#include <driver/gpio.h>
#include <driver/pcm_adapter.h>
#include <driver/pcm_mixer.h>
#include <wav_utils.h>

INCBIN(pcm, "example/resource/mono_s16le_48000.pcm");

INCBIN(wav, "example/resource/end_playing.aiff");

static struct pcm_params playback_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

static int gpio_pa_enable = GPIO_PB(9);

static struct pcm_device *aic_dai;
static struct pcm_device *codec;

static void enable_aic_and_codec(
    const char *dai_name, const char *codec_name, int sys_rate, int codec_gpio)
{

    aic_dai = pcm_get(dai_name);
    codec = pcm_get(codec_name);

    pcm_private_ctrl(aic_dai, "sysclk-set-rate", sys_rate);

    pcm_enable(codec, &playback_params);
    pcm_enable(aic_dai, &playback_params);
    pcm_start(codec);
    pcm_start(aic_dai);

    pcm_set_volume(codec, 80);

    // 使能功放引脚
    if (codec_gpio >= 0)
    gpio_direction_output(codec_gpio, 1);
}

static void disable_aic_and_codec(int codec_gpio)
{
    pcm_disable(aic_dai);
    pcm_disable(codec);

    // 关闭功放引脚
    if (codec_gpio >= 0)
    gpio_direction_output(codec_gpio, 0);
}

static void fg_thread_func(void *data)
{
    struct pcm_device *dev = pcm_get("fg-pcm");
    assert(dev);

    pcm_enable(dev, &playback_params);
    pcm_start(dev);

    struct pcm_adapter *adapter;
    /* 配置适配器，这里填写音频的相关参数
     */
    struct pcm_adapter_param adapter_param = {
        .channels = 1,
        .data_fmt = pcm_fmt_S16LE,
        .sample_rate = pcm_rate_48000,
    };

    /* 创建适配器 */
    adapter = pcm_adapter_create(dev, &adapter_param);
    int frame_size = pcm_data_sample_size(adapter_param.data_fmt) * adapter_param.channels;

    /* 解析wav格式音频,得到数据起始和帧数 */
    void *audio = wav_pcm_data((void *) wavData);
    int frames = wav_pcm_frames((void *) wavData);

    msleep(5000);

    printf("fg: playing fisrt time\n");
    pcm_adapter_write_frame(adapter, audio, frames);

    msleep(10000);

    printf("fg: playing again\n");
    pcm_adapter_write_frame(adapter, audio, frames);

    printf("fg: playing end\n");

    /* 释放适配器 */
    pcm_adapter_release(adapter);

    pcm_stop(dev);
    pcm_disable(dev);
}

static volatile int bg_is_end = 0;

static void bg_thread_func(void *data)
{
    struct pcm_device *dev = pcm_get("bg-pcm");
    assert(dev);

    pcm_enable(dev, &playback_params);
    pcm_start(dev);

    struct pcm_adapter *adapter;
    /* 配置适配器，这里填写音频的相关参数
     */
    struct pcm_adapter_param adapter_param = {
        .channels = 1,
        .data_fmt = pcm_fmt_S16LE,
        .sample_rate = pcm_rate_48000,
    };

    /* 创建适配器 */
    adapter = pcm_adapter_create(dev, &adapter_param);
    int frame_size = pcm_data_sample_size(adapter_param.data_fmt) * adapter_param.channels;

    printf("bg: playing\n");

    /* 这里引用的是pcm raw data,所以不用解析wav格式 */
    pcm_adapter_write_frame(adapter, (void *)pcmData, pcmSize/frame_size);

    printf("bg: playing end\n");

    /* 释放适配器 */
    pcm_adapter_release(adapter);

    pcm_stop(dev);
    pcm_disable(dev);

    bg_is_end = 1;
}

/*
 * 多路音频播放混合测试
 */
void pcm_mixer_server_test(void)
{
    printf("vendor init...\n");

    struct pcm_device *dai = pcm_get("aic0-playback");
    struct pcm_mixer_server *server = pcm_mixer_server_create(dai, playback_params.channels, 256);
    struct pcm_mixer_dev *dev0 = pcm_mixer_dev_create(server, "bg-pcm", 16*1024);
    struct pcm_mixer_dev *dev1 = pcm_mixer_dev_create(server, "fg-pcm", 16*1024);
    struct pcm_mixer_dev *dev2 = pcm_mixer_dev_create(server, "ctrl-pcm", 16*1024);

    enable_aic_and_codec("ctrl-pcm", "icodec-playback", 256*48000, gpio_pa_enable);

    thread_create("bg-thread", 4096, bg_thread_func, NULL);

    fg_thread_func(NULL);

    // 正常情况下,为了保持播放的实时性,没有必要关闭各路 pcm
    // 这样的话, 各路pcm 可以随时来播放

    if (0) {
        while (!bg_is_end);

        pcm_mixer_dev_delete(dev0);
        pcm_mixer_dev_delete(dev1);

        disable_aic_and_codec(gpio_pa_enable);
        pcm_mixer_dev_delete(dev2);
        pcm_mixer_server_delete(server);
    }
}
