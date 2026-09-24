#include "uacaudio.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"

#define EP_FLAG_RUNNING                 1
#define EP_FLAG_STOPPING                2

static unsigned get_usb_full_speed_rate(unsigned int rate)
{
    return ((rate << 13) + 62) / 125;
}

static unsigned get_usb_high_speed_rate(unsigned int rate)
{
    return ((rate << 10) + 62) / 125;
}

static void prepare_silent_urb(struct snd_usb_endpoint *ep,
                   struct snd_urb_ctx *ctx)
{
    struct urb *urb = ctx->urb;
    unsigned int offs = 0;
    int i;

    for (i = 0; i < ctx->packets; ++i) {
        unsigned int offset;
        unsigned int length;
        int counts;

        if (ctx->packet_size[i])
            counts = ctx->packet_size[i];
        else
            counts = snd_usb_endpoint_next_packet_size(ep);

        length = counts * ep->stride; /* number of silent bytes */
        offset = offs * ep->stride;
        urb->iso_frame_desc[i].offset = offset;
        urb->iso_frame_desc[i].length = length;

        memset(urb->transfer_buffer + offset,
               ep->silence_value, length);
        offs += counts;
    }

    urb->number_of_packets = ctx->packets;
    urb->transfer_buffer_length = offs * ep->stride;
}

static void prepare_outbound_urb(struct snd_usb_endpoint *ep,
                 struct snd_urb_ctx *ctx)
{
    struct urb *urb = ctx->urb;

    urb->dev = ep->dev->udev;

    switch (ep->type) {
    case SND_USB_ENDPOINT_TYPE_DATA:
        if (ep->prepare_data_urb) {
            ep->prepare_data_urb(ep->data_subs, urb);
        } else {
            /* no data provider, so send silence */
            prepare_silent_urb(ep, ctx);
        }
        break;

    case SND_USB_ENDPOINT_TYPE_SYNC:
        /* to do */
        break;
    }
}

static void prepare_inbound_urb(struct snd_usb_endpoint *ep,
                       struct snd_urb_ctx *urb_ctx)
{
    int i, offs;
    struct urb *urb = urb_ctx->urb;

    urb->dev = ep->dev->udev;

    switch (ep->type) {
    case SND_USB_ENDPOINT_TYPE_DATA:
        offs = 0;
        for (i = 0; i < urb_ctx->packets; i++) {
            urb->iso_frame_desc[i].offset = offs;
            urb->iso_frame_desc[i].length = ep->curpacksize;
            offs += ep->curpacksize;
        }

        urb->transfer_buffer_length = offs;
        urb->number_of_packets = urb_ctx->packets;
        break;

    case SND_USB_ENDPOINT_TYPE_SYNC:
        /* to do */
        break;
    }
}

static void retire_outbound_urb(struct snd_usb_endpoint *ep,
                struct snd_urb_ctx *urb_ctx)
{
    if (ep->retire_data_urb)
        ep->retire_data_urb(ep->data_subs, urb_ctx->urb);
}

static void retire_inbound_urb(struct snd_usb_endpoint *ep,
                   struct snd_urb_ctx *urb_ctx)
{
    struct urb *urb = urb_ctx->urb;

    if (ep->retire_data_urb)
        ep->retire_data_urb(ep->data_subs, urb);
}

static void snd_complete_urb(struct urb *urb)
{
    struct snd_urb_ctx *ctx = urb->context;
    struct snd_usb_endpoint *ep = ctx->ep;
    int err = 0;

    if (urb->status == -ENOENT ||       /* unlinked */
        urb->status == -ENODEV ||       /* device removed */
        urb->status == -ECONNRESET ||   /* unlinked */
        urb->status == -ESHUTDOWN)      /* device disabled */
        goto exit_clear;

    /* device disconnected */
    if (!ep->dev->udev || ep->dev->exiting)
        goto exit_clear;

    if (!test_bit(EP_FLAG_RUNNING, &ep->flags))
        goto exit_clear;

    if (usb_pipeout(ep->pipe)) {
        retire_outbound_urb(ep, ctx);
        /* can be stopped during retire callback */
        if (!test_bit(EP_FLAG_RUNNING, &ep->flags))
            goto exit_clear;

        prepare_outbound_urb(ep, ctx);

        /* can be stopped during prepare callback */
        if (!test_bit(EP_FLAG_RUNNING, &ep->flags))
            goto exit_clear;
    } else {
        retire_inbound_urb(ep, ctx);
        /* can be stopped during retire callback */
        if (!test_bit(EP_FLAG_RUNNING, &ep->flags))
            goto exit_clear;

        prepare_inbound_urb(ep, ctx);
    }

    err = usb_submit_urb(urb);
    if (err == 0)
        return;

exit_clear:
    clear_bit(ctx->index, &ep->active_mask);
}

