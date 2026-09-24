#include <driver/cache.h>
#include <os.h>
#include <errno.h>
#include <stdlib.h>
#include <malloc.h>
#include <driver/dma.h>
#include <driver/pcm.h>
#include <common.h>
#include "dmic_regs.h"
#include "audio_dma.h"

struct dmic_data {
    int is_init;
    int is_start;
    void *dma_buffer;

    unsigned int dma_pos;
    unsigned int read_pos;
    unsigned int unit_size;
    unsigned int data_size;
    unsigned int frame_size;
    unsigned int fmt_width;
    unsigned int dma_frame_size;
    unsigned int buffer_size;
    unsigned int period_time; //us
    unsigned int old_buffer_size;

    struct audio_dma_desc *dma_desc;
    unsigned int channels;
    unsigned int dma_channels;
    unsigned int data_offset[8];
};

static struct dmic_data dmic;

static unsigned int sub_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + size - delta) % size;
}

static unsigned int add_pos(unsigned int size, unsigned int pos, unsigned int delta)
{
    return (pos + delta) % size;
}

void calculate_data_offset(int dma_channels)
{
    int channels = 0;
    if (dma_channels >= 2) {
        dmic.data_offset[channels++] = 0;
        dmic.data_offset[channels++] = 1;
    }

    if (dma_channels >= 4) {
        dmic.data_offset[channels++] = 2;
        dmic.data_offset[channels++] = 3;
    }

    if (dma_channels >= 6) {
        dmic.data_offset[channels++] = 4;
        dmic.data_offset[channels++] = 5;
    }

    if (dma_channels >= 8) {
        dmic.data_offset[channels++] = 6;
        dmic.data_offset[channels++] = 7;
    }
}

static unsigned int get_dma_addr(unsigned long start, unsigned int len)
{
    unsigned long cur_addr = audio_dma_get_current_addr(Dev_tar_dma5);
    if (cur_addr >= start && cur_addr < start + len)
        return cur_addr;
    return 0;
}

static unsigned int dmic_get_readable_size(void)
{
    int ret, samples;
    unsigned int buf_size = dmic.buffer_size;
    unsigned int buffer_addr = virt_to_phys(dmic.dma_buffer);
    unsigned long cur_addr = get_dma_addr(buffer_addr, buf_size);
    if (!cur_addr)
        return 0;

    unsigned int pos = cur_addr - buffer_addr;
    unsigned int size = sub_pos(buf_size, pos, dmic.dma_pos);
    unsigned int unit_size = dmic.unit_size;

    dmic.dma_pos = pos;
    dmic.data_size += size;

    if (dmic.data_size > (buf_size - unit_size)) {
        unsigned int align_pos = ALIGN(pos, unit_size);
        dmic.read_pos = add_pos(buf_size, align_pos, unit_size);
        dmic.data_size = buf_size - sub_pos(buf_size, dmic.read_pos, pos);
    }

    ret = dmic.data_size > unit_size ? dmic.data_size - unit_size : 0;
    samples = ret / dmic.dma_frame_size;

    return samples * dmic.frame_size;
}

static void dmic_init_rx_dma(struct dmic_params *param)
{
    if (dmic.dma_desc)
        free(dmic.dma_desc);
    dmic.dma_desc = cache_align_malloc(sizeof(*dmic.dma_desc));

    audio_dma_desc_init(dmic.dma_desc, dmic.dma_buffer, dmic.buffer_size, dmic.unit_size, dmic.dma_desc);
    audio_connect_dev(Dev_src_dmic, Dev_tar_dma5);
    audio_dma_set_callback(Dev_tar_dma5, NULL, NULL);
    audio_dma_config(Dev_tar_dma5, dmic.dma_channels, dmic.fmt_width, dmic.unit_size);
}

static void dmic_init_rx_buffer(struct dmic_params *param)
{
    unsigned int buffer_time, period_time;
    unsigned int buf_size, period_size;
    unsigned int sample_rate = param->sample_rate;

    buffer_time = param->buffer_time_ms >= MIN_BUFFER_TIME_MS ? param->buffer_time_ms : DEFAULT_BUFFER_TIME_MS;
    buf_size = (uint64_t)buffer_time * (sample_rate * dmic.frame_size) / 1000;

    buf_size = ALIGN(buf_size, dmic.unit_size);
    if (buf_size < dmic.unit_size * 8) {
        buf_size = dmic.unit_size * 8;
        buffer_time = buf_size * 1000 / (sample_rate * dmic.frame_size);
    }

    period_time = param->period_time_ms ? param->period_time_ms : DEFAULT_BUFFER_PERIO_MS;
    period_size = (uint64_t)period_time * (sample_rate * dmic.frame_size) / 1000;

    if (buf_size / period_size < PERIODS_MIN) {
        period_time = buffer_time / PERIODS_MIN;
        period_size = (uint64_t)period_time * (sample_rate * dmic.frame_size) / 1000;
        assert(period_time);
    }

    dmic.period_time = period_time * 1000;
    dmic.buffer_size = buf_size;

    if (!dmic.dma_buffer || dmic.old_buffer_size < buf_size) {
        if (dmic.dma_buffer)
            free(dmic.dma_buffer);
        dmic.dma_buffer = cache_align_malloc(buf_size);
        assert(dmic.dma_buffer);
        dmic.old_buffer_size = buf_size;
    }

    if (!dmic.is_init)
        dmic.is_init = 1;
}

