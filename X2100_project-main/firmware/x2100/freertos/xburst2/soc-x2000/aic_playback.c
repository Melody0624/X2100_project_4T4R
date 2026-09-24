#include <driver/cache.h>
#include <driver/pcm.h>
#include <os.h>
#include <errno.h>
#include <stdlib.h>
#include <malloc.h>
#include <driver/dma.h>
#include <spinlock.h>
#include <common.h>
#include <os.h>
#include "audio_dma.h"
#include "aic.h"

#define ALIGN_DOWN(x, n) ((x) - (x)%(n))

struct aic_tx_data {
    int is_init;
    int is_start;
    void *buffer;
    int data_bits;

    unsigned int dma_pos;
    unsigned int write_pos;
    unsigned int data_size;
    unsigned int unit_size;
    unsigned int frame_size;
    unsigned int buffer_size;
    unsigned int period_time;
    unsigned int period_size;
    unsigned int old_buffer_size;

    struct mutex lock;
    spinlock_t spin;
    thread_waiter_t waiter;

    struct audio_dma_desc *dma_desc;
    enum audio_dev_id aic_dev;
    enum audio_dev_id dma_dev;
};

static struct aic_tx_data aic_tx_datas[AIC_NUMS];

static int icodec_vol;
extern int icodec_get_software_vol(void);

static unsigned int sub_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + size - delta) % size;
}

static unsigned int add_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + delta) % size;
}

#ifdef DEBUG
static void aic_playback_cb(void *data)
{
    struct aic_tx_data *aic_tx = data;
    int dma_id = dev_to_dma_id(aic_tx->dma_dev);

    unsigned int dsr = audio_read_reg(DSR(dma_id));

    audio_write_reg(DSR(dma_id), dsr);

    printf("aic_playback_cb: %x\n", dsr);
}
#endif

static void aic_init_tx_dma(struct aic_params *param, struct aic_tx_data *aic_tx)
{
    if (aic_tx->dma_desc)
        free(aic_tx->dma_desc);
    aic_tx->dma_desc = cache_align_malloc(sizeof(*aic_tx->dma_desc));

    audio_dma_desc_init(aic_tx->dma_desc, aic_tx->buffer, aic_tx->buffer_size, aic_tx->unit_size, aic_tx->dma_desc);
    audio_connect_dev(aic_tx->dma_dev, aic_tx->aic_dev);
    audio_dma_config(aic_tx->dma_dev, param->channels, param->data_bits, aic_tx->unit_size);
#ifdef DEBUG
    audio_dma_set_callback(aic_tx->dma_dev, aic_playback_cb, aic_tx);
#else
    audio_dma_set_callback(aic_tx->dma_dev, NULL, NULL);
#endif
}

static void aic_init_tx_buffer(struct aic_params *param, struct aic_tx_data *aic_tx)
{
    unsigned int buffer_time, period_time;
    unsigned int buffer_size, period_size;
    unsigned int sample_rate = param->sample_rate;

    buffer_time = param->buffer_time_ms >= MIN_BUFFER_TIME_MS ? param->buffer_time_ms : DEFAULT_BUFFER_TIME_MS;
    buffer_size = (uint64_t)buffer_time * (sample_rate * aic_tx->frame_size) / 1000;

    buffer_size = ALIGN(buffer_size, aic_tx->unit_size);
    if (buffer_size < aic_tx->unit_size * 8) {
        buffer_size = aic_tx->unit_size * 8;
        buffer_time = buffer_size * 1000 / (sample_rate * aic_tx->frame_size);
    }

    period_time = param->period_time_ms ? param->period_time_ms : DEFAULT_BUFFER_PERIO_MS;
    period_size = (uint64_t)period_time * (sample_rate * aic_tx->frame_size) / 1000;

    if (buffer_size / period_size < MIN_PERIODS) {
        period_time = buffer_time / MIN_PERIODS;
        period_size = (uint64_t)period_time * (sample_rate * aic_tx->frame_size) / 1000;
        assert(period_time);
    }

    aic_tx->period_time = period_time * 1000;
    aic_tx->period_size = period_size;
    aic_tx->buffer_size = buffer_size;

    if (!aic_tx->buffer || aic_tx->old_buffer_size < buffer_size) {
        if (aic_tx->buffer)
            free(aic_tx->buffer);

        aic_tx->buffer = cache_align_malloc(buffer_size);
        assert(aic_tx->buffer);

        aic_tx->old_buffer_size = buffer_size;
    }

    if (!aic_tx->is_init)
        aic_tx->is_init = 1;
}