static int wait_clear_urbs(struct snd_usb_endpoint *ep)
{
    clear_bit(EP_FLAG_STOPPING, &ep->flags);

    ep->data_subs = NULL;

    snd_usb_endpoint_set_ops(ep, SND_USB_ENDPOINT_STATE_STOP);

    return 0;
}

static int deactivate_urbs(struct snd_usb_endpoint *ep)
{
    unsigned int i;

    clear_bit(EP_FLAG_RUNNING, &ep->flags);

    for (i = 0; i < ep->nurbs; i++) {
        if (test_bit(i, &ep->active_mask)) {
            if (!test_bit(i, &ep->unlink_mask)) {
                set_bit(i, &ep->unlink_mask);

                struct urb *u = ep->urb[i].urb;
                usb_unlink_urb(u);
            }
        }
    }

    return 0;
}

static void release_urb_ctx(struct snd_urb_ctx *u)
{
    if (u->urb && u->buffer_size)
        usb_free_coherent(u->urb->dev, u->buffer_size, u->urb->transfer_buffer);
    usb_free_urb(u->urb);
    u->urb = NULL;
    u->buffer_size = 0;
}

static void release_urbs(struct snd_usb_endpoint *ep)
{
    int i;

    snd_usb_endpoint_set_ops(ep, SND_USB_ENDPOINT_STATE_STOP);

    /* stop urbs */
    deactivate_urbs(ep);
    wait_clear_urbs(ep);

    for (i = 0; i < ep->nurbs; i++)
        release_urb_ctx(&ep->urb[i]);

    ep->nurbs = 0;
}

static int data_ep_set_params(struct snd_usb_endpoint *ep,
                  pcm_data_fmt pcm_format,
                  unsigned int channels,
                  unsigned int period_bytes,
                  unsigned int frames_per_period,
                  unsigned int periods_per_buffer,
                  struct audioformat *fmt)
{
    unsigned int maxsize, minsize, packs_per_ms, max_packs_per_urb;
    unsigned int max_packs_per_period, urbs_per_period, urb_packs;
    unsigned int max_urbs, i, bits;
    unsigned long frames;

    ep->channels = channels;
    bits = pcm_data_sample_size(pcm_format) * UAC_BITS_PER_BYTE;
    ep->sample_bits = bits;
    bits *= channels;
    ep->frame_bits = bits;
    ep->datainterval = fmt->datainterval;
    ep->period_size = frames_per_period * ep->frame_bits / UAC_BITS_PER_BYTE;
    ep->buffer_size = ep->period_size * periods_per_buffer;

    frames = 1;
    while (bits % 8 != 0) {
        bits *= 2;
        frames *= 2;
    }

    ep->stride = ep->frame_bits >> 3;
    ep->silence_value = (pcm_format == pcm_fmt_U8) ? 0x80 : 0;

    /* assume max. frequency is 25% higher than nominal */
    ep->freqmax = ep->freqn + (ep->freqn >> 2);
    maxsize = (((ep->freqmax << ep->datainterval) + 0xffff) >> 16) *
             (ep->frame_bits >> 3);
    /* but wMaxPacketSize might reduce this */
    if (ep->maxpacksize && ep->maxpacksize < maxsize) {
        /* whatever fits into a max. size packet */
        unsigned int data_maxsize = maxsize = ep->maxpacksize;
        ep->freqmax = (data_maxsize / (ep->frame_bits >> 3))
                << (16 - ep->datainterval);
    }

    if (ep->fill_max)
        ep->curpacksize = ep->maxpacksize;
    else
        ep->curpacksize = maxsize;

    if (snd_usb_get_speed(ep->dev->udev) != USB_SPEED_FULL) {
        packs_per_ms = 8 >> ep->datainterval;
        max_packs_per_urb = UAC_MAX_PACKS_HS;
    } else {
        packs_per_ms = 1;
        max_packs_per_urb = UAC_MAX_PACKS;
    }

    max_packs_per_urb = max(1u, max_packs_per_urb >> ep->datainterval);

