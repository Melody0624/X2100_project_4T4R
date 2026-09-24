
#include "uacaudio.h"
#include <usb/host_uac.h>

static volatile u8 devices_bit;
static uac_device_callback_t device_callback;

struct uac_device *uac_table[UAC_MINORS];
struct mutex device_lock[UAC_MINORS];
thread_cond_t free_cond[UAC_MINORS];

void usb_host_uac_register_callback(uac_device_callback_t callback)
{
    device_callback = callback;
}

void update_devices_bit(void)
{
    int i;
    struct uac_device *dev;
    u8 dev_bit = 0;
    uac_device_callback_t callback = device_callback;

    for (i = 0; i < UAC_MINORS; i++) {
        dev = uac_table[i];
        if (dev && dev->udev)
            dev_bit |= (1 << i);
    }

    devices_bit = dev_bit;
    if (callback)
        callback(dev_bit);
}

int usb_host_uac_get_inform(u8 id, uac_stream_inform_t inform[], int stream_cnt)
{
    struct uac_device *dev;
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs = NULL;
    int stream, index = 0, ret = 0;

    if (id >= UAC_MINORS) {
        UAC_ERROR("%s: id(%d) >= UAC_MINORS", __func__, id);
        return -EINVAL;
    }

    mutex_lock(&device_lock[id]);
    dev = uac_table[id];
    if (!UAC_READY(dev)) {
        UAC_ERROR("%s: no uac(%d) devices", __func__, id);
        mutex_unlock(&device_lock[id]);
        return -ENODEV;
    }
    dev->used++;
    mutex_unlock(&device_lock[id]);

    if (list_empty(&dev->pcm_list)) {
        UAC_ERROR("%s: uac(%d) no audio stream", __func__, id);
        ret = -ENODATA;
        goto unlock;
    }

    memset(inform, 0, sizeof(uac_stream_inform_t) * stream_cnt);

    list_for_each_entry(as, &dev->pcm_list, list) {
        if (!as || !as->uac)
            continue;

        for (stream = 0; stream < UAC_SUBSTREAM_NUMS; stream++) {
            subs = &(as->substream[stream]);
            if (!subs->dev || !subs->pcm)
                continue;

            inform[index].subs[stream].pcm_name = subs->pcm_name;
            inform[index].subs[stream].channels_list = subs->pcm->channels_list;
            inform[index].subs[stream].pcm_data_fmt_list = subs->pcm->pcm_data_fmt_list;
            inform[index].subs[stream].pcm_sample_rate_list = subs->pcm->pcm_sample_rate_list;
        }

        if (++index == stream_cnt)
            break;
    }

    if (index == 0)
        ret = -ENODATA;
unlock:
    mutex_lock(&device_lock[id]);
    dev->used--;
    if (dev->exiting && !dev->used)
        thread_cond_signal(&free_cond[id]);
    mutex_unlock(&device_lock[id]);

    return ret;
}
