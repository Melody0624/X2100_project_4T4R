#include <common.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <os.h>
#include <driver/pcm.h>
#include <driver/gpio.h>
#include <driver/pwm.h>
#include <driver/cache.h>
#include <driver/hrtimer.h>
#include <libsamplerate/samplerate.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PWM_AUDIO_BASE_FREQ         384000
#define PWM_AUDIO_UNIT_TIME         32

#define PWM_DUTY_MAX_COUNT          (0xFFFF)
#define PWM_DMA_UNIT_SAMPLES        48
#define PWM_DMA_DESC_MAX            256
#define RESAMPLE_BLOCK_FRAMES       1024

#define PWM_EQ_ENABLE               1
#define PWM_EQ_GAIN_LOW             0.7f
#define PWM_EQ_GAIN_HIGH            1.0f
#define PWM_EQ_CORNER_HZ            15000.0f

/* 消除pop音 */
#define TEST_MUTE_UP_MS             200
#define TEST_MUTE_DOWN_MS           1000

#define CALC_AUDIO_TIME_MS(count)   (count * 1000 / PWM_AUDIO_BASE_FREQ)

#define PWM_RATE_LIST \
    BIT(pcm_rate_5512) | \
    BIT(pcm_rate_8000) | \
    BIT(pcm_rate_11025) | \
    BIT(pcm_rate_12000) | \
    BIT(pcm_rate_16000) | \
    BIT(pcm_rate_22050) | \
    BIT(pcm_rate_24000) | \
    BIT(pcm_rate_32000) | \
    BIT(pcm_rate_44100) | \
    BIT(pcm_rate_48000) | \
    BIT(pcm_rate_64000) | \
    BIT(pcm_rate_88200) | \
    BIT(pcm_rate_96000) | \
    BIT(pcm_rate_176400) | \
    BIT(pcm_rate_192000) | \
    BIT(pcm_rate_384000)

struct pwm_loop_ctx {
    int pwm_id;
    struct pwm_data *buffer;
    unsigned long buffer_phys;
    unsigned int buffer_count;
    unsigned int write_pos;
    unsigned int dma_pos;
    unsigned int data_count;
    unsigned int pwm_full_num;
    unsigned int unit_samples;
    int running;
};

struct pwm_audio_state {
    struct pwm_loop_ctx loop;
    unsigned int in_rate;
    int volume;
    int mute;
    int amp_inited;
    int silence_count;
    int period_time;
    int mute_up_ms;
    int mute_down_ms;
    struct hrtimer timer;
};

static struct pwm_audio_state pwm_audio = {
    .loop = {
        .pwm_id = -1,
    },
    .volume = 100,
    .mute = 0,
    .amp_inited = 0,
};

static DEFINE_MUTEX(lock);
static DEFINE_MUTEX(playback_write_lock);

#if PWM_EQ_ENABLE
static short eq_buf[RESAMPLE_BLOCK_FRAMES];

static struct {
    float b0;
    float b1;
    float a1;
    float x1;
    float y1;
} g_eq_state;

static void pwm_eq_init(unsigned int sample_rate)
{
    float w0 = 2.0f * (float)M_PI * PWM_EQ_CORNER_HZ;
    float w1 = w0 * (PWM_EQ_GAIN_LOW / PWM_EQ_GAIN_HIGH);
    float K = 2.0f * (float)sample_rate;
    float b0 = K + w1;
    float b1 = w1 - K;
    float a0 = K + w0;
    float a1 = w0 - K;

    g_eq_state.b0 = b0 / a0;
    g_eq_state.b1 = b1 / a0;
    g_eq_state.a1 = a1 / a0;
    g_eq_state.x1 = 0.0f;
    g_eq_state.y1 = 0.0f;
}

static inline short pwm_eq_process_sample(short in)
{
    float y = g_eq_state.b0 * (float)in +
              g_eq_state.b1 * g_eq_state.x1 -
              g_eq_state.a1 * g_eq_state.y1;

    g_eq_state.x1 = (float)in;
    g_eq_state.y1 = y;

    if (y > 32767.0f)
        return 32767;
    if (y < -32768.0f)
        return -32768;
    return (short)y;
}