    if (usb_pipein(ep->pipe)) {
        urb_packs = packs_per_ms;

        /* make capture URBs <= 1 ms and smaller than a period */
        urb_packs = min(max_packs_per_urb, urb_packs);
        while (urb_packs > 1 && urb_packs * maxsize >= period_bytes)
            urb_packs >>= 1;
        ep->nurbs = UAC_MAX_URBS;

    } else {
        /* determine how small a packet can be */
        minsize = (ep->freqn >> (16 - ep->datainterval)) *
                (ep->frame_bits >> 3);
        /* with sync from device, assume it can be 12% lower */
        minsize = max(minsize, 1u);

        /* how many packets will contain an entire ALSA period? */
        max_packs_per_period = DIV_ROUND_UP(period_bytes, minsize);

        /* how many URBs will contain a period? */
        urbs_per_period = DIV_ROUND_UP(max_packs_per_period,
                max_packs_per_urb);
        /* how many packets are needed in each URB? */
        urb_packs = DIV_ROUND_UP(max_packs_per_period, urbs_per_period);

        /* limit the number of frames in a single URB */
        ep->max_urb_frames = DIV_ROUND_UP(frames_per_period,
                    urbs_per_period);

        /* try to use enough URBs to contain an entire ALSA buffer */
        max_urbs = min((unsigned) UAC_MAX_URBS,
                UAC_MAX_QUEUE * packs_per_ms / urb_packs);
        ep->nurbs = min(max_urbs, urbs_per_period * periods_per_buffer);
    }

    /* allocate and initialize data urbs */
    for (i = 0; i < ep->nurbs; i++) {
        struct snd_urb_ctx *u = &ep->urb[i];
        u->index = i;
        u->ep = ep;
        u->packets = urb_packs;
        u->buffer_size = maxsize * u->packets;

        u->urb = usb_alloc_urb(u->packets);
        if (!u->urb)
            goto out_of_memory;

        u->urb->transfer_buffer = cache_align_malloc(u->buffer_size);
        if (!u->urb->transfer_buffer)
            goto out_of_memory;
        memset(u->urb->transfer_buffer, 0, u->buffer_size);

        u->urb->pipe = ep->pipe;
        u->urb->transfer_flags = URB_ISO_ASAP;
        u->urb->interval = 1 << ep->datainterval;
        u->urb->context = u;
        u->urb->complete = snd_complete_urb;
    }

    return 0;

out_of_memory:
    release_urbs(ep);
    return -ENOMEM;
}

struct snd_usb_endpoint *snd_usb_add_endpoint(struct uac_device *dev,
                          struct usb_host_interface *alts,
                          int ep_num, int direction, int type)
{
    struct snd_usb_endpoint *ep = NULL;
    int is_playback = (direction == UAC_SUBSTREAM_PLAYBACK);

    list_for_each_entry(ep, &dev->ep_list, list) {
        if (ep->ep_num == ep_num &&
            ep->iface == alts->desc.bInterfaceNumber &&
            ep->altsetting == alts->desc.bAlternateSetting) {
            return ep;
        }
    }

    ep = malloc(sizeof(*ep));
    if (!ep)
        return NULL;
    memset(ep, 0, sizeof(*ep));

    ep->dev = dev;
    spin_lock_init(&ep->lock);
    ep->type = type;
    ep->ep_num = ep_num;
    ep->direction = is_playback ? SND_USB_ENDPOINT_DIR_OUT : SND_USB_ENDPOINT_DIR_IN;
    ep->iface = alts->desc.bInterfaceNumber;
    ep->altsetting = alts->desc.bAlternateSetting;
    ep_num &= USB_ENDPOINT_NUMBER_MASK;

    if (is_playback)
        ep->pipe = usb_sndisocpipe(dev->udev, ep_num);
    else
        ep->pipe = usb_rcvisocpipe(dev->udev, ep_num);

    list_add_tail(&ep->list, &dev->ep_list);

    return ep;
}

int snd_usb_endpoint_next_packet_size(struct snd_usb_endpoint *ep)
{
    unsigned long flags;
    int ret;

    if (ep->fill_max)
        return ep->maxframesize;

    usb_spin_lock_irqsave(&ep->lock, flags);
    ep->phase = (ep->phase & 0xffff)
        + (ep->freqm << ep->datainterval);
    ret = min(ep->phase >> 16, ep->maxframesize);
    usb_spin_unlock_irqrestore(&ep->lock, flags);

    return ret;
}

void snd_usb_endpoint_pending_stop(struct snd_usb_endpoint *ep)
{
    if (ep && test_bit(EP_FLAG_STOPPING, &ep->flags))
        wait_clear_urbs(ep);
}

int snd_usb_endpoint_set_params(struct snd_usb_endpoint *ep,
                pcm_data_fmt pcm_format,
                unsigned int channels,
                unsigned int period_bytes,
                unsigned int period_frames,
                unsigned int buffer_periods,
                unsigned int rate,
                struct audioformat *fmt)
{
    int err = 0;
    unsigned long flags;

    /* release old buffers, if any */
    release_urbs(ep);

