#include <driver/pcm.h>
#include <include_bin.h>
#include <driver/gpio.h>
#include <driver/pcm_adapter.h>

#include <wav_utils.h>

INCBIN(audio, "example/resource/audio_pcm.aiff");

static struct pcm_params playback_params = {
    .channels = 1,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_16000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

static int gpio_pa_enable = GPIO_PB(9);
void pcm_test(void)
{
    struct pcm_adapter *adapter;
    /* 配置适配器，这里填写音频的相关参数
     */
    struct pcm_adapter_param adapter_param = {
        .channels = 2,
        .data_fmt = pcm_fmt_S16LE,
        .sample_rate = pcm_rate_16000,
    };

    int frame_size = pcm_data_sample_size(adapter_param.data_fmt) * adapter_param.channels;

    struct pcm_device *dai = pcm_get("aic0-playback");
    struct pcm_device *codec = pcm_get("icodec-playback");

    pcm_private_ctrl(dai, "sysclk-set-rate", 256*16000);

    /* 创建适配器 */
    adapter = pcm_adapter_create(dai, &adapter_param);

    pcm_enable(codec, &playback_params);
    pcm_enable(dai, &playback_params);

    pcm_start(codec);
    pcm_start(dai);

    pcm_set_volume(codec, 80);

    // 使能功放引脚
    if (gpio_pa_enable >= 0)
    gpio_direction_output(gpio_pa_enable, 1);

    /* 解析wav格式音频,得到数据起始和帧数 */
    void *audio = wav_pcm_data((void *) audioData);
    int frames = wav_pcm_frames((void *) audioData);

    pcm_adapter_write_frame(adapter, audio, frames);

    printf("------------------------playback over!\n");
    pcm_disable(dai);
    pcm_disable(codec);

    /* 释放适配器 */
    pcm_adapter_release(adapter);

    // 关闭功放引脚
    if (gpio_pa_enable >= 0)
    gpio_direction_output(gpio_pa_enable, 0);
}
