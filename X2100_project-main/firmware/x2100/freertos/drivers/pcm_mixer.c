#include <driver/pcm_mixer.h>
#include <errno.h>

void pcm_mixer_init(struct pcm_mixer *m, int step)
{
    m->F = 1;
    m->step = step;
    m->count = 0;
    m->threshold = 0;
}

void pcm_mixer_set_threshold(struct pcm_mixer *m, int threshold)
{
    m->threshold = threshold;
}

void pcm_mixer_mix(
    struct pcm_mixer *m, short **inputs, short *out,
    int input_nums, int channel_nums, int sample_nums)
{
    int i, j, k;

    float F = m->F;
    int count = m->count;

    int tmp[channel_nums];
    short *in[input_nums];
    for (i = 0; i < input_nums; i++) {
        in[i] = inputs[i];
    }

    for (i = 0; i < sample_nums; i++) {
        for (k = 0; k < channel_nums; k++) {
            int mix = 0;
            for (j = 0; j < input_nums; j++) {
                mix += in[j][k];
            }

            int max = 32767;
            int min = -32768;

            if (mix > max) {
                float f = (float)max / mix;
                if (f < F)
                    F = f;
                count = 0;
            } else if (mix < min) {
                float f = (float)min / mix;
                if (f < F)
                    F = f;
                count = 0;
            }

            tmp[k] = mix;
        }

        for (k = 0; k < channel_nums; k++) {
            out[k] = tmp[k] * F;
        }

        for (j = 0; j < input_nums; j++) {
            in[j] += channel_nums;
        }

        if (count++ >= m->threshold) {
            if (F >= 0.999)
                F = 1;
            else
                F += (1 - F) / m->step;
            count = m->threshold * 0.8;
        }

        out += channel_nums;
    }

    m->F = F;
    m->count = count;
}

#include <ring_mem.h>
#include <os.h>
#include <driver/pcm.h>
#include <string.h>
#include <common.h>

#define MAX_USER 8

struct pcm_mixer_server;

struct pcm_mixer_dev {
    const char *name;
    struct pcm_mixer_server *mixer;
    struct pcm_dev_data dev_data;
    struct pcm_device *dev;
    struct ring_mem ring;
    struct thread_waiter waiter;
    void *buf;
    int i;
};

struct pcm_mixer_server {
    struct pcm_mixer m;
    int period_samples;
    int channels;
    struct pcm_mixer_dev * volatile dev[MAX_USER];
    short * volatile buf[MAX_USER];
    thread_ptr_t thread;
    struct mutex lock;
    struct pcm_device *dai;
    struct thread_waiter waiter;
    volatile int is_stop;
    volatile int is_enable;
    volatile int is_start;
};

static void check_read_data(struct pcm_mixer_dev *dev, void *buf, int len)
{
    if (dev) {
        int size = ring_mem_readable_size(&dev->ring);
        if (size > len)
            size = len;
        if (size) {
            ring_mem_read(&dev->ring, buf, size);
            thread_waiter_wakeup(&dev->waiter);
        }
        memset(buf+size, 0, len-size);
        // printf("r: %d\n", size);
    }
}

static int frame_size(struct pcm_mixer_server *m)
{
    return m->channels*sizeof(short);
}

static void mixer_thread_func(void *data)
{
    struct pcm_mixer_server *m = data;

    int buf_size = m->period_samples*frame_size(m);
    short *out = malloc(buf_size);
    assert(out);

    while (!m->is_stop) {
        while (!m->is_stop && !m->is_start)
            thread_waiter_wait(&m->waiter);

        pcm_mixer_init(&m->m, 1000);

        while (!m->is_stop && m->is_start) {
            int i, j = 0;
            short *input[MAX_USER];
            mutex_lock(&m->lock);
            for (i = 0; i < 8; i++) {
                check_read_data(m->dev[i], m->buf[i], buf_size);
                if (m->dev[i])
                    input[j++] = m->buf[i];
            }
            mutex_unlock(&m->lock);

            pcm_mixer_mix(&m->m, input, out, j, m->channels, m->period_samples);

            pcm_write_frame_timeout(m->dai, out, m->period_samples, 300);
        }
    }

    free(out);
    m->is_stop = 2;
}

struct pcm_mixer_server *pcm_mixer_server_create(
    struct pcm_device *dai, int channels, int period_samples)
{
    struct pcm_mixer_server *mixer = malloc(sizeof(*mixer));

    memset(mixer, 0, sizeof(*mixer));
    mixer->period_samples = period_samples;
    mixer->channels = channels;
    mixer->dai = dai;

    mutex_init(&mixer->lock);
    thread_waiter_init(&mixer->waiter);

    mixer->thread = thread_create(
        "pcm mixer server", 4096, mixer_thread_func, mixer);

    return mixer;
}

void pcm_mixer_server_delete(struct pcm_mixer_server *m)
{
    int i;

    mutex_lock(&m->lock);

    m->is_stop = 1;
    thread_waiter_wakeup(&m->waiter);

    for (i = 0; i < MAX_USER; i++) {
        if (m->dev[i]) {
            fprintf(stderr, "pcm mixer: dev: %s is not delete\n", m->dev[i]->name);
            assert(0);
        }
        if (m->buf[i])
            free(m->buf[i]);
    }

    mutex_unlock(&m->lock);

    while (m->is_stop == 1)
        usleep(1000);

    free(m);
}