static void pwm_eq_process_buffer(short *buf, unsigned int frames)
{
    for (unsigned int i = 0; i < frames; i++)
        buf[i] = pwm_eq_process_sample(buf[i]);
}
#endif

static unsigned int sub_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + size - delta) % size;
}

static unsigned int add_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + delta) % size;
}

static void pwm_loop_fill_silence(struct pwm_loop_ctx *ctx, struct pwm_data *dst, unsigned int count)
{
    unsigned int i;
    unsigned int high = ctx->pwm_full_num / 2;
    unsigned int low = ctx->pwm_full_num - high;

    for (i = 0; i < count; i++) {
        dst[i].high = high;
        dst[i].low = low;
    }
}

static void pwm_loop_fill_data(struct pwm_loop_ctx *ctx, const short *samples,
                               unsigned int count, struct pwm_data *dst)
{
    unsigned int i;
    int volume = pwm_audio.mute ? 0 : pwm_audio.volume;

    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;

    for (i = 0; i < count; i++) {
        int sample = (int)samples[i] * volume / 100;
        int diff = sample * (int)ctx->pwm_full_num / PWM_DUTY_MAX_COUNT;
        unsigned int high = ctx->pwm_full_num / 2 + diff;

        if (high >= ctx->pwm_full_num)
            high = ctx->pwm_full_num - 1;
        if (high == 0)
            high = 1;

        dst[i].high = high;
        dst[i].low = ctx->pwm_full_num - high;
    }
}

static unsigned int pwm_loop_get_writable(struct pwm_loop_ctx *ctx)
{
    unsigned long addr;
    unsigned long buffer_end;
    unsigned int pos;
    unsigned int size;
    unsigned int buffer_count = ctx->buffer_count;

    if (!ctx->running)
        return 0;

    if (!buffer_count)
        return 0;

    addr = pwm_dma_read_src_addr(ctx->pwm_id);
    buffer_end = ctx->buffer_phys + buffer_count * sizeof(struct pwm_data);
    if (addr < ctx->buffer_phys || addr > buffer_end)
        return 0;

    if (addr == buffer_end)
        pos = 0;
    else
        pos = (addr - ctx->buffer_phys) / sizeof(struct pwm_data);

    size = sub_pos(buffer_count, pos, ctx->dma_pos);
    ctx->dma_pos = pos;

    if (size <= ctx->data_count)
        ctx->data_count -= size;
    else
        ctx->data_count = 0;

    if (ctx->data_count == 0) {
        unsigned int align_pos = ALIGN(pos, ctx->unit_samples);
        if (align_pos >= buffer_count)
            align_pos = 0;
        ctx->write_pos = add_pos(buffer_count, align_pos, ctx->unit_samples);
    }

    return sub_pos(buffer_count, ctx->dma_pos, ctx->write_pos);
}

static void pwm_loop_write_samples(struct pwm_loop_ctx *ctx, const short *samples, unsigned int count)
{
    if (!ctx->running)
        return;

    while (count) {
        unsigned int writable = pwm_loop_get_writable(ctx);
        unsigned int pos;
        unsigned int space;
        unsigned int n;

        if (!writable) {
            /* 等待四分之一的周期 */
            msleep(ctx->buffer_count * 1000 / PWM_AUDIO_BASE_FREQ / 4);
            continue;
        }

        if (writable > count)
            writable = count;

        pos = ctx->write_pos;
        space = ctx->buffer_count - pos;
        n = writable < space ? writable : space;

        pwm_loop_fill_data(ctx, samples, n, &ctx->buffer[pos]);
        flush_dcache_force((unsigned long)(&ctx->buffer[pos]), n * sizeof(struct pwm_data));

        ctx->write_pos = add_pos(ctx->buffer_count, pos, n);
        ctx->data_count += n;

        samples += n;
        count -= n;
    }
}

