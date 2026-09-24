#include "uacaudio.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"

#define DEFAULT_CHANNEL                 1
#define SUBSTREAM_FLAG_DATA_EP_STARTED  0

struct snd_pcm_hw_constraint_list {
    unsigned int count;
    const unsigned int *list;
    unsigned int mask;
};

static unsigned int rates[] = { 5512, 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100,
                                 48000, 64000, 88200, 96000, 176400, 192000 };

/* utils */

pcm_sample_rate snd_pcm_rate_to_rate_enum(unsigned int rate)
{
    unsigned int i;

    for (i = 0; i < pcm_rate_nums; i++)
        if (rate == rates[i])
            return (pcm_sample_rate) i;

    return pcm_rate_nums;
}

u64 pcm_format_to_bits(pcm_data_fmt pcm_format)
{
    return 1ULL << (int) pcm_format;
}

static signed long bytes_to_frames(struct snd_usb_endpoint *ep, ssize_t size)
{
    return size * 8 / ep->frame_bits;
}

static int mixer_ctrl_intf(struct usb_mixer_interface *mixer)
{
    return snd_usb_get_iface_desc(mixer->hostif)->bInterfaceNumber;
}

/* pitch control */

static int init_pitch_v1(struct uac_device *uac, int iface,
             struct usb_host_interface *alts,
             struct audioformat *fmt)
{
    struct usb_device *dev = uac->udev;
    unsigned int ep;
    unsigned char data[1];
    int err;

    ep = snd_usb_get_ep_desc(alts, 0)->bEndpointAddress;
    data[0] = 1;
    err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
                  USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT,
                  UAC_EP_CS_ATTR_PITCH_CONTROL << 8, ep,
                  data, sizeof(data));
    if (err < 0) {
        UAC_ERROR("%d:%d:%d: cannot set enable PITCH (v1)", dev->devnum, iface, fmt->altsetting);
        return err;
    }

    return 0;
}

static int init_pitch_v2(struct uac_device *uac, int iface,
             struct usb_host_interface *alts,
             struct audioformat *fmt)
{
    struct usb_device *dev = uac->udev;
    unsigned char data[1];
    int err;

    data[0] = 1;
    err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC2_CS_CUR,
                  USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_OUT,
                  UAC2_EP_CS_PITCH << 8, 0,
                  data, sizeof(data));
    if (err < 0) {
        UAC_ERROR("%d:%d:%d: cannot set enable PITCH (v2)", dev->devnum, iface, fmt->altsetting);
        return err;
    }

    return 0;
}

static int snd_usb_init_pitch(struct uac_device *uac, int iface,
               struct usb_host_interface *alts,
               struct audioformat *fmt)
{
    struct usb_interface_descriptor *altsd = snd_usb_get_iface_desc(alts);

    if (!(fmt->attributes & UAC_EP_CS_ATTR_PITCH_CONTROL))
        return 0;

    switch (altsd->bInterfaceProtocol) {
    case UAC_VERSION_1:
        return init_pitch_v1(uac, iface, alts, fmt);

    case UAC_VERSION_2:
        return init_pitch_v2(uac, iface, alts, fmt);

    default:
        UAC_ERROR("%s : unsupport UAC version (%x)", __func__, altsd->bInterfaceProtocol);
        return -EINVAL;
    }
}

/* sample rate control */

static int set_sample_rate_v1(struct uac_device *uac, int iface,
                  struct usb_host_interface *alts,
                  struct audioformat *fmt, int rate)
{
    struct usb_device *dev = uac->udev;
    unsigned int ep;
    unsigned char data[3];
    int err;

    if (snd_usb_get_iface_desc(alts)->bNumEndpoints < 1)
        return -EINVAL;
    ep = snd_usb_get_ep_desc(alts, 0)->bEndpointAddress;

    /* if endpoint doesn't have sampling rate control, bail out */
    if (!(fmt->attributes & UAC_EP_CS_ATTR_SAMPLE_RATE))
        return 0;

    data[0] = rate;
    data[1] = rate >> 8;
    data[2] = rate >> 16;

    if ((err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC_SET_CUR,
                   USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_OUT,
                   UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep,
                   data, sizeof(data))) < 0) {
        UAC_ERROR("%d:%d:%d: cannot set freq %d to ep %#x",
                dev->devnum, iface, fmt->altsetting, rate, ep);
        return err;
    }

    if ((err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC_GET_CUR,
                   USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_IN,
                   UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep,
                   data, sizeof(data))) < 0) {
        UAC_ERROR("%d:%d:%d: cannot get freq at ep %#x",
                dev->devnum, iface, fmt->altsetting, ep);
        return 0;
    }

    return 0;
}

static int get_sample_rate_v2(struct uac_device *uac, int iface,
                  int altsetting, int clock)
{
    struct usb_device *dev = uac->udev;
    u32 data;
    int err;

    err = snd_usb_ctl_msg(dev, usb_rcvctrlpipe(dev, 0), UAC2_CS_CUR,
                  USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
                  UAC2_CS_CONTROL_SAM_FREQ << 8,
                  snd_usb_ctrl_intf(uac) | (clock << 8),
                  &data, sizeof(data));

    if (err < 0) {
        UAC_ERROR("%d:%d: cannot get freq (v2): err %d\n",
                iface, altsetting, err);
        return 0;
    }

    return data;
}