static int m_pcm_write_frame(struct pcm_dev_data *data,
                        void *buf, int frame_count, unsigned int timeout_ms)
{
    struct pcm_mixer_dev *dev = container_of(data, struct pcm_mixer_dev, dev_data);
    struct pcm_mixer_server *m = dev->mixer;
    int size = frame_count*frame_size(m);
    int len = 0;

    uint64_t end = timeout_to_systick_us(timeout_ms);

    while (size) {
        while (!ring_mem_writable_size(&dev->ring)) {
            int ret = thread_waiter_wait_until(&dev->waiter, end);
            if (ret < 0) {
                fprintf(stderr, "pcm mixer: wait writable timeout\n");
                goto out;
            }
        }

        int sz = ring_mem_write(&dev->ring, buf, size);
        // printf("w: %d\n", sz);
        size -= sz;
        buf += sz;
        len += sz;
    }

out:
    return len ? len / frame_size(m) : -ETIMEDOUT;
}

static int m_pcm_enable(struct pcm_dev_data *data, struct pcm_params *params)
{
    struct pcm_mixer_dev *dev = container_of(data, struct pcm_mixer_dev, dev_data);
    struct pcm_mixer_server *m = dev->mixer;
    int ret = 0;

    mutex_lock(&m->lock);

    if (m->is_enable++ == 0) {
        ret = pcm_enable(m->dai, params);
        if (ret)
            m->is_enable = 0;
    }

    mutex_unlock(&m->lock);

    return ret;
}

static void m_pcm_disable(struct pcm_dev_data *data)
{
    struct pcm_mixer_dev *dev = container_of(data, struct pcm_mixer_dev, dev_data);
    struct pcm_mixer_server *m = dev->mixer;

    mutex_lock(&m->lock);

    if (--m->is_enable == 0) {
        pcm_disable(m->dai);
    }

    mutex_unlock(&m->lock);
}

static int m_pcm_start(struct pcm_dev_data *data)
{
    struct pcm_mixer_dev *dev = container_of(data, struct pcm_mixer_dev, dev_data);
    struct pcm_mixer_server *m = dev->mixer;
    int ret = 0;

    mutex_lock(&m->lock);

    if (m->is_start++ == 0) {
        ret = pcm_start(m->dai);
        if (ret == 0)
            thread_waiter_wakeup(&m->waiter);
        else
            m->is_start = 0;
    }

    mutex_unlock(&m->lock);

    return ret;
}

static void m_pcm_stop(struct pcm_dev_data *data)
{
    struct pcm_mixer_dev *dev = container_of(data, struct pcm_mixer_dev, dev_data);
    struct pcm_mixer_server *m = dev->mixer;

    mutex_lock(&m->lock);

    if (--m->is_start == 0)
        pcm_stop(m->dai);

    mutex_unlock(&m->lock);
}

struct pcm_mixer_dev *pcm_mixer_dev_create(
    struct pcm_mixer_server *m, const char *name, int buf_size)
{
    int period_size = m->period_samples*m->channels*sizeof(short);
    if (buf_size < period_size*2)
        buf_size = period_size*2;

    /* ring mem size must be a power of 2 */
    buf_size = roundup_pow_of_two(buf_size);

    struct pcm_mixer_dev *dev = malloc(sizeof(*dev));
    assert(dev);

    dev->buf = malloc(buf_size);
    assert(dev->buf);

    short *period_buf = malloc(period_size);
    assert(period_buf);

    ring_mem_init(&dev->ring, dev->buf, buf_size);
    thread_waiter_init(&dev->waiter);

    os_enter_critical();

    int i;
    for (i = 0; i < MAX_USER; i++) {
        if (!m->dev[i])
            break;
    }

    if (i == MAX_USER) {
        fprintf(stderr, "pcm mixer: too many dev register\n");
        assert(0);
    }

    if (!m->buf[i]) {
        m->buf[i] = malloc(period_size);
        assert(m->buf[i]);
    }

    os_exit_critical();

    m->dev[i] = dev;
    dev->i = i;
    dev->mixer = m;

    dev->dev = pcm_clone_device(m->dai, &dev->dev_data, name);
    dev->dev_data.pcm_write_frame = m_pcm_write_frame;
    dev->dev_data.pcm_enable = m_pcm_enable;
    dev->dev_data.pcm_disable = m_pcm_disable;
    dev->dev_data.pcm_start = m_pcm_start;
    dev->dev_data.pcm_stop = m_pcm_stop;

    return dev;
}

void pcm_mixer_dev_delete(struct pcm_mixer_dev *dev)
{
    struct pcm_mixer_server *m = dev->mixer;

    mutex_lock(&m->lock);

    assert(m->dev[dev->i] == dev);

    pcm_unregister(&dev->dev_data);

    m->dev[dev->i] = NULL;
    free(dev->buf);
    free(dev);

    mutex_unlock(&m->lock);
}
