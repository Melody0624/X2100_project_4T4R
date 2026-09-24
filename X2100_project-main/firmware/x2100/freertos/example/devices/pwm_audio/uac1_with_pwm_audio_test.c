#include <common.h>
#include <usb/gadget_uac1.h>
#include <errno.h>
#include <os.h>
#include <math.h>
#include <driver/gpio.h>
#include <driver/pwm.h>
#include <driver/cache.h>
#include <libsamplerate/samplerate.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define PWM_AUDIO_BASE_FREQ     384000
#define PWM_AUDIO_TIME_MS       16
#define PKT_SIZE_MS             10
#define PWM_DUTY_MAX_COUNT      (0xFFFF)
#define PWM_DMA_UNIT_SAMPLES    48
#define PWM_DMA_DESC_MAX        128
#define RESAMPLE_CONVERTER      SRC_LINEAR

#define PWM_AUDIO_GPIO      GPIO_PC(25)
#define GPIO_AMP_ENABLE      GPIO_PC(26)

// Enable/disable simple EQ compensation.
#define PWM_EQ_ENABLE          1

#define PWM_EQ_GAIN_LOW        0.7f
#define PWM_EQ_GAIN_HIGH       1.0f
#define PWM_EQ_CORNER_HZ       15000.0f

/* 单声道 16bit */
#define SAMPLE_SIZE     (16 / 8)

/* 消除pop音 */
#define TEST_MUTE_UP_MS       200
#define TEST_MUTE_DOWN_MS     1000

static const struct gadget_id uac1_id = {
    .vendor_id = 0x1d6b,
    .product_id = 0x0101
};

static unsigned int p_srates[] = {
    8000, 16000, 48000, 96000, 192000
};

static void uac1_connect_callback(int connect)
{
    printf("%s %d\n", __func__, connect);
}

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
static unsigned int sample_rate;
static volatile int playback_ok;
static thread_waiter_t playback_waiter;
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

static int uac1_start_playback_cb(u32 rate)
{
    /* 设置采样率 */
    sample_rate = rate;

    playback_ok = 1;
    thread_waiter_wakeup(&playback_waiter);

    printf("%s: rate %d\n", __func__, rate);

    return 0;
}

static void uac1_stop_playback_cb(void)
{
    playback_ok = 0;

    printf("%s\n", __func__);
}

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

static void pwm_loop_write_silence_samples(struct pwm_loop_ctx *ctx)
{
    unsigned int count = ctx->buffer_count;

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

        pwm_loop_fill_silence(ctx, &ctx->buffer[pos], n);
        flush_dcache_force((unsigned long)(&ctx->buffer[pos]), n * sizeof(struct pwm_data));

        ctx->write_pos = add_pos(ctx->buffer_count, pos, n);
        ctx->data_count += n;

        count -= n;
    }
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

    buffer_count = PWM_AUDIO_BASE_FREQ * PWM_AUDIO_TIME_MS / 1000;
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