static int set_sample_rate_v2(struct uac_device *uac, int iface,
                  struct usb_host_interface *alts,
                  struct audioformat *fmt, int rate)
{
    struct usb_device *dev = uac->udev;
    u32 data;
    int err, cur_rate, prev_rate;
    int clock;
    bool writeable;
    u32 bmControls;

    clock = snd_usb_clock_find_source(uac, fmt->clock, true);
    if (clock < 0)
        return clock;

    prev_rate = get_sample_rate_v2(uac, iface, fmt->altsetting, clock);
    if (prev_rate == rate)
        return 0;

    struct uac_clock_source_descriptor *cs_desc;
    cs_desc = snd_usb_find_clock_source(uac->ctrl_intf, clock);
    bmControls = cs_desc->bmControls;

    writeable = uac_v2v3_control_is_writeable(bmControls, UAC2_CS_CONTROL_SAM_FREQ);
    if (writeable) {
        data = rate;
        err = snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), UAC2_CS_CUR,
                      USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
                      UAC2_CS_CONTROL_SAM_FREQ << 8,
                      snd_usb_ctrl_intf(uac) | (clock << 8),
                      &data, sizeof(data));
        if (err < 0)
            return err;

        cur_rate = get_sample_rate_v2(uac, iface, fmt->altsetting, clock);
    } else {
        cur_rate = prev_rate;
    }

    if (cur_rate != rate) {
        if (!writeable) {
            UAC_ERROR("%d:%d: freq mismatch (RO clock): req %d, clock runs @%d\n",
                    iface, fmt->altsetting, rate, cur_rate);
            return -ENXIO;
        }
    }

    /* Some devices doesn't respond to sample rate changes while the
     * interface is active. */
    if (rate != prev_rate) {
        usb_set_interface(dev, iface, 0);
        usb_set_interface(dev, iface, fmt->altsetting);
    }

    return 0;
}

static int snd_usb_init_sample_rate(struct uac_device *uac, int iface,
                 struct usb_host_interface *alts,
                 struct audioformat *fmt, int rate)
{
    struct usb_interface_descriptor *altsd = snd_usb_get_iface_desc(alts);

    switch (altsd->bInterfaceProtocol) {
    case UAC_VERSION_1:
        return set_sample_rate_v1(uac, iface, alts, fmt, rate);

    case UAC_VERSION_2:
        return set_sample_rate_v2(uac, iface, alts, fmt, rate);

    default:
        UAC_ERROR("%s : unsupport UAC version (%x)", __func__, altsd->bInterfaceProtocol);
        return -EINVAL;
    }
}

/* mute, volume control */

