#include <driver/hrtimer.h>
#include <driver/cache.h>
#include <driver/pcm.h>
#include <os.h>
#include <errno.h>
#include <stdlib.h>
#include <malloc.h>
#include <driver/dma.h>
#include <common.h>
#include "audio_dma.h"
#include "aic.h"

struct aic_rx_data {
    int is_init;
    int is_start;
    void *buffer;

    unsigned int dma_pos;
    unsigned int read_pos;
    unsigned int unit_size;
    unsigned int data_size;
    unsigned int frame_size;
    unsigned int buffer_size;
    unsigned int period_time;
    unsigned int old_buffer_size;

    struct hrtimer timer;
    struct mutex lock;
    thread_waiter_t waiter;

    struct audio_dma_desc *dma_desc;
    enum audio_dev_id aic_dev;
    enum audio_dev_id dma_dev;
};

static struct aic_rx_data aic_rx_datas[AIC_NUMS];

static unsigned int sub_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + size - delta) % size;
}

static unsigned int add_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + delta) % size;
}

static void aic_init_rx_dma(struct aic_params *param, struct aic_rx_data *aic_rx)
{
    if (aic_rx->dma_desc)
        free(aic_rx->dma_desc);
    aic_rx->dma_desc = cache_align_malloc(sizeof(*aic_rx->dma_desc));

    audio_dma_desc_init(aic_rx->dma_desc, aic_rx->buffer, aic_rx->buffer_size, aic_rx->unit_size, aic_rx->dma_desc);
    audio_connect_dev(aic_rx->aic_dev, aic_rx->dma_dev);
    audio_dma_set_callback(aic_rx->dma_dev, NULL, NULL);
    audio_dma_config(aic_rx->dma_dev, param->channels, param->data_bits, aic_rx->unit_size);
}

static void aic_rx_timer_cb(struct hrtimer *timer);

static void aic_init_rx_buffer(struct aic_params *param, struct aic_rx_data *aic_rx)
{
    unsigned int buffer_time, period_time;
    unsigned int buffer_size, period_size;
    unsigned int sample_rate = param->sample_rate;

    buffer_time = param->buffer_time_ms >= MIN_BUFFER_TIME_MS ? param->buffer_time_ms : DEFAULT_BUFFER_TIME_MS;
    buffer_size = (uint64_t)buffer_time * (sample_rate * aic_rx->frame_size) / 1000;

    buffer_size = ALIGN(buffer_size, aic_rx->unit_size);

    if (buffer_size < aic_rx->unit_size * 8) {
        buffer_size = aic_rx->unit_size * 8;
        buffer_time = buffer_size * 1000 / (sample_rate * aic_rx->frame_size);
    }

    period_time = param->period_time_ms ? param->period_time_ms : DEFAULT_BUFFER_PERIO_MS;
    period_size = (uint64_t)period_time * (sample_rate * aic_rx->frame_size) / 1000;

    if (buffer_size / period_size < MIN_PERIODS) {
        period_time = buffer_time / MIN_PERIODS;
        period_size = (uint64_t)period_time * (sample_rate * aic_rx->frame_size) / 1000;
        assert(period_time);
    }

    aic_rx->period_time = period_time * 1000;
    aic_rx->buffer_size = buffer_size;

    if (!aic_rx->buffer || aic_rx->old_buffer_size < buffer_size) {
        if (aic_rx->buffer)
            free(aic_rx->buffer);
        aic_rx->buffer = cache_align_malloc(buffer_size);
        assert(aic_rx->buffer);
        aic_rx->old_buffer_size = buffer_size;
    }

    if (!aic_rx->is_init) {
        hrtimer_init(&aic_rx->timer, aic_rx_timer_cb);
        thread_waiter_init(&aic_rx->waiter);
        aic_rx->is_init = 1;
    }
}

void aic_start_capture_dma(int id)
{
    struct aic_rx_data *aic_rx = &aic_rx_datas[id];

    os_enter_critical();

    assert(!aic_rx->is_start);

    aic_rx->dma_pos = 0;
    aic_rx->read_pos = 0;
    aic_rx->data_size = 0;

    audio_dma_start(aic_rx->dma_dev, aic_rx->dma_desc);

    hrtimer_start(&aic_rx->timer, aic_rx->period_time);

    aic_rx->is_start = 1;

    os_exit_critical();
}

void aic_stop_capture_dma(int id)
{
    struct aic_rx_data *aic_rx = &aic_rx_datas[id];

    hrtimer_cancel(&aic_rx->timer);
    audio_dma_stop(aic_rx->dma_dev);
    audio_disconnect_dev(aic_rx->aic_dev, aic_rx->dma_dev);
    audio_release_dma_dev(aic_rx->dma_dev);
    aic_rx->is_start = 0;
    flush_dcache_all();
}