void pwm_audio_playback_thread(void *data)
{
    int len;
    u32 in_len;
    u32 rate;
    u32 out_rate;
    u32 max_in_len;
    u32 in_frames;
    u32 out_frames_max;
    u8 *input_buffer;
    float *in_float;
    float *out_float;
    short *out_s16;
    SRC_STATE *src_state = NULL;
    double ratio = 1.0;

    /* max rate buf size */
    out_rate = PWM_AUDIO_BASE_FREQ;
    max_in_len = out_rate * SAMPLE_SIZE / 1000 * PKT_SIZE_MS;
    out_frames_max = out_rate / 1000 * PKT_SIZE_MS;
    input_buffer = malloc(max_in_len);
    in_float = malloc((max_in_len / SAMPLE_SIZE) * sizeof(float));
    out_float = malloc(out_frames_max * sizeof(float));
    out_s16 = malloc(out_frames_max * sizeof(short));
    assert(input_buffer && in_float && out_float && out_s16);

    while (1) {
        thread_waiter_wait(&playback_waiter);
        if (!playback_ok)
            continue;

        printf("playback start\n");

        rate = sample_rate;
        in_len = rate * SAMPLE_SIZE / 1000 * PKT_SIZE_MS;
        ratio = (double)out_rate / rate;

#if PWM_EQ_ENABLE
        if (!g_eq_initted)
            pwm_eq_init(out_rate);
#endif

        if (src_state) {
            src_delete(src_state);
            src_state = NULL;
        }
        if (rate != out_rate) {
            int error = 0;
            src_state = src_new(RESAMPLE_CONVERTER, 1, &error);
            if (!src_state) {
                printf("src_new error %d\n", error);
            } else if (src_set_ratio(src_state, ratio)) {
                printf("src_set_ratio error %d\n", src_error(src_state));
                src_delete(src_state);
                src_state = NULL;
            }
        }

        if (pwm_loop_start(&pwm_ctx) < 0) {
            playback_ok = 0;
            continue;
        }

        // 使能功放引脚
        if (gpio_is_valid(GPIO_AMP_ENABLE)) {
            msleep(TEST_MUTE_UP_MS);
            gpio_set_value(GPIO_AMP_ENABLE, 1);
        }

        while (playback_ok) {
            len = gadget_uac1_playback_read(input_buffer, in_len);
            if (len == 0)
                msleep(PKT_SIZE_MS);
            else if (len > 0) {
                in_frames = len / SAMPLE_SIZE;
                if (!in_frames)
                    continue;
                if (rate == out_rate || !src_state) {
#if PWM_EQ_ENABLE
                    pwm_eq_process_buffer((short *)input_buffer, in_frames);
#endif
                    pwm_loop_write_samples(&pwm_ctx, (const short *)input_buffer, in_frames);
                } else {
                    SRC_DATA src_data;
                    src_short_to_float_array((const short *)input_buffer, in_float, in_frames);
                    memset(&src_data, 0, sizeof(src_data));
                    src_data.data_in = in_float;
                    src_data.data_out = out_float;
                    src_data.input_frames = in_frames;
                    src_data.output_frames = out_frames_max;
                    src_data.src_ratio = ratio;
                    src_data.end_of_input = 0;
                    if (src_process(src_state, &src_data)) {
                        printf("src_process error %d\n", src_error(src_state));
                        continue;
                    }
                    if (src_data.output_frames_gen > 0) {
                        src_float_to_short_array(out_float, out_s16, src_data.output_frames_gen);
#if PWM_EQ_ENABLE
                        pwm_eq_process_buffer(out_s16, src_data.output_frames_gen);
#endif
                        pwm_loop_write_samples(&pwm_ctx, out_s16, src_data.output_frames_gen);
                    }
                }
            }
            else
                printf("gadget_uac1_playback_read error %d\n", len);
        }

        pwm_loop_write_silence_samples(&pwm_ctx);

        // 关闭功放引脚
        if (gpio_is_valid(GPIO_AMP_ENABLE)) {
            gpio_set_value(GPIO_AMP_ENABLE, 0);
            msleep(TEST_MUTE_DOWN_MS);
        }

        pwm_loop_stop(&pwm_ctx);

        if (src_state) {
            src_delete(src_state);
            src_state = NULL;
        }

        printf("playback stop\n");
    }

}

struct uac1_params uac1_param = {
    /* playback */
    .p_chmask = UAC_CH_LAYOUT_MONO,
    .p_ssize = SAMPLE_SIZE,
    .p_srate_num = ARRAY_SIZE(p_srates),
    .p_srates = p_srates,
    .p_srate = 48000,
    .start_playback_callback = uac1_start_playback_cb,
    .stop_playback_callback = uac1_stop_playback_cb,

    .buffer_size_ms = PKT_SIZE_MS * 2,
    .connect_cb = uac1_connect_callback,
};

int uac1_pwm_audio_test(void)
{
    if (gpio_is_valid(GPIO_AMP_ENABLE)) {
        assert(!gpio_request(GPIO_AMP_ENABLE, "AMP_ENABLE"));
        gpio_set_func(GPIO_AMP_ENABLE , GPIO_OUTPUT0 | GPIO_PULL_HIZ);
    }

    assert(!pwm_loop_init(&pwm_ctx));

    thread_waiter_init(&playback_waiter);
    thread_create("pwm_audio_playback_thread", 4096, pwm_audio_playback_thread, NULL);
    gadget_uac1_init(&uac1_id, &uac1_param);
    return 0;
}