static int snd_usb_set_mute_control(struct usb_mixer_interface *mixer, int *is_mute)
{
    if (!mixer)
        return -EINVAL;

    struct uac_device *uac = (struct uac_device *) mixer->prv_data;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    u8 request = (mixer->protocol == UAC_VERSION_1) ? UAC_SET_CUR : UAC2_CS_CUR;
    u8 bmRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT;
    u16 value = (MUTE_CONTROL << 8);
    u16 index = (mixer->idx << 8) | mixer_ctrl_intf(mixer);
    unsigned int pipe = usb_sndctrlpipe(uac->udev, 0);
    int ret = 0, len = 0;

    len = usb_control_msg(uac->udev, pipe, request, bmRequestType, value,
                                    index, (void *)is_mute, 1, UAC_REQ_TIMEOUT);

    if (len != 1)
        ret = -EINVAL;

    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

static int snd_usb_set_volume_control_v1(struct usb_mixer_interface *mixer, u8 request, u8 channels, int *volume)
{
    struct uac_device *uac = (struct uac_device *) mixer->prv_data;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    u8 bmRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    u16 value = (VOLUME_CONTROL << 8) | channels;
    u16 index = (mixer->idx << 8) | mixer_ctrl_intf(mixer);
    int val_len = 2;        /* UAC_FU_VOLUME default data type: USB_MIXER_S16 */
    unsigned int pipe;
    unsigned char buf[4];
    int ret = 0, len = 0;

    if (request & USB_DIR_IN) {
        pipe = usb_rcvctrlpipe(uac->udev, 0);
        bmRequestType |= USB_DIR_IN;

        memset(buf, 0, sizeof(buf));
    } else {
        pipe = usb_sndctrlpipe(uac->udev, 0);
        bmRequestType |= USB_DIR_OUT;

        buf[0] = *volume & 0xff;
        buf[1] = (*volume >> 8) & 0xff;
        buf[2] = (*volume >> 16) & 0xff;
        buf[3] = (*volume >> 24) & 0xff;
    }

    len = usb_control_msg(uac->udev, pipe, request, bmRequestType,
                value, index, buf, val_len, UAC_REQ_TIMEOUT);

    if (len >= val_len) {
        int vol = snd_usb_combine_bytes(buf, val_len);
        vol &= 0xffff;
        if (vol > 0x8000)       /* minus db: 0x8000 ~ 0xffff */
            vol -= 0x10000;
        *volume = vol;
    } else {
        ret = -EINVAL;
    }

    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

static int snd_usb_set_volume_control_v2(struct usb_mixer_interface *mixer, u8 request, u8 channels, int *volume)
{
    /* Range (max/min/res) setting not yet supported */
    if (!(request & UAC__CUR) && !(request & UAC_GET_))
        return -EINVAL;

    struct uac_device *uac = (struct uac_device *) mixer->prv_data;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    u8 bmRequestType = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    u8 bRequest;
    u16 value = (VOLUME_CONTROL << 8) | channels;
    u16 index = (mixer->idx << 8) | mixer_ctrl_intf(mixer);
    unsigned int pipe;
    unsigned char buf[sizeof(u16) + 3 * sizeof(u32)];
    unsigned char *val = NULL;
    int val_size = 2;        /* UAC_FU_VOLUME default data type: USB_MIXER_S16 */
    int len, vol, ret = 0;

    if (request & USB_DIR_IN) {
        pipe = usb_rcvctrlpipe(uac->udev, 0);
        bmRequestType |= USB_DIR_IN;

        memset(buf, 0, sizeof(buf));
    } else {
        pipe = usb_sndctrlpipe(uac->udev, 0);
        bmRequestType |= USB_DIR_OUT;

        buf[0] = *volume & 0xff;
        buf[1] = (*volume >> 8) & 0xff;
        buf[2] = (*volume >> 16) & 0xff;
        buf[3] = (*volume >> 24) & 0xff;
    }

    if (request & UAC__CUR) {
        bRequest = UAC2_CS_CUR;
        len = val_size;
    } else {
        bRequest = UAC2_CS_RANGE;
        len = sizeof(u16) + 3 * val_size;
    }

    ret = usb_control_msg(uac->udev, pipe, bRequest, bmRequestType,
                value, index, buf, len, UAC_REQ_TIMEOUT);

    if (ret < 0)
        goto unlock;

    switch (request) {
    case UAC_GET_CUR:
        val = buf;
        break;
    case UAC_GET_MIN:
        val = buf + sizeof(u16);
        break;
    case UAC_GET_MAX:
        val = buf + sizeof(u16) + val_size;
        break;
    case UAC_GET_RES:
        val = buf + sizeof(u16) + val_size * 2;
        break;
    case UAC_SET_CUR:
        goto unlock;
    default:
        ret = -EINVAL;
        goto unlock;
    }

    vol = snd_usb_combine_bytes(val, val_size);
    vol &= 0xffff;
    if (vol >= 0x8000)      /* minus db */
        vol -= 0x10000;

    *volume = vol;

unlock:
    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

static int snd_usb_set_volume_control(struct usb_mixer_interface *mixer, u8 request, u8 channels, int *volume)
{
    if (!mixer)
        return -EINVAL;

    int ret = 0;

    switch (mixer->protocol)
    {
    case UAC_VERSION_1:
        ret = snd_usb_set_volume_control_v1(mixer, request, channels, volume);
        break;

    case UAC_VERSION_2:
        ret = snd_usb_set_volume_control_v2(mixer, request, channels, volume);
        break;

    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}

/* urb */

static void copy_to_urb(struct snd_usb_substream *subs, struct urb *urb,
            int offset, int stride, unsigned int bytes)
{
    struct snd_usb_endpoint *ep = subs->data_endpoint;
    unsigned long flags;
    int avail_bytes = 0;

    usb_spin_lock_irqsave(&ep->lock, flags);

    avail_bytes = ring_buffer_used_size(&ep->reader);
    if (bytes > avail_bytes)
        bytes = avail_bytes;

    bytes = ring_buffer_read(&ep->reader, urb->transfer_buffer + offset, bytes);

    usb_spin_unlock_irqrestore(&ep->lock, flags);

    subs->hwptr_done += bytes;
    if (subs->hwptr_done >= ep->buffer_size * stride)
        subs->hwptr_done -= ep->buffer_size * stride;
}

static void retire_capture_urb(struct snd_usb_substream *subs,
                   struct urb *urb)
{
    unsigned int stride, frames, bytes, oldptr;
    unsigned long flags;
    unsigned char *cp;
    struct snd_usb_endpoint *ep = subs->data_endpoint;
    int i;

    stride = ep->stride;

    for (i = 0; i < urb->number_of_packets; i++) {
        cp = (unsigned char *)urb->transfer_buffer + urb->iso_frame_desc[i].offset;
        bytes = urb->iso_frame_desc[i].actual_length;
        frames = bytes / stride;
        bytes = frames * stride;

        if (bytes % (subs->sample_bits >> 3) != 0) {
            bytes = frames * stride;
        }

        usb_spin_lock_irqsave(&subs->lock, flags);

        oldptr = subs->hwptr_done;
        subs->hwptr_done += bytes;
        if (subs->hwptr_done >= ep->buffer_size * stride)
            subs->hwptr_done -= ep->buffer_size * stride;

        frames = (bytes + (oldptr % stride)) / stride;
        subs->transfer_done += frames;
        if (subs->transfer_done >= ep->period_size) {
            subs->transfer_done -= ep->period_size;
        }

        usb_spin_unlock_irqrestore(&subs->lock, flags);

        usb_spin_lock_irqsave(&ep->lock, flags);

        /* copy a data chunk */
        ring_buffer_write(&ep->writer, cp, bytes);

        usb_spin_unlock_irqrestore(&ep->lock, flags);
    }
}

static void prepare_playback_urb(struct snd_usb_substream *subs,
                 struct urb *urb)
{
    struct snd_usb_endpoint *ep = subs->data_endpoint;
    struct snd_urb_ctx *ctx = urb->context;
    unsigned int counts, bytes, frames = 0;
    int i, stride, period_elapsed = 0;
    unsigned long flags;

    stride = ep->stride;
    urb->number_of_packets = 0;

    usb_spin_lock_irqsave(&subs->lock, flags);
    subs->frame_limit += ep->max_urb_frames;

    for (i = 0; i < ctx->packets; i++) {
        if (ctx->packet_size[i])
            counts = ctx->packet_size[i];
        else
            counts = snd_usb_endpoint_next_packet_size(ep);

        /* set up descriptor */
        urb->iso_frame_desc[i].offset = frames * ep->stride;
        urb->iso_frame_desc[i].length = counts * ep->stride;
        frames += counts;
        urb->number_of_packets++;
        subs->transfer_done += counts;
        if (subs->transfer_done >= subs->data_endpoint->period_size) {
            subs->transfer_done -= subs->data_endpoint->period_size;
            subs->frame_limit = 0;
            period_elapsed = 1;
        }

        /* finish at the period boundary or after enough frames */
        if ((period_elapsed ||
                subs->transfer_done >= subs->frame_limit))
            break;
    }
    bytes = frames * ep->stride;

    /* usual PCM */
    copy_to_urb(subs, urb, 0, stride, bytes);

    usb_spin_unlock_irqrestore(&subs->lock, flags);

    urb->transfer_buffer_length = bytes;
}

static void retire_playback_urb(struct snd_usb_substream *subs,
                   struct urb *urb)
{
    unsigned long flags;
    struct snd_usb_endpoint *ep = subs->data_endpoint;
    int processed = urb->transfer_buffer_length / ep->stride;
    int used_size = 0;

    if (!processed)
        return;

    usb_spin_lock_irqsave(&ep->lock, flags);
    used_size = ring_buffer_used_size(&ep->reader);
    usb_spin_unlock_irqrestore(&ep->lock, flags);

    /* update trigger state */
    if (used_size <= 0)
        snd_usb_endpoint_set_ops(ep, SND_USB_ENDPOINT_STATE_PAUSE);
    else if (used_size >= ep->period_size)
        snd_usb_endpoint_set_ops(ep, SND_USB_ENDPOINT_STATE_START);
}

/* endpoint */

static int start_endpoints(struct snd_usb_substream *subs)
{
    int err;

    if (!subs->data_endpoint)
        return -EINVAL;

    if (!(subs->flags & BIT(SUBSTREAM_FLAG_DATA_EP_STARTED))) {
        set_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags);

        struct snd_usb_endpoint *ep = subs->data_endpoint;
        ep->data_subs = subs;

        err = snd_usb_endpoint_start(ep);
        if (err < 0) {
            clear_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags);
            return err;
        }
    }

    return 0;
}

static void stop_endpoints(struct snd_usb_substream *subs, bool wait)
{
    if (!subs->data_endpoint)
        return;

    if (test_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags)){
        clear_bit(SUBSTREAM_FLAG_DATA_EP_STARTED, &subs->flags);

        snd_usb_endpoint_stop(subs->data_endpoint);
    }

    if (wait) {
        snd_usb_endpoint_pending_stop(subs->data_endpoint);
    }
}

static int configure_endpoint(struct snd_usb_substream *subs)
{
    int ret;

    stop_endpoints(subs, true);

    ret = snd_usb_endpoint_set_params(subs->data_endpoint,
                      subs->pcm_format,
                      subs->channels,
                      subs->period_bytes,
                      subs->period_frames,
                      subs->buffer_periods,
                      subs->cur_rate,
                      subs->cur_audiofmt);

    return ret;
}

void snd_usb_endpoint_set_ops(struct snd_usb_endpoint *ep, int state)
{
    switch (ep->direction)
    {
    case SND_USB_ENDPOINT_DIR_OUT: {
        switch (state)
        {
        case SND_USB_ENDPOINT_STATE_START:
            ep->prepare_data_urb = prepare_playback_urb;
            ep->retire_data_urb = retire_playback_urb;
            break;

        case SND_USB_ENDPOINT_STATE_STOP:
            ep->prepare_data_urb = NULL;
            ep->retire_data_urb = NULL;
            break;

        case SND_USB_ENDPOINT_STATE_PAUSE:
            ep->prepare_data_urb = NULL;
            ep->retire_data_urb = retire_playback_urb;
            break;
        }
        break;
    }
    case SND_USB_ENDPOINT_DIR_IN: {
        switch (state)
        {
        case SND_USB_ENDPOINT_STATE_START:
        case SND_USB_ENDPOINT_STATE_PAUSE:
            ep->retire_data_urb = retire_capture_urb;
            break;

        case SND_USB_ENDPOINT_STATE_STOP:
            ep->retire_data_urb = NULL;
            break;
        }
        break;
    }
    }
}

/* format */

static struct audioformat *find_format(struct snd_usb_substream *subs)
{
    struct audioformat *fp;
    struct audioformat *found = NULL;
    int cur_attr = 0, attr;

    list_for_each_entry(fp, &subs->fmt_list, list) {
        if (!(fp->formats & pcm_format_to_bits(subs->pcm_format)))
            continue;

        if (fp->channels != subs->channels)
            continue;

        if (subs->cur_rate < fp->rate_min ||
            subs->cur_rate > fp->rate_max)
            continue;

        if (! (fp->rates & SNDRV_PCM_RATE_CONTINUOUS)) {
            unsigned int i;
            for (i = 0; i < fp->nr_rates; i++)
                if (fp->rate_table[i] == subs->cur_rate)
                    break;
            if (i >= fp->nr_rates)
                continue;
        }

        attr = fp->ep_attr & USB_ENDPOINT_SYNCTYPE;
        if (!found) {
            found = fp;
            cur_attr = attr;
            continue;
        }
        /* avoid async out and adaptive in if the other method
         * supports the same format.
         * this is a workaround for the case like
         * M-audio audiophile USB.
         */
        if (attr != cur_attr) {
            if ((attr == USB_ENDPOINT_SYNC_ASYNC &&
                 subs->direction == UAC_SUBSTREAM_PLAYBACK) ||
                (attr == USB_ENDPOINT_SYNC_ADAPTIVE &&
                 subs->direction == UAC_SUBSTREAM_CAPTURE))
                continue;
            if ((cur_attr == USB_ENDPOINT_SYNC_ASYNC &&
                 subs->direction == UAC_SUBSTREAM_PLAYBACK) ||
                (cur_attr == USB_ENDPOINT_SYNC_ADAPTIVE &&
                 subs->direction == UAC_SUBSTREAM_CAPTURE)) {
                found = fp;
                cur_attr = attr;
                continue;
            }
        }
        /* find the format with the largest max. packet size */
        if (fp->maxpacksize > found->maxpacksize) {
            found = fp;
            cur_attr = attr;
        }
    }

    return found;
}

static int set_format(struct snd_usb_substream *subs, struct audioformat *fp)
{
    struct usb_interface *iface = usb_ifnum_to_if(subs->dev, fp->iface);
    struct usb_host_interface *alts = &iface->altsetting[fp->altset_idx];
    int ret = 0;

    if (fp == subs->cur_audiofmt)
        return 0;

    /* close the old interface */
    if (subs->ifnum >= 0 && (subs->ifnum != fp->iface)) {
        ret = usb_set_interface(subs->dev, subs->ifnum, 0);
        if (ret < 0) {
            UAC_ERROR("%d:%d: return to setting 0 failed (%d)",
                    fp->iface, fp->altsetting, ret);
            return -EIO;
        }
        subs->ifnum = -1;
    }

    /* set interface */
    if (iface->cur_altsetting != alts) {
        ret = usb_set_interface(subs->dev, fp->iface, fp->altsetting);
        if (ret < 0) {
            UAC_ERROR("%d:%d: usb_set_interface failed (%d)",
                    fp->iface, fp->altsetting, ret);
            return -EIO;
        }
    }

    subs->ifnum = fp->iface;
    subs->data_endpoint = snd_usb_add_endpoint(subs->stream->uac,
                            alts, fp->endpoint, subs->direction,
                            SND_USB_ENDPOINT_TYPE_DATA);

    if (!subs->data_endpoint)
        return -EINVAL;

    ret = snd_usb_init_pitch(subs->stream->uac, fp->iface, alts, fp);
    if (ret < 0)
        return ret;

    subs->cur_audiofmt = fp;

    return ret;
}

static void clear_format(struct snd_usb_substream *subs)
{
    subs->cur_audiofmt = NULL;
    subs->cur_rate = 0;
    subs->period_bytes = 0;
}

/* main func */

static int snd_usb_substream_init(struct snd_usb_substream *subs, struct pcm_params *params)
{
    if (!subs->dev || !subs->stream || !params)
        return -EINVAL;

    struct audioformat *fp;
    struct uac_device *uac;
    int frame_size = 0;
    int ret = 0;

    uac = subs->stream->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    subs->ifnum = -1;
    subs->channels = params->channels;
    subs->cur_rate = pcm_data_sample_rate(params->pcm_sample_rate);
    subs->pcm_format = params->pcm_data_fmt;
    subs->sample_bits = pcm_data_sample_size(params->pcm_data_fmt) * UAC_BITS_PER_BYTE;

    fp = find_format(subs);
    if (!fp) {
        ret = -ENODATA;
        goto unlock;
    }

    frame_size = subs->channels * subs->sample_bits / UAC_BITS_PER_BYTE;
    subs->buffer_periods = params->buffer_time_ms / params->period_time_ms;
    subs->period_bytes = params->period_time_ms * subs->cur_rate * frame_size / 1000;
    subs->period_frames = subs->period_bytes / frame_size;

    ret = set_format(subs, fp);

unlock:
    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

static int snd_usb_substream_deinit(struct snd_usb_substream *subs)
{
    if (!subs->dev || !subs->stream)
        return -EINVAL;

    struct uac_device *uac = subs->stream->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    clear_format(subs);

    stop_endpoints(subs, true);
    snd_usb_endpoint_deactivate(subs->data_endpoint);

    usb_set_interface(subs->dev, subs->ifnum, 0);
    subs->ifnum = -1;

    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return 0;
}

static int uac_playback_write_frame(struct pcm_dev_data *dev, void *buf, int frame_count, unsigned int timeout_ms)
{
    if (!dev || !buf || frame_count <= 0)
        return -EINVAL;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;
    struct snd_usb_endpoint *ep;
    int frame_size, transfer_bytes, free_size;
    unsigned long flags;
    int ret = 0;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return -ENODEV;

    uac = as->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    subs = &(as->substream[UAC_SUBSTREAM_PLAYBACK]);
    if (!subs->dev) {
        ret = -ENODEV;
        goto unlock_dev;
    }

    ep = subs->data_endpoint;
    if (!ep) {
        ret = -ENODEV;
        goto unlock_dev;
    }

    frame_size = pcm_data_sample_size(subs->pcm_format) * subs->channels;
    transfer_bytes = frame_size * frame_count;
    transfer_bytes = transfer_bytes - (transfer_bytes % frame_size);

    usb_spin_lock_irqsave(&ep->lock, flags);

    free_size = ring_buffer_free_size(&ep->reader);
    free_size = free_size - (free_size % frame_size);
    if (!free_size) {
        transfer_bytes = 0;
        goto unlock_ep;
    }

    if (transfer_bytes > free_size)
        transfer_bytes = free_size;
    ring_buffer_write(&ep->writer, buf, transfer_bytes);

unlock_ep:
    usb_spin_unlock_irqrestore(&ep->lock, flags);

    ret = transfer_bytes / frame_size;

unlock_dev:
    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

static int uac_capture_read_frame(struct pcm_dev_data *dev,
        void *buf, int frame_count, unsigned int timeout_ms)
{
    if (!dev || !buf || frame_count <= 0)
        return -EINVAL;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;
    struct snd_usb_endpoint *ep;
    int frame_size, transfer_bytes, avail_bytes = 0;
    unsigned long flags;
    int ret = 0;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return -ENODEV;

    uac = as->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    subs = &(as->substream[UAC_SUBSTREAM_CAPTURE]);
    if (!subs->dev) {
        ret = -ENODEV;
        goto unlock_dev;
    }

    ep = subs->data_endpoint;
    if (!ep) {
        ret = -ENODEV;
        goto unlock_dev;
    }

    frame_size = pcm_data_sample_size(subs->pcm_format) * subs->channels;
    transfer_bytes = frame_size * frame_count;
    transfer_bytes = transfer_bytes - (transfer_bytes % frame_size);

    usb_spin_lock_irqsave(&ep->lock, flags);

    avail_bytes = ring_buffer_used_size(&ep->reader);
    avail_bytes = avail_bytes - (avail_bytes % frame_size);
    if (avail_bytes <= 0) {
        transfer_bytes = 0;
        goto unlock_ep;
    }

    if (transfer_bytes > avail_bytes)
        transfer_bytes = avail_bytes;
    transfer_bytes = ring_buffer_read(&ep->reader, buf, transfer_bytes);

unlock_ep:
    usb_spin_unlock_irqrestore(&ep->lock, flags);

    ret = transfer_bytes / frame_size;

unlock_dev:
    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

/* pcm ops */

static void uac_pcm_remove(struct pcm_dev_data *dev)
{
    if (!dev || !dev->name)
        return;

    struct snd_usb_stream *as;
    struct pcm_device *pcm_dev;
    struct uac_device *uac;
    int minor;

    pcm_dev = pcm_get(dev->name);
    if (!pcm_dev)
        return;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return;

    uac = as->uac;
    if (!uac)
        return;

    minor = uac->minor;

    mutex_lock(&device_lock[minor]);

    if (pcm_dev->is_exiting) {
        pcm_unregister(dev);
        snd_usb_pcm_free(dev);

        if (--as->opened_pcm == 0) {
            free(as);

            if (--uac->opened_stream == 0) {
                free(uac);
                uac_table[minor] = NULL;
            }
        }
    }

    mutex_unlock(&device_lock[minor]);
}

static int uac_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    if (!dev || !params)
        return -EINVAL;

    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;
    int ret = 0;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as || !as->uac)
        return -ENODEV;

    subs = &(as->substream[dev->stream_type]);
    if (!subs->dev)
        return -ENODEV;

    ret = snd_usb_substream_init(subs, params);
    if (ret < 0)
        return ret;

    return ret;
}

static void uac_pcm_disable(struct pcm_dev_data *dev)
{
    if (!dev)
        return;

    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as || !as->uac)
        return;

    subs = &(as->substream[dev->stream_type]);
    if (!subs->dev)
        return;

    snd_usb_substream_deinit(subs);
}

static int uac_pcm_start(struct pcm_dev_data *dev)
{
    if (!dev)
        return -EINVAL;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;
    struct audioformat *fp;
    struct usb_interface *iface;
    struct usb_host_interface *alts;
    int ret = 0, len = 0;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return -ENODEV;

    uac = as->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    subs = &(as->substream[dev->stream_type]);
    if (!subs->dev || !subs->cur_audiofmt) {
        ret = -ENODEV;
        goto unlock;
    }

    fp = subs->cur_audiofmt;
    iface = usb_ifnum_to_if(uac->udev, fp->iface);
    alts = &iface->altsetting[fp->altset_idx];

    ret = snd_usb_init_sample_rate(uac, fp->iface, alts, fp, subs->cur_rate);
    if (ret < 0)
        goto unlock;

    ret = configure_endpoint(subs);
    if (ret < 0)
        goto unlock;

    /* alloc ring buffer */
    len = subs->data_endpoint->buffer_size * subs->data_endpoint->stride;
    ret = snd_usb_endpoint_alloc_buffer(subs->data_endpoint, len);
    if (ret < 0)
        goto unlock;

    subs->data_endpoint->maxframesize =
        bytes_to_frames(subs->data_endpoint, subs->data_endpoint->maxpacksize);
    subs->data_endpoint->curframesize =
        bytes_to_frames(subs->data_endpoint, subs->data_endpoint->curpacksize);

    subs->hwptr_done = 0;
    subs->transfer_done = 0;

    ret = start_endpoints(subs);
    if (ret < 0)
        snd_usb_endpoint_free_buffer(subs->data_endpoint);

unlock:
    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);

    return ret;
}

