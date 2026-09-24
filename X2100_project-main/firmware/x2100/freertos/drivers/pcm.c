#include <driver/pcm.h>
#include <common.h>
#include <list.h>
#include <os.h>

static LIST_HEAD(pcm_list);

struct pcm_device *pcm_register(struct pcm_dev_data *data)
{
    struct list_head *pos;

    assert(data && data->name);
    assert(data->pcm_enable);
    assert(data->pcm_disable);
    assert(data->pcm_start);
    assert(data->pcm_stop);

    os_enter_critical();

    list_for_each(pos, &pcm_list) {
        struct pcm_device *dev = list_entry(pos, struct pcm_device, link);
        if (dev->data == data)
            panic("don't register the same pcm_dev_data: %s\n", data->name);
        if (!strcmp(dev->data->name, data->name))
            panic("pcm device (%s) has been registered\n", data->name);
    }

    struct pcm_device *dev = malloc(sizeof(*dev));
    memset(dev, 0, sizeof(*dev));
    dev->data = data;

    mutex_init(&dev->lock);

    wake_lock_init(&dev->w_lock, "pcm_wake_lock");

    list_add_tail(&dev->link, &pcm_list);

    os_exit_critical();

    return dev;
}

void pcm_unregister(struct pcm_dev_data *data)
{
    struct list_head *pos;

    os_enter_critical();
    list_for_each(pos, &pcm_list) {
        struct pcm_device *dev = list_entry(pos, struct pcm_device, link);
        if (dev->data == data) {
            if (dev->is_enable && dev->is_start)
                panic("pcm: can't unregister %s when it is active\n", data->name);
            dev->data = NULL;
            list_del(&dev->link);
            wake_lock_deinit(&dev->w_lock);
            free(dev);
            break;
        }
    }
    os_exit_critical();
}

struct pcm_device *pcm_get(const char *name)
{
    struct list_head *pos;
    struct pcm_device *device = NULL;

    os_enter_critical();

    list_for_each(pos, &pcm_list) {
        struct pcm_device *dev = list_entry(pos, struct pcm_device, link);
        if (!strcmp(dev->data->name, name)) {
            device = dev;
            break;
        }
    }

    os_exit_critical();

    return device;
}

int pcm_write_frame_timeout(struct pcm_device *dev,
    void *buf, int frame_count, unsigned int timeout_ms)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (frame_count <= 0) {
        ret = 0;
        goto unlock;
    }

    if (dev->data->stream_type != pcm_stream_playback) {
        ret = -EINVAL;
        goto unlock;
    }

    if (!dev->data->pcm_write_frame) {
        ret = -ENODEV;
        goto unlock;
    }

    if (!dev->is_start) {
        ret = -EINVAL;
        goto unlock;
    }

    ret = dev->data->pcm_write_frame(dev->data, buf, frame_count, timeout_ms);

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

int pcm_write_frame(struct pcm_device *dev, void *buf, int frame_count)
{
    return pcm_write_frame_timeout(dev, buf, frame_count, OS_TIMEOUT_NOT_LIMIT_MS);
}

int pcm_read_frame_timeout(struct pcm_device *dev,
    void *buf, int frame_count, unsigned int timeout_ms)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (frame_count <= 0) {
        ret = 0;
        goto unlock;
    }

    if (dev->data->stream_type != pcm_stream_capture) {
        ret = -EINVAL;
        goto unlock;
    }

    if (!dev->data->pcm_read_frame) {
        ret = -ENODEV;
        goto unlock;
    }

    if (!dev->is_start) {
        ret = -EINVAL;
        goto unlock;
    }

    ret = dev->data->pcm_read_frame(dev->data, buf, frame_count, timeout_ms);

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

int pcm_read_frame(struct pcm_device *dev, void *buf, int frame_count)
{
    return pcm_read_frame_timeout(dev, buf, frame_count, OS_TIMEOUT_NOT_LIMIT_MS);
}

static inline int check_val(unsigned int val, unsigned int mask, const char *name)
{
    if (mask && !(mask & BIT(val))) {
        printf("pcm: failed to check param %s, list is %x, but value %d\n", name, mask, val);
        return 0;
    }

    return 1;
}

#define CHECK_VAL(name) check_val(p->name , d->name##_list , #name)