void dmic_start_capture_dma(void)
{
    os_enter_critical();

    assert(!dmic.is_start);

    dmic.dma_pos = 0;
    dmic.read_pos = 0;
    dmic.data_size = 0;

    audio_dma_start(Dev_tar_dma5, dmic.dma_desc);

    dmic.is_start = 1;

    os_exit_critical();
}

void dmic_stop_capture_dma(void)
{
    audio_dma_stop(Dev_tar_dma5);
    audio_disconnect_dev(Dev_src_dmic, Dev_tar_dma5);
    audio_release_dma_dev(Dev_tar_dma5);
    dmic.is_start = 0;
    flush_dcache_all();
}

static int to_frame_size(int data_bits)
{
    if (data_bits == 16)
        return 16;
    if (data_bits == 24)
        return 32;
    return 16;
}

void dmic_init_capture_dma(struct dmic_params *param)
{
    if (dmic.is_start)
        dmic_stop_capture_dma();

    dmic.unit_size = 32;
    dmic.frame_size = to_frame_size(param->data_bits) * param->channels / 8;
    dmic.dma_frame_size = to_frame_size(param->data_bits) * param->dma_channels / 8;
    dmic.fmt_width = param->data_bits;
    dmic.channels = param->channels;
    dmic.dma_channels = param->dma_channels;

    dmic_init_rx_buffer(param);
    dmic_init_rx_dma(param);
}

static void copy_to_user_channels_16(void *dst_, void *src_, int bytes)
{
    short *dst = dst_;
    short *src = src_;
    int i,j;
    int dma_channels = dmic.dma_channels;
    int user_channels = dmic.channels;
    int samples = bytes / dmic.dma_frame_size;

    for (i = 0; i < user_channels; i++) {
        int data_offset = dmic.data_offset[i];
        for (j = 0; j < samples; j++) {
            dst[j * user_channels + i] = src[data_offset + dma_channels * j];
        }
    }
}

static void copy_to_user_channels_24(void *dst_, void *src_, int bytes)
{
    int *dst = dst_;
    int *src = src_;
    int i,j;
    int dma_channels = dmic.dma_channels;
    int user_channels = dmic.channels;
    int samples = bytes / dmic.dma_frame_size;

    for (i = 0; i < user_channels; i++) {
        int data_offset = dmic.data_offset[i];
        for (j = 0; j < samples; j++) {
            dst[j * user_channels + i] = src[data_offset + dma_channels * j];
        }
    }
}

static void do_memcpy(void *dst, void *src, int bytes)
{
    invalidate_dcache_force((unsigned long)(src), bytes);
    if (dmic.fmt_width == 16)
        copy_to_user_channels_16(dst, src, bytes);
    else
        copy_to_user_channels_24(dst, src, bytes);
}

static void dmic_do_read_buffer(void *mem, unsigned int bytes)
{
    unsigned int buf_size = dmic.buffer_size;
    unsigned int pos = dmic.read_pos;
    void *src = dmic.dma_buffer;
    int dma_bytes = bytes * dmic.dma_frame_size / dmic.frame_size;

    if (pos + dma_bytes <= buf_size) {
        do_memcpy(mem, src+pos, dma_bytes);
    } else {
        unsigned int size1 = buf_size - pos;
        unsigned int mem_pos = size1 * dmic.frame_size / dmic.dma_frame_size;
        do_memcpy(mem, src+pos, size1);
        do_memcpy(mem+mem_pos, src, dma_bytes-size1);
    }

    dmic.read_pos = add_pos(buf_size, pos, dma_bytes);
    dmic.data_size -= dma_bytes;
}

int dmic_read_frame(void *mem, int frames, unsigned int timeout_ms)
{
    int bytes = frames * dmic.frame_size;
    unsigned int len = 0;
    uint64_t now = systick_get_time_us();

    os_enter_critical();

    if (!dmic.is_start) {
        len = -EINVAL;
        goto unlock;
    }

    while (bytes) {
        if (timeout_ms != -1) {
            if (systick_get_time_us() - now >= timeout_ms * 1000) {
                if (!len)
                    len = -ETIMEDOUT;
                goto unlock;
            }
        }

        unsigned int n = dmic_get_readable_size();
        if (n) {
            if (n > bytes)
                n = bytes;

            dmic_do_read_buffer(mem, n);
            len += n;
            mem += n;
            bytes -= n;
        }

        /* 打开中断，让其它中断可以响应
         */
        os_exit_critical();

        if (!n)
            usleep(dmic.period_time);

        os_enter_critical();
    }

unlock:
    os_exit_critical();

    return len;
}