    usb_spin_lock_irqsave(&ep->lock, flags);

    ep->rate = rate;
    ep->datainterval = fmt->datainterval;
    ep->maxpacksize = fmt->maxpacksize;
    ep->fill_max = !!(fmt->attributes & UAC_EP_CS_ATTR_FILL_MAX);

    if (snd_usb_get_speed(ep->dev->udev) == USB_SPEED_FULL)
        ep->freqn = get_usb_full_speed_rate(rate);
    else
        ep->freqn = get_usb_high_speed_rate(rate);

    /* calculate the frequency in 16.16 format */
    ep->freqm = ep->freqn;
    ep->phase = 0;
    ep->use_count = 0;

    switch (ep->type) {
    case SND_USB_ENDPOINT_TYPE_DATA:
        err = data_ep_set_params(ep, pcm_format, channels,
                     period_bytes, period_frames,
                     buffer_periods, fmt);
        break;
    case SND_USB_ENDPOINT_TYPE_SYNC:
        /* to do */
    default:
        err = -EINVAL;
    }

    usb_spin_unlock_irqrestore(&ep->lock, flags);

    return err;
}

int snd_usb_endpoint_alloc_buffer(struct snd_usb_endpoint *ep, int size)
{
    if (!ep || size <= 0)
        return -EINVAL;

    /* ring mem size must be a power of 2 */
    size = roundup_pow_of_two(size);

    if (ep->ring_area) {
        if (ep->ring_bytes >= size)
            goto reset_ring;

        free(ep->ring_area);
        ep->ring_bytes = 0;
    }

    ep->ring_area = cache_align_malloc(size);
    if (!ep->ring_area)
        return -ENOMEM;
    ep->ring_bytes = size;

reset_ring:
    memset(ep->ring_area, 0, size);
    ring_buffer_writer_init(&ep->writer, ep->ring_area, size);
    ring_buffer_reader_init(&ep->reader, &ep->writer);

    return 0;
}

void snd_usb_endpoint_free_buffer(struct snd_usb_endpoint *ep)
{
    if (!ep || !ep->ring_area)
        return;

    unsigned long flags;
    usb_spin_lock_irqsave(&ep->lock, flags);

    free(ep->ring_area);
    ep->ring_area = NULL;
    ep->ring_bytes = 0;

    usb_spin_unlock_irqrestore(&ep->lock, flags);
}

void snd_usb_endpoint_stop(struct snd_usb_endpoint *ep)
{
    if (!ep)
        return;

    if (ep->use_count == 0)
        return;

    if (--ep->use_count == 0) {
        deactivate_urbs(ep);
        set_bit(EP_FLAG_STOPPING, &ep->flags);
    }
}

void snd_usb_endpoints_release(struct uac_device *dev)
{
    struct snd_usb_endpoint *ep, *n1;

    if (list_empty(&dev->ep_list))
        return;

    list_for_each_entry_safe(ep, n1, &dev->ep_list, list) {
        list_del(&ep->list);

        release_urbs(ep);
        snd_usb_endpoint_free_buffer(ep);

        free(ep);
    }
}

void snd_usb_endpoint_deactivate(struct snd_usb_endpoint *ep)
{
    if (!ep)
        return;

    if (ep->use_count != 0)
        return;

    deactivate_urbs(ep);
    wait_clear_urbs(ep);
}

int snd_usb_endpoint_start(struct snd_usb_endpoint *ep)
{
    int err = 0;
    unsigned int i;

    /* check if device is disconnecting */
    if (!ep->dev->udev || ep->dev->exiting)
        return -ENODEV;

    /* already running? */
    if (++ep->use_count != 1)
        return 0;

    /* just to be sure */
    deactivate_urbs(ep);

    ep->active_mask = 0;
    ep->unlink_mask = 0;
    ep->phase = 0;

    set_bit(EP_FLAG_RUNNING, &ep->flags);

    snd_usb_endpoint_set_ops(ep, SND_USB_ENDPOINT_STATE_PAUSE);

    for (i = 0; i < ep->nurbs; i++) {
        struct urb *urb = ep->urb[i].urb;

        if (!urb)
            goto __error;

        if (usb_pipeout(ep->pipe))
            prepare_outbound_urb(ep, urb->context);
        else
            prepare_inbound_urb(ep, urb->context);

        err = usb_submit_urb(urb);
        if (err < 0)
            goto __error;

        set_bit(i, &ep->active_mask);
    }

    return 0;

__error:
    clear_bit(EP_FLAG_RUNNING, &ep->flags);
    ep->use_count--;
    deactivate_urbs(ep);
    return -EPIPE;
}