void aic_init_capture_dma(struct aic_params *param)
{
    int id = param->id;
    struct aic_rx_data *aic_rx = &aic_rx_datas[id];

    if (aic_rx->is_start)
        aic_stop_capture_dma(id);

    aic_rx->unit_size = 32;
    aic_rx->aic_dev = Dev_src_baic0 + id;
    aic_rx->dma_dev = audio_requst_tar_dma_dev();
    aic_rx->frame_size = param->data_bits / 8 * param->channels;

    mutex_init(&aic_rx->lock);
    aic_init_rx_buffer(param, aic_rx);
    aic_init_rx_dma(param, aic_rx);
}

static unsigned int aic_get_readable_size(struct aic_rx_data *aic_rx)
{
    int ret, samples;
    unsigned int buffer_size = aic_rx->buffer_size;
    unsigned int pos = audio_dma_get_current_addr(aic_rx->dma_dev) - virt_to_phys(aic_rx->buffer);
    unsigned int size = sub_pos(buffer_size, pos, aic_rx->dma_pos);
    unsigned int unit_size = aic_rx->unit_size;

    aic_rx->dma_pos = pos;
    aic_rx->data_size += size;

    if (aic_rx->data_size > (buffer_size - unit_size)) {
        unsigned int align_pos = ALIGN(pos, unit_size);
        aic_rx->read_pos = add_pos(buffer_size, align_pos, unit_size);
        aic_rx->data_size = buffer_size - sub_pos(buffer_size, aic_rx->read_pos, pos);
    }

    ret = aic_rx->data_size > unit_size ? aic_rx->data_size - unit_size : 0;
    samples = ret / aic_rx->frame_size;

    return samples * aic_rx->frame_size;
}

static void aic_rx_timer_cb(struct hrtimer *timer)
{
    struct aic_rx_data *aic_rx = container_of(timer, struct aic_rx_data, timer);

    unsigned int n = aic_get_readable_size(aic_rx);
    if (n)
        thread_waiter_wakeup(&aic_rx->waiter);

    hrtimer_restart(timer, aic_rx->period_time);
}

static void aic_do_read_buffer(void *mem, unsigned int bytes, struct aic_rx_data *aic_rx)
{
    unsigned int buffer_size = aic_rx->buffer_size;
    unsigned int pos = aic_rx->read_pos;
    void *src = aic_rx->buffer;

    if (pos + bytes <= buffer_size) {
        invalidate_dcache_force((unsigned long)(src+pos), bytes);
        memcpy(mem, src+pos, bytes);
    } else {
        unsigned int size1 = buffer_size - pos;
        invalidate_dcache_force((unsigned long)(src+pos), size1);
        memcpy(mem, src+pos, size1);
        invalidate_dcache_force((unsigned long)(src), bytes-size1);
        memcpy(mem+size1, src, bytes-size1);
    }

    aic_rx->read_pos = add_pos(buffer_size, pos, bytes);
    aic_rx->data_size -= bytes;
}

int aic_read_frame(void *mem, int frames, unsigned int timeout_ms, int id)
{
    struct aic_rx_data *aic_rx = &aic_rx_datas[id];
    int bytes = frames * aic_rx->frame_size;
    unsigned int len = 0;

    mutex_lock(&aic_rx->lock);

    if (!aic_rx->is_start) {
        len = -EINVAL;
        goto unlock;
    }

    uint64_t end = timeout_to_systick_us(timeout_ms);

    os_enter_critical();

    while(bytes) {

        if (!aic_get_readable_size(aic_rx)) {
            os_exit_critical();
            int ret = thread_waiter_wait_until(&aic_rx->waiter, end);
            if (ret < 0) {
                if (!len)
                    len = -ETIMEDOUT;
                goto unlock;
            }
            os_enter_critical();
        }

        unsigned int n = aic_get_readable_size(aic_rx);
        if (!n) {
            if (!len)
                len = -ETIMEDOUT;
            goto exit_critical;
        }

        if (n > bytes)
            n = bytes;
        if (n > 512)
            n = 512;

        aic_do_read_buffer(mem, n, aic_rx);
        len += n;
        mem += n;
        bytes -= n;

        /* 打开中断，让其它中断可以响应
         */
        os_exit_critical();
        os_enter_critical();
    }

exit_critical:
    os_exit_critical();

unlock:
    mutex_unlock(&aic_rx->lock);

    return len >= 0 ? len/aic_rx->frame_size : len;
}