static void pwm_loop_write_silence_samples(struct pwm_loop_ctx *ctx, int count)
{
    if (count > ctx->buffer_count)
        count = ctx->buffer_count;

    while (count) {
        unsigned int writable = pwm_loop_get_writable(ctx);
        unsigned int pos;
        unsigned int space;
        unsigned int n;

        if (!writable) {
            /* 等待四分之一的周期 */
            msleep(ctx->buffer_count * 1000 / PWM_AUDIO_BASE_FREQ / 4);
            continue;
        }

        if (writable > count)
            writable = count;

        pos = ctx->write_pos;
        space = ctx->buffer_count - pos;
        n = writable < space ? writable : space;

        pwm_loop_fill_silence(ctx, &ctx->buffer[pos], n);
        flush_dcache_force((unsigned long)(&ctx->buffer[pos]), n * sizeof(struct pwm_data));

        ctx->write_pos = add_pos(ctx->buffer_count, pos, n);
        ctx->data_count += n;

        count -= n;
    }
}

static int pwm_loop_silence_played(struct pwm_loop_ctx *ctx)
{
    unsigned int old_dma_pos = ctx->dma_pos;
    unsigned int pos;

    pwm_loop_get_writable(ctx);
    pos = ctx->dma_pos;

    if (pos == old_dma_pos)
        return 0;

    if (pos > old_dma_pos) {
        unsigned int n = pos - old_dma_pos;
        pwm_loop_fill_silence(ctx, &ctx->buffer[old_dma_pos], n);
        flush_dcache_force((unsigned long)(&ctx->buffer[old_dma_pos]),
                           n * sizeof(struct pwm_data));

        return n;
    } else {
        unsigned int n1 = ctx->buffer_count - old_dma_pos;
        if (n1) {
            pwm_loop_fill_silence(ctx, &ctx->buffer[old_dma_pos], n1);
            flush_dcache_force((unsigned long)(&ctx->buffer[old_dma_pos]),
                               n1 * sizeof(struct pwm_data));
        }
        if (pos) {
            pwm_loop_fill_silence(ctx, &ctx->buffer[0], pos);
            flush_dcache_force((unsigned long)(&ctx->buffer[0]),
                               pos * sizeof(struct pwm_data));
        }

        return n1 + pos;
    }
}

static int pwm_loop_init(struct pwm_loop_ctx *ctx)
{
    struct pwm_dma_config dma_config;
    unsigned int buffer_count;
    int rate;

    ctx->pwm_id = pwm_request(CONFIG_PWM_AUDIO_GPIO, "pwm_audio");
    if (ctx->pwm_id < 0) {
        printf("%s: pwm request fail\n", __func__);
        return -1;
    }

    memset(&dma_config, 0, sizeof(dma_config));
    dma_config.idle_level = PWM_idle_low;
    dma_config.start_level = PWM_start_high;
    rate = pwm_dma_init(ctx->pwm_id, &dma_config);
    if (rate < 0) {
        printf("%s: pwm dma init fail\n", __func__);
        goto err_release;
    }

    if (rate % PWM_AUDIO_BASE_FREQ) {
        printf("%s: pwm%d not support base freq %d, rate %d\n",
               __func__, ctx->pwm_id, PWM_AUDIO_BASE_FREQ, rate);
        goto err_release;
    }

    ctx->pwm_full_num = rate / PWM_AUDIO_BASE_FREQ;
    ctx->unit_samples = PWM_DMA_UNIT_SAMPLES;

    buffer_count = PWM_AUDIO_BASE_FREQ * PWM_AUDIO_UNIT_TIME / 1000;
    buffer_count = ALIGN(buffer_count, ctx->unit_samples);
    if (buffer_count > PWM_DMA_UNIT_SAMPLES * PWM_DMA_DESC_MAX)
        buffer_count = PWM_DMA_UNIT_SAMPLES * PWM_DMA_DESC_MAX;

    ctx->buffer_count = buffer_count;
    ctx->buffer = cache_align_malloc(buffer_count * sizeof(struct pwm_data));
    assert(ctx->buffer);

    ctx->buffer_phys = virt_to_phys(ctx->buffer);
    ctx->write_pos = 0;
    ctx->dma_pos = 0;
    ctx->data_count = 0;
    ctx->running = 0;

    printf("pwm loop dma init: base_freq=%uHz buf_samples=%u\n",
           PWM_AUDIO_BASE_FREQ, ctx->buffer_count);
    return 0;

err_release:
    pwm_release(ctx->pwm_id);
    ctx->pwm_id = -1;
    return -1;
}

