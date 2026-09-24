#include <stdio.h>
#include <assert.h>
#include <driver/pwm.h>
#include <driver/cache.h>
#include <driver/gpio.h>
#include <os.h>
#include <libsamplerate/samplerate.h>

#include "pwm_audio.h"

#define PWM_DUTY_MAX_COUNT      (0xFFFF)
#define PWM_AUDIO_BASE_FREQ     96000
#define ONE_RESAMPLE_SIZE       1024

struct pwm_audio_dev {
    int pwm_id;
    int exit_flag;
    unsigned int pwm_full_num;
    unsigned int buf_size;
    unsigned int buf_offset;
    unsigned int write_pos;
    unsigned int read_pos;
    thread_waiter_t write_waiter;
    thread_waiter_t read_waiter;
    struct mutex lock;
    unsigned int base_freq;
    unsigned int unit_time;
    unsigned int buf_count;
    struct pwm_dma_data *dma_data;
};

static void pwm_audio_thread(void *val)
{
    int i;
    struct pwm_audio_dev *dev = val;
    struct pwm_dma_data *dma_data_p;

    while (!dev->exit_flag)
    {
        while ((dev->write_pos - dev->read_pos) == 0) {
            thread_waiter_wait(&dev->read_waiter);

            if (dev->exit_flag)
                goto exit_pwm_audio;
        }

        dma_data_p = &dev->dma_data[dev->read_pos % dev->buf_count];
        pwm_dma_update(dev->pwm_id, dma_data_p);
        dev->read_pos++;
        thread_waiter_wakeup(&dev->write_waiter);
    }

exit_pwm_audio:
    dev->read_pos = dev->write_pos;
    thread_waiter_wakeup(&dev->write_waiter);
    mutex_lock(&dev->lock);
    pwm_release(dev->pwm_id);

    for (i = 0; i < dev->buf_count; i++)
        free(dev->dma_data[i].data);

    free(dev->dma_data);
    free(dev);
}

void pwm_audio_data_flush(struct pwm_audio_dev *dev)
{
    int i;
    unsigned short low;
    unsigned short high;
    struct pwm_data *data;

    assert(dev);

    mutex_lock(&dev->lock);

    if (dev->buf_offset) {
        data = dev->dma_data[dev->write_pos % dev->buf_count].data;
        high = dev->pwm_full_num / 2;
        low = dev->pwm_full_num - high;
        for(i = dev->buf_offset; i < dev->buf_size; i++) {
            data[i].high = high;
            data[i].low = low;
        }

        dev->buf_offset = 0;
        dev->write_pos++;
        thread_waiter_wakeup(&dev->read_waiter);
    }
    mutex_unlock(&dev->lock);
}

static int pwm_audio_mono_96k_s16le_write(struct pwm_audio_dev *dev, const short *data, unsigned int data_count)
{
    int i;
    short diff;
    struct pwm_data *pwm_data;

    assert(dev);

    mutex_lock(&dev->lock);

    while (data_count)
    {
        if (dev->buf_offset == 0) {
            while ((dev->buf_count - (dev->write_pos - dev->read_pos)) == 0)
                thread_waiter_wait(&dev->write_waiter);
        }

        pwm_data = dev->dma_data[dev->write_pos % dev->buf_count].data;

        for (i = dev->buf_offset; i < dev->buf_size; i++) {
            if (!data_count)
                break;

            diff = data[0] * dev->pwm_full_num / PWM_DUTY_MAX_COUNT;
            pwm_data[i].high = dev->pwm_full_num / 2 + diff;
            if (pwm_data[i].high >= dev->pwm_full_num)
                pwm_data[i].high = dev->pwm_full_num - 1;
            if (pwm_data[i].high == 0)
                pwm_data[i].high = 1;
            pwm_data[i].low = dev->pwm_full_num - pwm_data[i].high;

            data++;
            data_count--;
        }

        if (i < dev->buf_size) {
            dev->buf_offset = i;
        } else {
            dev->buf_offset = 0;
            dev->write_pos++;
            thread_waiter_wakeup(&dev->read_waiter);
        }
    }
    mutex_unlock(&dev->lock);
    return 0;
}