static void uac_pcm_stop(struct pcm_dev_data *dev)
{
    if (!dev)
        return;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return;

    uac = as->uac;
    if (!UAC_READY(uac))
        return;

    mutex_lock(&device_lock[uac->minor]);
    uac->used++;
    mutex_unlock(&device_lock[uac->minor]);

    subs = &(as->substream[dev->stream_type]);
    if (!subs->dev)
        goto unlock;

    stop_endpoints(subs, false);

    snd_usb_endpoint_free_buffer(subs->data_endpoint);

unlock:
    mutex_lock(&device_lock[uac->minor]);
    uac->used--;
    if (uac->exiting && !uac->used)
        thread_cond_signal(&free_cond[uac->minor]);
    mutex_unlock(&device_lock[uac->minor]);
}

static int uac_pcm_set_mute(struct pcm_dev_data *dev, int mute)
{
    if (!dev)
        return -EINVAL;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct usb_mixer_interface *mixer;
    int is_mute = mute;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return -ENODEV;

    uac = as->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mixer = uac->mixer;
    if (!mixer)
        return 0;

    if (snd_usb_set_mute_control(mixer, &is_mute) < 0)
        return -EINVAL;

    return 0;
}

static int uac_pcm_set_volume(struct pcm_dev_data *dev, int val)
{
    if (!dev)
        return -EINVAL;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;
    struct usb_mixer_interface *mixer;
    int volume = val, ch;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return -ENODEV;

    uac = as->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    subs = &(as->substream[dev->stream_type]);
    if (!subs->dev)
        return -ENODEV;

    mixer = uac->mixer;
    if (!mixer)
        return 0;

    volume = (volume < 0) ? 0 : volume;
    volume = (volume > 100) ? 100 : volume;
    volume = volume * (mixer->vol_max - mixer->vol_min) / 100 + mixer->vol_min;

    for (ch = 1; ch <= mixer->channels; ch++) {
        if (snd_usb_set_volume_control(mixer, UAC_SET_CUR, ch, &volume) < 0)
            return -EINVAL;
    }

    return 0;
}