static void pwm_loop_deinit(struct pwm_loop_ctx *ctx)
{
    if (ctx->running)
        pwm_dma_disable_loop(ctx->pwm_id);

    if (ctx->pwm_id >= 0)
        pwm_release(ctx->pwm_id);

    if (ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }

    ctx->buffer_phys = 0;
    ctx->buffer_count = 0;
    ctx->write_pos = 0;
    ctx->dma_pos = 0;
    ctx->data_count = 0;
    ctx->pwm_full_num = 0;
    ctx->unit_samples = 0;
    ctx->running = 0;
    ctx->pwm_id = -1;
}

static int pwm_loop_start(struct pwm_loop_ctx *ctx)
{
    struct pwm_dma_data dma_data;

    if (ctx->running)
        return 0;

    ctx->write_pos = 0;
    ctx->dma_pos = 0;
    ctx->data_count = 0;

    pwm_loop_fill_silence(ctx, ctx->buffer, ctx->buffer_count);
    flush_dcache_force((unsigned long)ctx->buffer, ctx->buffer_count * sizeof(struct pwm_data));

    memset(&dma_data, 0, sizeof(dma_data));
    dma_data.data = ctx->buffer;
    dma_data.data_count = ctx->buffer_count;
    dma_data.dma_loop = 1;
    if (pwm_dma_update(ctx->pwm_id, &dma_data) < 0) {
        printf("%s: pwm dma update fail\n", __func__);
        return -1;
    }

    ctx->running = 1;
    return 0;
}

static void pwm_loop_stop(struct pwm_loop_ctx *ctx)
{
    if (!ctx->running)
        return;

    pwm_dma_disable_loop(ctx->pwm_id);
    ctx->running = 0;
}

static void pwm_audio_resample_write(struct pwm_loop_ctx *ctx, unsigned int in_rate,
                                     const short *data, unsigned int frames)
{
    unsigned int out_rate = PWM_AUDIO_BASE_FREQ;
    unsigned int offset = 0;

    if (in_rate == out_rate) {
#if PWM_EQ_ENABLE
        while (offset < frames) {
            unsigned int chunk = frames - offset;

            if (chunk > RESAMPLE_BLOCK_FRAMES)
                chunk = RESAMPLE_BLOCK_FRAMES;

            memcpy(eq_buf, data + offset, chunk * sizeof(short));
            pwm_eq_process_buffer(eq_buf, chunk);
            pwm_loop_write_samples(ctx, eq_buf, chunk);
            offset += chunk;
        }
#else
        pwm_loop_write_samples(ctx, data, frames);
#endif
        return;
    }

    double ratio = (double)out_rate / in_rate;
    int error = 0;
    SRC_STATE *src_state;
    float *in_float;
    float *out_float;
    short *out_s16;
    unsigned int out_frames_max;

    src_state = src_new(SRC_LINEAR, 1, &error);
    if (!src_state) {
        printf("src_new error %d\n", error);
        pwm_loop_write_samples(ctx, data, frames);
        return;
    }
    if (src_set_ratio(src_state, ratio)) {
        printf("src_set_ratio error %d\n", src_error(src_state));
        src_delete(src_state);
        pwm_loop_write_samples(ctx, data, frames);
        return;
    }

    out_frames_max = (unsigned int)(RESAMPLE_BLOCK_FRAMES * ratio + 4);
    in_float = malloc(RESAMPLE_BLOCK_FRAMES * sizeof(float));
    out_float = malloc(out_frames_max * sizeof(float));
    out_s16 = malloc(out_frames_max * sizeof(short));
    assert(in_float && out_float && out_s16);

    while (offset < frames) {
        unsigned int chunk = frames - offset;
        SRC_DATA src_data = {0};

        if (chunk > RESAMPLE_BLOCK_FRAMES)
            chunk = RESAMPLE_BLOCK_FRAMES;

        src_short_to_float_array(data + offset, in_float, chunk);

        src_data.data_in = in_float;
        src_data.data_out = out_float;
        src_data.input_frames = chunk;
        src_data.output_frames = out_frames_max;
        src_data.src_ratio = ratio;
        src_data.end_of_input = (offset + chunk) >= frames ? 1 : 0;

        if (src_process(src_state, &src_data)) {
            printf("src_process error %d\n", src_error(src_state));
            break;
        }

        if (src_data.output_frames_gen > 0) {
            src_float_to_short_array(out_float, out_s16, src_data.output_frames_gen);
#if PWM_EQ_ENABLE
            pwm_eq_process_buffer(out_s16, src_data.output_frames_gen);
#endif
            pwm_loop_write_samples(ctx, out_s16, src_data.output_frames_gen);
        }

        offset += chunk;
    }

    src_delete(src_state);
    free(in_float);
    free(out_float);
    free(out_s16);
}

