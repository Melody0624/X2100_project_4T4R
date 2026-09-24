

#include <driver/gpio.h>
#include <driver/pcm.h>

#include <include_bin.h>
#include <wav_utils.h>

INCBIN(audio, "example/resource/audio_pcm.aiff");

static struct pcm_params playback_params = {
    .channels = 2,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_16000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

static struct pcm_params capture_params = {
    .channels = 2,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_16000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

static volatile int playback_ok = 0;
static int gpio_pa_enable = GPIO_PB(9);

void write_thread_func(void *data)
{
    struct pcm_device *dai = pcm_get("aic-playback");
    struct pcm_device *codec = pcm_get("icodec-playback");

    /* 注意！
     * 如果为外部codec，则必须在使能aic之前使能。
     * 不是外部则无此规定。
     */
    pcm_enable(codec, &playback_params);

    pcm_enable(dai, &playback_params);

    pcm_start(codec);

    pcm_start(dai);

    // 使能功放引脚
    if (gpio_pa_enable >= 0)
    gpio_direction_output(gpio_pa_enable, 1);

    /* 解析wav格式音频,得到数据起始和帧数 */
    void *audio = wav_pcm_data((void *) audioData);
    int frames = wav_pcm_frames((void *) audioData);

    pcm_write_frame(dai, (void *)audio, frames);

    pcm_disable(dai);

    pcm_disable(codec);

    // 关闭功放引脚
    if (gpio_pa_enable >= 0)
    gpio_direction_output(gpio_pa_enable, 0);

    playback_ok = 1;
}

static unsigned int buffer[10 * 16 * 1024];

void pcm_test(void)
{
    struct pcm_device *c_dai = pcm_get("aic-capture");
    struct pcm_device *c_codec = pcm_get("icodec-capture");

    if (gpio_pa_enable >= 0)
        gpio_request(gpio_pa_enable, "pa_enable");

    pcm_private_ctrl(c_dai, "sysclk-set-rate", 16000 * 768);
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
     * 开启播放线程
     * 内部codec实际上只能播放左声道的数据
     */
    thread_create("write_thread", 4096, write_thread_func, NULL);

    /*
     * 开始录音
     * 内部codec实际上只能录制一个通道的数据在左声道
     */
    pcm_enable(c_dai, &capture_params);
    pcm_enable(c_codec, &capture_params);

    pcm_start(c_codec);
    pcm_start(c_dai);

    pcm_read_frame(c_dai, buffer, sizeof(buffer)/pcm_frame_size(&capture_params));

    pcm_disable(c_codec);
    pcm_disable(c_dai);

    /*
     * 等待播放线程结束
     */
    while (!playback_ok)
        msleep(1);

    /*
     * 播放录音数据
     * 此时录制的音量可能比较小
     */
    struct pcm_device *dai = pcm_get("aic-playback");
    struct pcm_device *codec = pcm_get("icodec-playback");

    pcm_enable(dai, &playback_params);

    pcm_enable(codec, &playback_params);

    // 使能功放引脚
    if (gpio_pa_enable >= 0)
    gpio_direction_output(gpio_pa_enable, 1);

    pcm_start(codec);

    pcm_start(dai);

    pcm_write_frame(dai, (void *)buffer, sizeof(buffer) / pcm_frame_size(&playback_params));

    pcm_disable(dai);

    pcm_disable(codec);

    // 关闭功放引脚
    if (gpio_pa_enable >= 0)
    gpio_direction_output(gpio_pa_enable, 0);

    /*
     * 打印录音数据
     */
    dump_mem32(buffer, sizeof(buffer), 8);
}