static int uac_pcm_get_volume(struct pcm_dev_data *dev)
{
    if (!dev)
        return -EINVAL;

    struct uac_device *uac;
    struct snd_usb_stream *as;
    struct usb_mixer_interface *mixer;
    int vol = 0;

    as = (struct snd_usb_stream *) dev->drv_data;
    if (!as)
        return -ENODEV;

    uac = as->uac;
    if (!UAC_READY(uac))
        return -ENODEV;

    mixer = uac->mixer;
    if (!mixer)
        return 0;

    if (snd_usb_set_volume_control(mixer, UAC_GET_CUR, DEFAULT_CHANNEL, &vol) < 0)
        return -EINVAL;

    vol = (vol - mixer->vol_min) * 100 / (mixer->vol_max - mixer->vol_min);

    return vol;
}

/* for uac device init */

int snd_usb_mixer_create(struct uac_device *dev, int ctrlif)
{
    int ret = 0;
    struct usb_host_interface *alts;
    struct uac_feature_unit_descriptor *fu;
    struct usb_mixer_interface *mixer;
    int csize;

    dev->mixer = NULL;

    alts = dev->ctrl_intf;
    fu = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_FEATURE_UNIT);
    if (!fu) {
        UAC_INFO("UAC(%d): descriptor of feature unit not found", dev->minor);
        return 0;
    }

    mixer = malloc(sizeof(*mixer));
    if (!mixer)
        return -ENOMEM;
    memset(mixer, 0, sizeof(*mixer));

    mixer->prv_data = dev;
    mixer->hostif = &usb_ifnum_to_if(dev->udev, ctrlif)->altsetting[0];
    mixer->idx = fu->bUnitID;
    mixer->protocol = snd_usb_get_iface_desc(mixer->hostif)->bInterfaceProtocol;

    switch (mixer->protocol) {
    case UAC_VERSION_1: {
        csize = fu->bControlSize;
        mixer->channels = (fu->bLength - 7) / csize - 1;
        break;
    }
    case UAC_VERSION_2: {
        csize = 4;
        mixer->channels = (fu->bLength - 6) / csize - 1;
        break;
    }
    default: {
        ret = -EINVAL;
        goto err_out;
    }
    }

    ret = snd_usb_set_volume_control(mixer, UAC_GET_MIN, DEFAULT_CHANNEL, &(mixer->vol_min));
    if (ret < 0)
        goto err_out;

    ret = snd_usb_set_volume_control(mixer, UAC_GET_MAX, DEFAULT_CHANNEL, &(mixer->vol_max));
    if (ret < 0)
        goto err_out;

    if (mixer->vol_max <= mixer->vol_min) {
        ret = -ERANGE;
        goto err_out;
    }

    ret = snd_usb_set_volume_control(mixer, UAC_GET_CUR, DEFAULT_CHANNEL, &(mixer->vol_cur));
    if (ret < 0)
        goto err_out;

    dev->mixer = mixer;
    return 0;