static void pwm_audio_amp_prepare(void)
{
    if (!gpio_is_valid(CONFIG_PWM_AUDIO_GPIO_AMP_ENABLE))
        return;

    if (!pwm_audio.amp_inited) {
        if (!gpio_request(CONFIG_PWM_AUDIO_GPIO_AMP_ENABLE, "AMP_ENABLE")) {
            gpio_set_func(CONFIG_PWM_AUDIO_GPIO_AMP_ENABLE, GPIO_OUTPUT0 | GPIO_PULL_HIZ);
            pwm_audio.amp_inited = 1;
        }
    }
}

static void pwm_audio_amp_set(int enable)
{
    if (!gpio_is_valid(CONFIG_PWM_AUDIO_GPIO_AMP_ENABLE))
        return;

    if (!pwm_audio.amp_inited)
        return;

    gpio_set_value(CONFIG_PWM_AUDIO_GPIO_AMP_ENABLE, enable ? 1 : 0);
}

static void pwm_silence_timer_cb(struct hrtimer *timer)
{
    struct pwm_loop_ctx *ctx = &pwm_audio.loop;
    pwm_audio.silence_count += pwm_loop_silence_played(ctx);

    if (pwm_audio.silence_count < ctx->buffer_count)
        hrtimer_restart(timer, pwm_audio.period_time);
}

static int pwm_playback_pcm_write_frame(struct pcm_dev_data *dev,
                                        void *buf, int frame_count, unsigned int timeout_ms)
{
    struct pwm_loop_ctx *ctx = &pwm_audio.loop;

    if (!ctx->running)
        return -EINVAL;

    if (!buf || frame_count <= 0)
        return 0;

    mutex_lock(&playback_write_lock);

    hrtimer_cancel(&pwm_audio.timer);

    pwm_audio_resample_write(ctx, pwm_audio.in_rate, (const short *)buf, frame_count);

    pwm_audio.silence_count = 0;
    hrtimer_start(&pwm_audio.timer, pwm_audio.period_time);

    mutex_unlock(&playback_write_lock);

    return frame_count;
}

int pwm_playback_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    int ret = 0;

    mutex_lock(&lock);

    if (params->channels != 1 || params->pcm_data_fmt != pcm_fmt_S16LE) {
        printf("Only supports single-channel and S16LE format\n");
        ret = -EINVAL;
        goto unlock;
    }

    pwm_audio.in_rate = pcm_data_sample_rate(params->pcm_sample_rate);
    pwm_audio.mute_up_ms = TEST_MUTE_UP_MS;
    pwm_audio.mute_down_ms = TEST_MUTE_DOWN_MS;

    pwm_audio_amp_prepare();
    pwm_audio_amp_set(0);

    if (pwm_loop_init(&pwm_audio.loop) < 0) {
        ret = -EINVAL;
        goto unlock;
    }

unlock:
    mutex_unlock(&lock);
    return ret;
}

void pwm_playback_pcm_disable(struct pcm_dev_data *dev)
{
    mutex_lock(&playback_write_lock);
    mutex_lock(&lock);

    hrtimer_cancel(&pwm_audio.timer);

    pwm_audio_amp_set(0);
    pwm_loop_deinit(&pwm_audio.loop);

    mutex_unlock(&lock);
    mutex_unlock(&playback_write_lock);
}