int pwm_audio_mono_s16le_write(struct pwm_audio_dev *dev, unsigned int sampling_rate, const short *data, unsigned int data_count)
{
    if (sampling_rate == dev->base_freq) {
        pwm_audio_mono_96k_s16le_write(dev, data, data_count);
        return 0;
    }
    double old_sample_rate = sampling_rate; // 原始采样率
    double new_sample_rate = dev->base_freq; // 目标采样率

    int error = 0;
    SRC_STATE* src_state = src_new(SRC_ZERO_ORDER_HOLD, 1, &error); // 单声道

    double ratio = new_sample_rate / old_sample_rate;
    src_set_ratio(src_state, ratio);

    const unsigned char* audio_data = data;
    size_t audio_data_size = data_count * 2;
    int block_size = ONE_RESAMPLE_SIZE;
    size_t offset = 0;

    float* output_buffer = (float*)malloc(block_size * sizeof(float) * ratio);

    while (offset < audio_data_size) {
        size_t remaining_bytes = audio_data_size - offset;
        size_t bytes_to_process = remaining_bytes < block_size * sizeof(float) ? remaining_bytes : block_size * sizeof(float);

        SRC_DATA src_data;
        src_data.data_in = &audio_data[offset];
        src_data.data_out = output_buffer;
        src_data.input_frames = bytes_to_process / sizeof(float);
        src_data.output_frames = src_data.input_frames * ratio;
        src_data.src_ratio = ratio;
        src_data.end_of_input = 1;
        int result = src_process(src_state, &src_data);

        pwm_audio_mono_96k_s16le_write(dev, (const short *)output_buffer, src_data.output_frames_gen * 2);
        offset += bytes_to_process;
    }

    src_delete(src_state);
    free(output_buffer);
    return 0;
}

struct pwm_audio_dev *pwm_audio_init(int pwm_gpio, unsigned int base_freq, unsigned int unit_time, unsigned int buf_count)
{
    int i;
    int rate;
    char gpio_str[10];
    struct pwm_audio_dev *dev;
    struct pwm_dma_data *dma_data_p;
    struct pwm_dma_config dma_config;
    memset(&dma_config, 0, sizeof(struct pwm_dma_config));

    dev = malloc(sizeof(struct pwm_audio_dev));
    assert(dev);

    dev->exit_flag = 0;
    dev->buf_offset = 0;
    dev->read_pos = 0;
    dev->write_pos = 0;
    thread_waiter_init(&dev->write_waiter);
    thread_waiter_init(&dev->read_waiter);
    mutex_init(&dev->lock);

    dev->base_freq = base_freq;
    dev->unit_time = unit_time;
    dev->buf_count = buf_count;

    dev->buf_size = base_freq * unit_time / 1000;

    dev->pwm_id = pwm_request(pwm_gpio, "pwm_audio");
    if (dev->pwm_id < 0) {
        printf("%s: pwm request fail. gpio %s\n", __func__, gpio_to_str(pwm_gpio, gpio_str, sizeof(gpio_str)));
        goto err_free_dev;
    }

    dma_config.idle_level = PWM_idle_low;
    dma_config.start_level = PWM_start_high;
    rate = pwm_dma_init(dev->pwm_id, &dma_config);
    if (rate < 0) {
        printf("%s: pwm%d dma init fail\n", __func__, dev->pwm_id);
        goto err_pwm_release;
    }

    if (rate % base_freq) {
        printf("%s: pwm%d not support base freq %d, rate %d\n", __func__, dev->pwm_id, base_freq, rate);
        goto err_pwm_release;
    }

    dev->pwm_full_num = rate / base_freq;

    dev->dma_data = malloc(buf_count * sizeof(struct pwm_dma_data));
    assert(dev->dma_data);

    for (i = 0; i < buf_count; i++) {
        dma_data_p = &dev->dma_data[i];
        dma_data_p->data_count = dev->buf_size;
        dma_data_p->dma_loop = 0;
        dma_data_p->data = cache_align_malloc(dev->buf_size * sizeof(struct pwm_data));
        assert(dma_data_p->data);
    }

    thread_create("pwm_audio_thread", 4096, pwm_audio_thread, dev);

    return dev;

err_pwm_release:
    pwm_release(dev->pwm_id);
err_free_dev:
    free(dev);
    return NULL;
}

void pwm_audio_exit(struct pwm_audio_dev *dev)
{
    assert(dev);

    dev->exit_flag = 1;
    thread_waiter_wakeup(&dev->read_waiter);
}