err_out:
    free(mixer);
    return ret;
}

void snd_usb_mixer_delete(struct uac_device *uac)
{
    if (!uac->mixer)
        return;

    free(uac->mixer);
    uac->mixer = NULL;
}

int snd_usb_pcm_new(struct snd_usb_stream *as, int stream)
{
    struct snd_usb_substream *subs = &(as->substream[stream]);
    struct pcm_dev_data *pcm;
    struct pcm_device *pcm_dev;
    struct audioformat *fp;
    int is_playback = (stream == UAC_SUBSTREAM_PLAYBACK) ? 1 : 0;

    pcm = malloc(sizeof(*pcm));
    if (pcm == NULL)
        return -ENOMEM;
    memset(pcm, 0, sizeof(*pcm));

    pcm->drv_data = as;
    pcm->stream_type = is_playback ? pcm_stream_playback : pcm_stream_capture;

    /* pre-defined */
    pcm->i2s_frame_mode_list = 0;
    pcm->i2s_bclk_direction_list = 0;
    pcm->i2s_frame_direction_list = 0;
    pcm->pcm_interface_list = 0;

    list_for_each_entry(fp, &subs->fmt_list, list) {
        if (subs->ep_num != fp->endpoint)
            continue;
        pcm->channels_list |= BIT(fp->channels);
        pcm->pcm_data_fmt_list |= fp->formats;
        pcm->pcm_sample_rate_list |= fp->rates;
    }

    pcm->name                           = subs->pcm_name;
    pcm->pcm_enable                     = uac_pcm_enable;
    pcm->pcm_disable                    = uac_pcm_disable;
    pcm->pcm_start                      = uac_pcm_start;
    pcm->pcm_stop                       = uac_pcm_stop;
    pcm->pcm_get_volume                 = uac_pcm_get_volume;
    pcm->pcm_set_volume                 = uac_pcm_set_volume;
    pcm->pcm_set_mute                   = uac_pcm_set_mute;
    pcm->pcm_remove                     = uac_pcm_remove;

    if (is_playback) {
        pcm->pcm_write_frame            = uac_playback_write_frame;
    } else {
        pcm->pcm_read_frame             = uac_capture_read_frame;
    }

    pcm_dev = pcm_register(pcm);
    if (!pcm_dev) {
        free(pcm);
        subs->pcm = NULL;
        return -ENODEV;
    }

    subs->pcm = pcm;
    return 0;
}