static int pwm_playback_pcm_start(struct pcm_dev_data *dev)
{
    int ret = 0;

    mutex_lock(&lock);

#if PWM_EQ_ENABLE
    pwm_eq_init(PWM_AUDIO_BASE_FREQ);
#endif

    ret = pwm_loop_start(&pwm_audio.loop);
    if (ret < 0) {
        mutex_unlock(&lock);
        return ret;
    }

    if (!pwm_audio.mute) {
        msleep(pwm_audio.mute_up_ms);
        pwm_audio_amp_set(1);
    }

    pwm_audio.period_time = CALC_AUDIO_TIME_MS(pwm_audio.loop.buffer_count) * 1000 / 4;
    hrtimer_init(&pwm_audio.timer, pwm_silence_timer_cb);

    mutex_unlock(&lock);
    return ret;
}

static void pwm_playback_pcm_stop(struct pcm_dev_data *dev)
{
    struct pwm_loop_ctx *ctx = &pwm_audio.loop;

    mutex_lock(&playback_write_lock);
    mutex_lock(&lock);

    hrtimer_cancel(&pwm_audio.timer);

    pwm_loop_write_silence_samples(ctx, ctx->buffer_count);

    pwm_audio_amp_set(0);
    msleep(pwm_audio.mute_down_ms);

    pwm_loop_stop(ctx);

    mutex_unlock(&lock);
    mutex_unlock(&playback_write_lock);
}

int pwm_playback_pcm_set_mute(struct pcm_dev_data *dev, int mute)
{
    mutex_lock(&lock);

    pwm_audio.mute = !!mute;
    if (pwm_audio.mute) {
        pwm_audio_amp_set(0);
    } else if (pwm_audio.loop.running) {
        msleep(pwm_audio.mute_up_ms);
        pwm_audio_amp_set(1);
    }

    mutex_unlock(&lock);

    return 0;
}

int pwm_playback_pcm_set_volume(struct pcm_dev_data *dev, int val)
{
    if (val < 0)
        val = 0;
    if (val > 100)
        val = 100;

    mutex_lock(&lock);

    pwm_audio.volume = val;

    mutex_unlock(&lock);

    return 0;
}

int pwm_playback_pcm_get_volume(struct pcm_dev_data *dev)
{
    int val;

    mutex_lock(&lock);

    val = pwm_audio.volume;

    mutex_unlock(&lock);

    return val;
}

static int pwm_playback_pcm_private_ctrl(struct pcm_dev_data *dev,
                                    const char *ctrl_id, unsigned long value)
{
    int ret = 0;

    mutex_lock(&lock);

    if (!strcmp(ctrl_id, "mute_up_ms"))
        pwm_audio.mute_up_ms = value;
    else if (!strcmp(ctrl_id, "mute_down_ms"))
        pwm_audio.mute_down_ms = value;
    else
        ret = -EINVAL;

    mutex_unlock(&lock);

    return ret;
}

static struct pcm_dev_data pwm_playback_device = {
    .name = "pwm-playback",
    .stream_type = pcm_stream_playback,
    .pcm_interface_list = 0,
    .channels_list = BIT(1),
    .pcm_data_fmt_list = BIT(pcm_fmt_S16LE),
    .pcm_sample_rate_list = PWM_RATE_LIST,
    .pcm_write_frame = pwm_playback_pcm_write_frame,
    .pcm_enable = pwm_playback_pcm_enable,
    .pcm_disable = pwm_playback_pcm_disable,
    .pcm_set_volume = pwm_playback_pcm_set_volume,
    .pcm_get_volume = pwm_playback_pcm_get_volume,
    .pcm_set_mute = pwm_playback_pcm_set_mute,
    .pcm_start = pwm_playback_pcm_start,
    .pcm_stop = pwm_playback_pcm_stop,
    .pcm_private_ctrl = pwm_playback_pcm_private_ctrl,
    .drv_data = &pwm_audio,
};

void pwm_audio_init(void)
{
    pcm_register(&pwm_playback_device);
}
