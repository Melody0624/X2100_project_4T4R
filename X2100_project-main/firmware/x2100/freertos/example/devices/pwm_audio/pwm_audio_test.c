#include <common.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <os.h>
#include <driver/gpio.h>
#include <driver/pwm.h>
#include <driver/cache.h>
#include <include_bin.h>
#include <libsamplerate/samplerate.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
    数据生成命令
    ffmpeg -i 1.mp3 -ar 8000 -ac 1 -f S16le mono_s16le_8000.pcm
    ffmpeg -i 1.mp3 -ar 48000 -ac 1 -f S16le mono_s16le_48000.pcm
    由于pcm文件比较大，实际场景请用libmad实时解码。
 */
// INCBIN(music1, "example/resource/mono_s16le_8000.pcm");
// INCBIN(music2, "example/resource/mono_s16le_48000.pcm");
INCBIN(music3, "example/resource/mono_s16le_384000.pcm");

#define PWM_AUDIO_BASE_FREQ     384000
#define PWM_AUDIO_UNIT_TIME     16

#define PWM_AUDIO_GPIO      GPIO_PC(25)
#define GPIO_AMP_ENABLE      GPIO_PC(26)
#define PWM_DUTY_MAX_COUNT      (0xFFFF)
#define PWM_DMA_UNIT_SAMPLES    48
#define PWM_DMA_DESC_MAX        128
#define RESAMPLE_BLOCK_FRAMES  1024

// Enable/disable simple EQ compensation.
#define PWM_EQ_ENABLE          1

#define PWM_EQ_GAIN_LOW        0.7f
#define PWM_EQ_GAIN_HIGH       1.0f
#define PWM_EQ_CORNER_HZ       15000.0f

/* 消除pop音 */
#define TEST_MUTE_UP_MS       200
#define TEST_MUTE_DOWN_MS     1000

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

static struct pwm_loop_ctx pwm_ctx;
static volatile int pwm_test_stop;
static volatile int g_volume = 100;

#if PWM_EQ_ENABLE
static int g_eq_initted;
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
    g_eq_initted = 1;
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
    int volume = g_volume;

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

static void pwm_loop_write_silence_samples(struct pwm_loop_ctx *ctx)
{
    unsigned int count = ctx->buffer_count;

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

static int pwm_loop_init(struct pwm_loop_ctx *ctx)
{
    struct pwm_dma_config dma_config;
    unsigned int buffer_count;
    int rate;

    ctx->pwm_id = pwm_request(PWM_AUDIO_GPIO, "pwm_audio");
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
    double ratio = (double)out_rate / in_rate;
    int error = 0;
    SRC_STATE *src_state;
    float *in_float;
    float *out_float;
    short *out_s16;
    unsigned int out_frames_max;
    unsigned int offset = 0;
#if PWM_EQ_ENABLE
    static short eq_buf[RESAMPLE_BLOCK_FRAMES];

    if (!g_eq_initted)
        pwm_eq_init(out_rate);
#endif

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

void pwm_audio_test(void)
{
    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        assert(!gpio_request(GPIO_AMP_ENABLE, "AMP_ENABLE"));
        gpio_set_func(GPIO_AMP_ENABLE , GPIO_OUTPUT0 | GPIO_PULL_HIZ);
    }

    assert(!pwm_loop_init(&pwm_ctx));

    if (pwm_loop_start(&pwm_ctx) < 0)
        return;

    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        msleep(TEST_MUTE_UP_MS);
        gpio_set_value(GPIO_AMP_ENABLE, 1);
    }

    while (g_volume)
    {
        // pwm_audio_resample_write(&pwm_ctx, 8000, (const short *)music1Data, music1Size / 2);
        // pwm_audio_resample_write(&pwm_ctx, 48000, (const short *)music2Data, music2Size / 2);
        pwm_audio_resample_write(&pwm_ctx, 384000, (const short *)music3Data, music3Size / 2);
        g_volume--;
    }

    pwm_loop_write_silence_samples(&pwm_ctx);

    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        gpio_set_value(GPIO_AMP_ENABLE, 0);
        msleep(TEST_MUTE_DOWN_MS);
    }

    pwm_loop_stop(&pwm_ctx);
}