void snd_usb_pcm_free(struct pcm_dev_data *pcm)
{
    if (pcm)
        free(pcm);
}

void snd_usb_init_interface(struct uac_device *dev, int iface_no, int altno,
               struct usb_host_interface *alts, struct audioformat *fp)
{
    int ret = 0;

    ret = usb_set_interface(dev->udev, iface_no, altno);
    if (ret < 0) {
        UAC_ERROR("USB UAC Device [%d]: set interface %d setting %d error",
            dev->minor, iface_no, altno);
        return;
    }

    ret = snd_usb_init_pitch(dev, iface_no, alts, fp);
    if (ret < 0) {
        UAC_ERROR("USB UAC Device [%d]: interface %d setting %d init pitch error",
            dev->minor, iface_no, altno);
        return;
    }

    ret = snd_usb_init_sample_rate(dev, iface_no, alts, fp, fp->rate_max);
    if (ret < 0) {
        UAC_ERROR("USB UAC Device [%d]: interface %d setting %d init sample rate error",
            dev->minor, iface_no, altno);
        return;
    }
}

void snd_usb_init_substream(struct snd_usb_stream *as, int stream, struct audioformat *fp)
{
    struct uac_device *uac = as->uac;
    struct usb_interface *iface = usb_ifnum_to_if(uac->udev, fp->iface);
    struct snd_usb_substream *subs = &as->substream[stream];

    memset(subs, 0, sizeof(*subs));

    spin_lock_init(&subs->lock);
    subs->dev = uac->udev;
    subs->stream = as;
    subs->direction = stream;
    subs->ep = &(iface->altsetting[fp->altset_idx].endpoint[0]);
    subs->ep_num = fp->endpoint;

    snprintf(subs->pcm_name, sizeof(subs->pcm_name), "uac%d-%d-%s",
        as->uac->minor, as->pcm_index,
        (stream == UAC_SUBSTREAM_PLAYBACK) ? "playback" : "capture");

    INIT_LIST_HEAD(&subs->fmt_list);
    list_add_tail(&fp->list, &subs->fmt_list);
    subs->num_formats++;
    subs->rate_table = fp->rate_table;
}