int pcm_check_params(struct pcm_device *dev, struct pcm_params *p)
{
    struct pcm_dev_data *d = dev->data;

    return
    CHECK_VAL(pcm_interface) &&
    CHECK_VAL(i2s_frame_mode) &&
    CHECK_VAL(i2s_bclk_direction) &&
    CHECK_VAL(i2s_frame_direction) &&
    CHECK_VAL(pcm_data_fmt) &&
    CHECK_VAL(pcm_sample_rate) &&
    CHECK_VAL(channels);
}

int pcm_enable(struct pcm_device *dev, struct pcm_params *param)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (dev->is_enable) {
        ret = 0;
        goto unlock;
    }

    if (!pcm_check_params(dev, param)) {
        ret = -EINVAL;
        goto unlock;
    }

    wake_lock(&dev->w_lock);

    ret = dev->data->pcm_enable(dev->data, param);

    if (!ret) {
        dev->is_enable = 1;
        dev->params = *param;
        if (dev->data->pcm_get_volume)
            dev->volume = dev->data->pcm_get_volume(dev->data);
    } else {
        wake_unlock(&dev->w_lock);
    }

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

void pcm_disable(struct pcm_device *dev)
{
    if (!dev || !dev->data)
        return;

    mutex_lock(&dev->lock);

    if (!dev->is_enable) {
        mutex_unlock(&dev->lock);
        return;
    }

    if (dev->is_start) {
        dev->data->pcm_stop(dev->data);
        dev->is_start = 0;
    }

    dev->data->pcm_disable(dev->data);
    dev->is_enable = 0;
    dev->is_mute = 0;

    wake_unlock(&dev->w_lock);

    mutex_unlock(&dev->lock);

    if (dev->is_exiting && dev->data->pcm_remove)
        dev->data->pcm_remove(dev->data);
}

int pcm_start(struct pcm_device *dev)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (dev->is_start) {
        ret = 0;
        goto unlock;
    }

    if (!dev->is_enable) {
        ret = -EINVAL;
        goto unlock;
    }

    ret = dev->data->pcm_start(dev->data);

    if (!ret)
        dev->is_start = 1;

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

void pcm_stop(struct pcm_device *dev)
{
    if (!dev || !dev->data || dev->is_exiting)
        return;

    mutex_lock(&dev->lock);

    if (!dev->is_start)
        goto unlock;

    dev->data->pcm_stop(dev->data);

    dev->is_start = 0;
    dev->is_mute = 0;

unlock:
    mutex_unlock(&dev->lock);
}

int pcm_set_mute(struct pcm_device *dev, int mute)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mute = !!mute;

    mutex_lock(&dev->lock);

    if (!dev->is_enable) {
        ret = -EINVAL;
        goto unlock;
    }

    if (!dev->data->pcm_set_mute) {
        ret = -ENODEV;
        goto unlock;
    }

    ret = dev->data->pcm_set_mute(dev->data, mute);

    if (!ret)
        dev->is_mute = mute;

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

int pcm_get_mute(struct pcm_device *dev)
{
    return dev->is_mute;
}

int pcm_set_volume(struct pcm_device *dev, int val)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (!dev->is_enable) {
        ret = -EINVAL;
        goto unlock;
    }

    if (!dev->data->pcm_set_volume) {
        ret = -ENODEV;
        goto unlock;
    }

    ret = dev->data->pcm_set_volume(dev->data, val);

    if (!ret)
        dev->volume = val;

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

int pcm_get_volume(struct pcm_device *dev)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (!dev->is_enable) {
        ret = -EINVAL;
        goto unlock;
    }

    if (!dev->data->pcm_get_volume) {
        if (dev->data->pcm_set_volume)
            ret = dev->volume;
        else
            ret = -ENODEV;
        goto unlock;
    }

    ret = dev->data->pcm_get_volume(dev->data);
    if (ret >= 0)
        dev->volume = ret;

unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

int pcm_private_ctrl(struct pcm_device *dev, const char *ctrl_id, unsigned long value)
{
    int ret;

    if (!dev || !dev->data || dev->is_exiting)
        return -EINVAL;

    mutex_lock(&dev->lock);

    if (!dev->data->pcm_private_ctrl) {
        ret = -ENODEV;
        goto unlock;
    }

    ret = dev->data->pcm_private_ctrl(dev->data, ctrl_id, value);
    if (ret)
        printf("pcm_private_ctrl %s fail! return value 0x%02x\n", ctrl_id, ret);
unlock:
    mutex_unlock(&dev->lock);

    return ret;
}