static inline void do_clear_mem(void *mem, int size)
{
    memset(mem, 0, size);
    flush_dcache_force((unsigned long)mem, size);
}

static void clear_mem(void *mem, int mem_size, int pos, int size)
{
    int sz1 = size, sz2 = 0;
    if (pos + size > mem_size) {
        sz1 = mem_size - pos;
        sz2 = size - sz1;
    }

    if (sz1)
        do_clear_mem(mem+pos, sz1);

    if (sz2)
        do_clear_mem(mem, sz2);
}

static inline void do_tune_mem(void *dst, void *src, int size)
{
    int i, len;
    switch (aic_tx_datas[0].data_bits)
    {
    case 16: {
        short *p_dst = dst, *p_src = src;
        len = size / 2;
        for (i = 0; i < len; i++)
            p_dst[i] = p_src[i] * icodec_vol / 100;
        break;
    }

    case 24: {
        int *p_dst = dst, *p_src = src;
        len = size / 4;
        /* S24_LE 符号位在 bit23, 需要取出符号位运算 */
        for (i = 0; i < len; i++)
            p_dst[i] = ((p_src[i] ^ 0x00800000) * icodec_vol / 100) ^ 0x00800000;
        break;
    }
    default:
        printf("aic: icodec do not support this data_bits(%d)\n", aic_tx_datas[0].data_bits);
        break;
    }
}

static inline void do_copy_mem(void *dst, int size, void *src)
{
    if (icodec_vol < 99)
        do_tune_mem(dst, src, size);
    else
        memcpy(dst, src, size);
    flush_dcache_force((unsigned long)dst, size);
}

static void copy_mem(void *mem, int mem_size, int pos, int size, void *src)
{
    int sz1 = size, sz2 = 0;
    if (pos + size > mem_size) {
        sz1 = mem_size - pos;
        sz2 = size - sz1;
    }

    if (sz1)
        do_copy_mem(mem+pos, sz1, src);

    if (sz2)
        do_copy_mem(mem, sz2, src+sz1);
}

void tx_playback_notify_func(void *data)
{
    struct aic_tx_data *aic_tx = data;
    void *mem = aic_tx->buffer;
    unsigned int mem_size = aic_tx->buffer_size;
    unsigned long start = virt_to_phys(mem);

    while (aic_tx->is_start) {
        usleep(aic_tx->period_time/2);

        spin_lock(&aic_tx->spin);

        unsigned int pos = audio_dma_get_current_addr(aic_tx->dma_dev) - start;
        int size = sub_pos(mem_size, pos, aic_tx->dma_pos);

        if (size)  {
            clear_mem(mem, mem_size, aic_tx->dma_pos, size);
            aic_tx->dma_pos = pos;

            if (size < aic_tx->data_size)
                aic_tx->data_size -= size;
            else
                aic_tx->data_size = 0;
        }

        spin_unlock(&aic_tx->spin);

        if (size)
            thread_waiter_wakeup(&aic_tx->waiter);
    }

    clear_mem(mem, mem_size, 0, mem_size);

    thread_waiter_wakeup(&aic_tx->waiter);
}

void aic_start_playback_dma(int id)
{
    struct aic_tx_data *aic_tx = &aic_tx_datas[id];

    os_enter_critical();

    assert(!aic_tx->is_start);

    aic_tx->dma_pos = 0;
    aic_tx->write_pos = 0;
    aic_tx->data_size = 0;

    audio_dma_start(aic_tx->dma_dev, aic_tx->dma_desc);

    aic_tx->is_start = 1;
    thread_create("pcm notify thread", 2048, tx_playback_notify_func, aic_tx);

    os_exit_critical();
}