unsigned int pcm_data_sample_size(pcm_data_fmt fmt)
{
    static const unsigned char fmtsizes[] = {
        [pcm_fmt_S8] = 1,
        [pcm_fmt_U8] = 1,
        [pcm_fmt_S16LE] = 2,
        [pcm_fmt_S16BE] = 2,
        [pcm_fmt_U16LE] = 2,
        [pcm_fmt_U16BE] = 2,
        [pcm_fmt_S24LE] = 4,
        [pcm_fmt_S24BE] = 4,
        [pcm_fmt_U24LE] = 4,
        [pcm_fmt_U24BE] = 4,
        [pcm_fmt_S32LE] = 4,
        [pcm_fmt_S32BE] = 4,
        [pcm_fmt_U32LE] = 4,
        [pcm_fmt_U32BE] = 4,
    };

    assert(fmt < ARRAY_SIZE(fmtsizes));

    return fmtsizes[fmt];
}

unsigned int pcm_data_sample_rate(pcm_sample_rate rate)
{
    static const unsigned int rates[] = {
        [pcm_rate_5512] = 5512,
        [pcm_rate_8000] = 8000,
        [pcm_rate_11025] = 11025,
        [pcm_rate_12000] = 12000,
        [pcm_rate_16000] = 16000,
        [pcm_rate_22050] = 22050,
        [pcm_rate_24000] = 24000,
        [pcm_rate_32000] = 32000,
        [pcm_rate_44100] = 44100,
        [pcm_rate_48000] = 48000,
        [pcm_rate_64000] = 64000,
        [pcm_rate_88200] = 88200,
        [pcm_rate_96000] = 96000,
        [pcm_rate_176400] = 176400,
        [pcm_rate_192000] = 192000,
        [pcm_rate_384000] = 384000,
    };

    assert(rate < ARRAY_SIZE(rates));

    return rates[rate];
}

int pcm_frame_size(struct pcm_params *param)
{
    return pcm_data_sample_size(param->pcm_data_fmt) * param->channels;
}

struct pcm_params *pcm_get_device_params(struct pcm_device *dev)
{
    return &dev->params;
}

static int m_pcm_write_frame(struct pcm_dev_data *dev,
                        void *buf, int frame_count, unsigned int timeout_ms)
{
    return pcm_write_frame(dev->drv_data, buf, frame_count);
}

static int m_pcm_read_frame(struct pcm_dev_data *dev,
                        void *buf, int frame_count, unsigned int timeout_ms)
{
    return pcm_read_frame_timeout(dev->drv_data, buf, frame_count, timeout_ms);
}

static int m_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    return pcm_enable(dev->drv_data, params);
}

static void m_pcm_disable(struct pcm_dev_data *dev)
{
    return pcm_disable(dev->drv_data);
}

static int m_pcm_start(struct pcm_dev_data *dev)
{
    return pcm_start(dev->drv_data);
}

static void m_pcm_stop(struct pcm_dev_data *dev)
{
    return pcm_stop(dev->drv_data);
}

static int m_pcm_set_mute(struct pcm_dev_data *dev, int mute)
{
    return pcm_set_mute(dev->drv_data, mute);
}

static int m_pcm_set_volume(struct pcm_dev_data *dev, int val)
{
    return pcm_set_volume(dev->drv_data, val);
}

static int m_pcm_get_volume(struct pcm_dev_data *dev)
{
    return pcm_get_volume(dev->drv_data);
}

static int m_pcm_private_ctrl(struct pcm_dev_data *dev, const char *ctrl_id, unsigned long value)
{
    return pcm_private_ctrl(dev->drv_data, ctrl_id, value);
}

struct pcm_device *pcm_clone_device(struct pcm_device *dev, struct pcm_dev_data *data, const char *name)
{
    memcpy(data, dev->data, sizeof(*data));

    data->name = name;
    data->pcm_write_frame = m_pcm_write_frame;
    data->pcm_read_frame = m_pcm_read_frame;
    data->pcm_enable = m_pcm_enable;
    data->pcm_disable = m_pcm_disable;
    data->pcm_start = m_pcm_start;
    data->pcm_stop = m_pcm_stop;
    data->pcm_set_mute = m_pcm_set_mute;
    data->pcm_set_volume = m_pcm_set_volume;
    data->pcm_get_volume = m_pcm_get_volume;
    data->pcm_private_ctrl = m_pcm_private_ctrl;

    struct pcm_device *dev_new = pcm_register(data);
    if (dev_new)
        data->drv_data = dev;

    return dev_new;
}