void aic_stop_playback_dma(int id)
{
    struct aic_tx_data *aic_tx = &aic_tx_datas[id];

    mutex_lock(&aic_tx->lock);

    uint64_t end = timeout_to_systick_us(aic_tx->buffer_size);

    while (aic_tx->data_size) {
        int ret = thread_waiter_wait_until(&aic_tx->waiter, end);
        if (ret < 0)
            break;
    }

    aic_tx->is_start = 0;
    thread_waiter_wait(&aic_tx->waiter);

    audio_dma_stop(aic_tx->dma_dev);

    mutex_unlock(&aic_tx->lock);

    audio_disconnect_dev(aic_tx->dma_dev, aic_tx->aic_dev);
    audio_release_dma_dev(aic_tx->dma_dev);
}

void aic_init_playback_dma(struct aic_params *param)
{
    int id = param->id;
    struct aic_tx_data *aic_tx = &aic_tx_datas[id];

    if (aic_tx->is_start)
        aic_stop_playback_dma(id);

    aic_tx->unit_size = 32;
    aic_tx->aic_dev = Dev_tar_baic0 + id;
    aic_tx->dma_dev = audio_requst_src_dma_dev();

    aic_tx->data_bits = param->data_bits;
    aic_tx->frame_size = param->data_bits / 8 * param->channels;

    mutex_init(&aic_tx->lock);
    spin_lock_init(&aic_tx->spin);
    thread_waiter_init(&aic_tx->waiter);
    aic_init_tx_buffer(param, aic_tx);
    aic_init_tx_dma(param, aic_tx);
}

int aic_write_frame(void *buf, int frames, unsigned int timeout_ms, int id)
{
    unsigned int len = 0;
    struct aic_tx_data *aic_tx = &aic_tx_datas[id];
    int bytes = frames * aic_tx->frame_size;

    mutex_lock(&aic_tx->lock);

    if (!aic_tx->is_start) {
        len = -EINVAL;
        goto unlock;
    }

    if (id == 0)
        icodec_vol = icodec_get_software_vol();
    else
        icodec_vol = 100;

    void *mem = aic_tx->buffer;
    unsigned int mem_size = aic_tx->buffer_size;
    unsigned int unit_size = aic_tx->unit_size;
    unsigned int period_size = aic_tx->period_size;
    unsigned long start = virt_to_phys(mem);

    uint64_t end = timeout_to_systick_us(timeout_ms);

    while (bytes) {
        unsigned int usable_size, size = 0;
        spin_lock(&aic_tx->spin);

        unsigned int pos = audio_dma_get_current_addr(aic_tx->dma_dev) - start;

        usable_size = mem_size - period_size - aic_tx->data_size;
        usable_size = ALIGN_DOWN(usable_size, unit_size);

        if (aic_tx->data_size == 0)
            aic_tx->write_pos = add_pos(mem_size, ALIGN(pos, unit_size), unit_size);

        if (usable_size) {
            size = usable_size < bytes ? usable_size : bytes;
            size = size < 512 ? size : 512;
            copy_mem(mem, mem_size, aic_tx->write_pos, size, buf);
            aic_tx->write_pos = add_pos(mem_size, aic_tx->write_pos, size);
            buf += size;
            bytes -= size;
            len += size;
            aic_tx->data_size += size;
        }

        spin_unlock(&aic_tx->spin);

        if (!usable_size) {
            int ret = thread_waiter_wait_until(&aic_tx->waiter, end);
            if (ret < 0) {
                if (!len)
                    len = -ETIMEDOUT;
                goto unlock;
            }
        }
    }

unlock:
    mutex_unlock(&aic_tx->lock);

    return len >= 0 ? len/aic_tx->frame_size : len;
}
