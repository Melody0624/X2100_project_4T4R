#include "uacaudio.h"
#include "endpoint.h"
#include "helper.h"
#include "pcm.h"

#define BITS_TO_LONGS(nr)               DIV_ROUND_UP(nr, UAC_BITS_PER_BYTE * sizeof(long))
#define DECLARE_BITMAP(name,bits)       unsigned long name[BITS_TO_LONGS(bits)]

static struct usb_driver uac_driver;

/* get specific descriptor */

struct uac_clock_source_descriptor *
    snd_usb_find_clock_source(struct usb_host_interface *ctrl_iface, int clock_id)
{
    struct uac_clock_source_descriptor *cs = NULL;

    while ((cs = snd_usb_find_csint_desc(ctrl_iface->extra,
                         ctrl_iface->extralen,
                         cs, UAC2_CLOCK_SOURCE))) {
        if (cs->bClockID == clock_id)
            return cs;
    }

    return NULL;
}

static struct uac_clock_selector_descriptor *
    snd_usb_find_clock_selector(struct usb_host_interface *ctrl_iface,
                    int clock_id)
{
    struct uac_clock_selector_descriptor *cs = NULL;

    while ((cs = snd_usb_find_csint_desc(ctrl_iface->extra,
                         ctrl_iface->extralen,
                         cs, UAC2_CLOCK_SELECTOR))) {
        if (cs->bClockID == clock_id)
            return cs;
    }
    return NULL;
}

static struct uac_clock_multiplier_descriptor *
    snd_usb_find_clock_multiplier(struct usb_host_interface *ctrl_iface, int clock_id)
{
    struct uac_clock_multiplier_descriptor *cs = NULL;

    while ((cs = snd_usb_find_csint_desc(ctrl_iface->extra,
                         ctrl_iface->extralen,
                         cs, UAC2_CLOCK_MULTIPLIER))) {
        if (cs->bClockID == clock_id)
            return cs;
    }
    return NULL;
}

static struct uac2_input_terminal_descriptor *
    snd_usb_find_input_terminal_descriptor(struct usb_host_interface *ctrl_iface,
                        int terminal_id)
{
    struct uac2_input_terminal_descriptor *term = NULL;

    while ((term = snd_usb_find_csint_desc(ctrl_iface->extra,
                           ctrl_iface->extralen,
                           term, UAC_INPUT_TERMINAL))) {
        if (term && (term->bTerminalID == terminal_id))
            return term;
    }

    return NULL;
}

static struct uac2_output_terminal_descriptor *
    snd_usb_find_output_terminal_descriptor(struct usb_host_interface *ctrl_iface,
                        int terminal_id)
{
    struct uac2_output_terminal_descriptor *term = NULL;

    while ((term = snd_usb_find_csint_desc(ctrl_iface->extra,
                           ctrl_iface->extralen,
                           term, UAC_OUTPUT_TERMINAL))) {
        if (term->bTerminalID == terminal_id)
            return term;
    }

    return NULL;
}

/* clock */

static bool snd_usb_clock_source_is_valid(struct uac_device *dev, int source_id)
{
    int err;
    unsigned char data;
    struct usb_device *udev = dev->udev;
    struct uac_clock_source_descriptor *cs_desc =
        snd_usb_find_clock_source(dev->ctrl_intf, source_id);

    if (!cs_desc)
        return 0;

    if (!uac_v2v3_control_is_readable(cs_desc->bmControls, UAC2_CS_CONTROL_CLOCK_VALID - 1))
        return 1;

    err = snd_usb_ctl_msg(udev, usb_rcvctrlpipe(udev, 0), UAC2_CS_CUR,
                  USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
                  UAC2_CS_CONTROL_CLOCK_VALID << 8,
                  snd_usb_ctrl_intf(dev) | (source_id << 8),
                  &data, sizeof(data));

    if (err < 0)
        return 0;

    return !!data;
}

static int uac_clock_selector_get_val(struct uac_device *dev, int selector_id)
{
    unsigned char buf;
    int ret;

    ret = snd_usb_ctl_msg(dev->udev, usb_rcvctrlpipe(dev->udev, 0),
                  UAC2_CS_CUR,
                  USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
                  UAC2_CX_CLOCK_SELECTOR << 8,
                  snd_usb_ctrl_intf(dev) | (selector_id << 8),
                  &buf, sizeof(buf));

    if (ret < 0)
        return ret;
    return buf;
}

static int uac_clock_selector_set_val(struct uac_device *dev, int selector_id,
                    unsigned char pin)
{
    int ret;

    ret = snd_usb_ctl_msg(dev->udev, usb_sndctrlpipe(dev->udev, 0),
                  UAC2_CS_CUR,
                  USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
                  UAC2_CX_CLOCK_SELECTOR << 8,
                  snd_usb_ctrl_intf(dev) | (selector_id << 8),
                  &pin, sizeof(pin));

    if (ret < 0)
        return ret;

    if (ret != sizeof(pin))
        return -EINVAL;

    ret = uac_clock_selector_get_val(dev, selector_id);

    if (ret < 0)
        return ret;

    if (ret != pin)
        return -EINVAL;

    return ret;
}

static int __uac_clock_find_source(struct uac_device *dev, int entity_id,
                    unsigned long *visited, bool validate)
{
    struct uac_clock_source_descriptor *source;
    struct uac_clock_selector_descriptor *selector;
    struct uac_clock_multiplier_descriptor *multiplier;

    entity_id &= 0xff;

    source = snd_usb_find_clock_source(dev->ctrl_intf, entity_id);
    if (source) {
        entity_id = source->bClockID;
        if (validate && !snd_usb_clock_source_is_valid(dev, entity_id)){
            UAC_ERROR("clock source %d is not valid, cannot use", entity_id);
            return -ENXIO;
        }
        return entity_id;
    }

    selector = snd_usb_find_clock_selector(dev->ctrl_intf, entity_id);
    if (selector != NULL) {
        int ret, i, cur, err;

        ret = uac_clock_selector_get_val(dev, selector->bClockID);
        if (ret < 0)
            return ret;

        if (ret > selector->bNrInPins || ret < 1){
            UAC_ERROR("selector reported illegal value, id %d, ret %d",
                selector->bClockID, ret);
            return -EINVAL;
        }

        cur = ret;
        ret = __uac_clock_find_source(dev, selector->baCSourceID[ret - 1],
                           visited, validate);

        if (ret > 0) {
            err = uac_clock_selector_set_val(dev, entity_id, cur);
            if (err < 0)
                return err;
        }

        if (!validate || ret > 0)
            return ret;

        for (i = 1; i <= selector->bNrInPins; i++) {
            if (i == cur)
                continue;

            ret = __uac_clock_find_source(dev, selector->baCSourceID[i - 1],
                visited, true);
            if (ret < 0)
                continue;

            err = uac_clock_selector_set_val(dev, entity_id, i);
            if (err < 0)
                continue;

            return ret;
        }
        return -ENXIO;
    }

    multiplier = snd_usb_find_clock_multiplier(dev->ctrl_intf, entity_id);
    if (multiplier)
        return __uac_clock_find_source(dev, multiplier->bCSourceID,
                        visited, validate);

    return -EINVAL;
}

int snd_usb_clock_find_source(struct uac_device *dev, int entity_id, bool validate)
{
    DECLARE_BITMAP(visited, 256);
    memset(visited, 0, sizeof(visited));

    switch (dev->ctrl_intf->desc.bInterfaceProtocol) {
    case UAC_VERSION_2:
        return __uac_clock_find_source(dev, entity_id, visited, validate);

    default:
        return -EINVAL;
    }
}

/* format */

static unsigned char snd_usb_parse_data_interval(struct uac_device *dev,
                     struct usb_host_interface *alts)
{
    switch (snd_usb_get_speed(dev->udev)) {
    case USB_SPEED_HIGH:
        if (snd_usb_get_ep_desc(alts, 0)->bInterval >= 1 &&
            snd_usb_get_ep_desc(alts, 0)->bInterval <= 4)
            return snd_usb_get_ep_desc(alts, 0)->bInterval - 1;
        break;

    default:
        break;
    }

    return 0;
}

static int snd_usb_parse_uac_endpoint_attributes(struct uac_device *dev,
                     struct usb_host_interface *alts, int protocol, int iface_no)
{
    struct uac_iso_endpoint_descriptor *csep;
    struct usb_interface_descriptor *altsd = snd_usb_get_iface_desc(alts);
    int attributes = 0;

    csep = snd_usb_find_desc(alts->endpoint[0].extra, alts->endpoint[0].extralen, \
                                NULL, USB_DT_CS_ENDPOINT);

    if (!csep && altsd->bNumEndpoints >= 2)
        csep = snd_usb_find_desc(alts->endpoint[1].extra, alts->endpoint[1].extralen, \
                                    NULL, USB_DT_CS_ENDPOINT);

    if (!csep)
        csep = snd_usb_find_desc(alts->extra, alts->extralen, \
                                    NULL, USB_DT_CS_ENDPOINT);

    if (!csep || csep->bLength < 7 || csep->bDescriptorSubtype != UAC_EP_GENERAL)
        goto error;

    switch (protocol)
    {
    case UAC_VERSION_1:
        attributes = csep->bmAttributes;
        break;

    case UAC_VERSION_2: {
        struct uac2_iso_endpoint_descriptor *csep2 =
            (struct uac2_iso_endpoint_descriptor *) csep;

        if (csep2->bLength < sizeof(*csep2))
            goto error;
        attributes = csep->bmAttributes & UAC_EP_CS_ATTR_FILL_MAX;

        if (csep2->bmControls & UAC2_CONTROL_PITCH)
            attributes |= UAC_EP_CS_ATTR_PITCH_CONTROL;

        break;
    }

    default:
        goto error;
    }

    return attributes;
error:
    UAC_ERROR("%u:%d : no or invalid class specific endpoint descriptor\n",
               iface_no, altsd->bAlternateSetting);
    return 0;
}

static int parse_uac2_sample_rate_range(struct audioformat *fp, int nr_triplets,
                    const unsigned char *data)
{
    int i, nr_rates = 0;

    fp->rates = fp->rate_min = fp->rate_max = 0;

    for (i = 0; i < nr_triplets; i++) {
        int min = combine_quad(&data[2 + 12 * i]);
        int max = combine_quad(&data[6 + 12 * i]);
        int res = combine_quad(&data[10 + 12 * i]);
        unsigned int rate;

        if ((max < 0) || (min < 0) || (res < 0) || (max < min))
            continue;

        if (res == 1) {
            fp->rate_min = min;
            fp->rate_max = max;
            fp->rates = SNDRV_PCM_RATE_CONTINUOUS;
            return 0;
        }

        for (rate = min; rate <= max; rate += res) {
            if (fp->rate_table)
                fp->rate_table[nr_rates] = rate;
            if (!fp->rate_min || rate < fp->rate_min)
                fp->rate_min = rate;
            if (!fp->rate_max || rate > fp->rate_max)
                fp->rate_max = rate;
            fp->rates |= BIT(snd_pcm_rate_to_rate_enum(rate));

            nr_rates++;
            if (nr_rates >= UAC_MAX_NR_RATES) {
                UAC_ERROR("invalid uac2 rates");
                break;
            }

            if (res == 0)
                break;
        }
    }

    return nr_rates;
}

static u64 parse_audio_format_i_type(struct uac_device *dev,
                     struct audioformat *fp,
                     unsigned int format, void *_fmt,
                     int protocol)
{
    int sample_width = 0, sample_bytes = 0;
    u64 pcm_formats = 0;

    if (protocol != UAC_VERSION_2)
        protocol = UAC_VERSION_1;     /* default */

    switch (protocol) {
    case UAC_VERSION_1: {
        struct uac_format_type_i_discrete_descriptor *fmt = _fmt;
        sample_width = fmt->bBitResolution;
        sample_bytes = fmt->bSubframeSize;
        format = 1 << format;
        break;
    }

    case UAC_VERSION_2: {
        struct uac_format_type_i_ext_descriptor *fmt = _fmt;
        sample_width = fmt->bBitResolution;
        sample_bytes = fmt->bSubslotSize;
        format <<= 1;
        break;
    }
    }

    fp->fmt_bits = sample_width;

    if ((pcm_formats == 0) && (format == 0 || format == (1 << UAC_FORMAT_TYPE_I_UNDEFINED))) {
        format = 1 << UAC_FORMAT_TYPE_I_PCM;
    }

    if (format & (1 << UAC_FORMAT_TYPE_I_PCM)) {
        switch (sample_bytes) {
        case 1:
            pcm_formats |= pcm_format_to_bits(pcm_fmt_S8);
            break;

        case 2:
            pcm_formats |= pcm_format_to_bits(pcm_fmt_S16LE);
            break;

        case 3:
            /* to do */
            /* pcm_formats |= pcm_format_to_bits(pcm_fmt_S24_3LE); */
            break;

        case 4:
            pcm_formats |= pcm_format_to_bits(pcm_fmt_S32LE);
            break;

        default:
            UAC_ERROR("unsupported sample bitwidth %d", sample_width);
            break;

        }
    }
    return pcm_formats;
}

static int parse_audio_format_rates_v1(struct uac_device *dev, struct audioformat *fp,
                       unsigned char *fmt, int offset)
{
    int nr_rates = fmt[offset];

    if (fmt[0] < offset + 1 + 3 * (nr_rates ? nr_rates : 2)) {
        UAC_ERROR("%u:%d : invalid UAC_FORMAT_TYPE desc",
            fp->iface, fp->altsetting);
        return -EINVAL;
    }

    if (nr_rates) {
        int r, idx;

        fp->rate_table = malloc(sizeof(int) * nr_rates);
        if (fp->rate_table == NULL) {
            return -ENOMEM;
        }
        memset(fp->rate_table, 0, sizeof(int) * nr_rates);

        fp->nr_rates = 0;
        fp->rate_min = fp->rate_max = 0;

        for (r = 0, idx = offset + 1; r < nr_rates; r++, idx += 3) {
            unsigned int rate = combine_triple(&fmt[idx]);
            if (!rate)
                continue;

            fp->rate_table[fp->nr_rates] = rate;
            if (!fp->rate_min || rate < fp->rate_min)
                fp->rate_min = rate;
            if (!fp->rate_max || rate > fp->rate_max)
                fp->rate_max = rate;
            fp->rates |= BIT(snd_pcm_rate_to_rate_enum(rate));
            fp->nr_rates++;
        }
        if (!fp->nr_rates) {
            UAC_ERROR("All rates were zero. Skipping format!");
            return -EINVAL;
        }
    } else {
        /* continuous rates */
        fp->rates = SNDRV_PCM_RATE_CONTINUOUS;
        fp->rate_min = combine_triple(&fmt[offset + 1]);
        fp->rate_max = combine_triple(&fmt[offset + 4]);
    }
    return 0;
}

static int parse_audio_format_rates_v2(struct uac_device *dev,
                       struct audioformat *fp)
{
    struct usb_device *udev = dev->udev;
    unsigned char tmp[2], *data;
    int nr_triplets, data_size, ret = 0;
    int clock = snd_usb_clock_find_source(dev, fp->clock, false);

    if (clock < 0) {
        UAC_ERROR("unable to find clock source (clock %d)", clock);
        goto err;
    }

    ret = snd_usb_ctl_msg(udev, usb_rcvctrlpipe(udev, 0), UAC2_CS_RANGE,
                  USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
                  UAC2_CS_CONTROL_SAM_FREQ << 8,
                  snd_usb_ctrl_intf(dev) | (clock << 8),
                  tmp, sizeof(tmp));
    if (ret < 0)
        goto err;

    nr_triplets = (tmp[1] << 8) | tmp[0];
    data_size = 2 + 12 * nr_triplets;
    data = malloc(data_size);
    if (!data) {
        ret = -ENOMEM;
        goto err;
    }
    memset(data, 0, data_size);

    ret = snd_usb_ctl_msg(udev, usb_rcvctrlpipe(udev, 0), UAC2_CS_RANGE,
                  USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
                  UAC2_CS_CONTROL_SAM_FREQ << 8,
                  snd_usb_ctrl_intf(dev) | (clock << 8),
                  data, data_size);

    if (ret < 0) {
        UAC_ERROR("unable to retrieve sample rate range (clock %d)", clock);
        ret = -EINVAL;
        goto err_free;
    }

    if (fp->rate_table) {
        free(fp->rate_table);
        fp->rate_table = NULL;
    }

    fp->nr_rates = parse_uac2_sample_rate_range(fp, nr_triplets, data);

    if (fp->nr_rates == 0) {
        /* SNDRV_PCM_RATE_CONTINUOUS */
        ret = 0;
        goto err_free;
    }

    fp->rate_table = malloc(sizeof(int) * fp->nr_rates);
    if (!fp->rate_table) {
        ret = -ENOMEM;
        goto err_free;
    }
    memset(fp->rate_table, 0, sizeof(int) * fp->nr_rates);

    /* Call the triplet parser again, but this time, fp->rate_table is
     * allocated, so the rates will be stored */
    parse_uac2_sample_rate_range(fp, nr_triplets, data);

err_free:
    free(data);
err:
    return ret;
}

static int parse_audio_format_i(struct uac_device *dev,
                struct audioformat *fp, unsigned int format,
                struct uac_format_type_i_continuous_descriptor *fmt,
                struct usb_host_interface *iface)
{
    struct usb_interface_descriptor *altsd = snd_usb_get_iface_desc(iface);
    int protocol = altsd->bInterfaceProtocol;
    int ret = 0;

    if (fmt->bFormatType == UAC_FORMAT_TYPE_I) {
        fp->formats = parse_audio_format_i_type(dev, fp, format, fmt, protocol);
        if (!fp->formats)
            return -EINVAL;
    }

    switch (protocol) {
    case UAC_VERSION_1:
        fp->channels = fmt->bNrChannels;

        ret = parse_audio_format_rates_v1(dev, fp, (unsigned char *) fmt, 7);
        break;

    case UAC_VERSION_2:
        ret = parse_audio_format_rates_v2(dev, fp);
        break;
    }

    if (fp->channels < 1) {
        UAC_ERROR("%u:%d : invalid channels %d",
            fp->iface, fp->altsetting, fp->channels);
        return -EINVAL;
    }

    return ret;
}

static int snd_usb_parse_audio_format(struct uac_device *dev,
                   struct audioformat *fp, unsigned int format,
                   struct uac_format_type_i_continuous_descriptor *fmt,
                   int stream, struct usb_host_interface *iface)
{
    int ret = 0;

    switch (fmt->bFormatType) {
    case UAC_FORMAT_TYPE_I:
        ret = parse_audio_format_i(dev, fp, format, fmt, iface);
        break;

    case UAC_FORMAT_TYPE_II:
    case UAC_FORMAT_TYPE_III:
    default:
        UAC_ERROR("%u:%d : format type %d is not supported yet",
         fp->iface, fp->altsetting, fmt->bFormatType);
        return -ENOTSUP;
    }

    fp->fmt_type = fmt->bFormatType;
    return ret;
}

/*
 * format type different -> create one stream, init the substream
 * format type same && substream not inited -> init the substream
 * format type same && substream inited && subs->ep same -> add audio format to the substream
 * format type same && substream inited && subs->ep different -> create one stream, init the substream
 */
static int snd_usb_add_audio_stream(struct uac_device *dev, int stream, struct audioformat *fp)
{
    struct snd_usb_stream *as;
    struct snd_usb_substream *subs;

    list_for_each_entry(as, &dev->pcm_list, list) {
        if (as->fmt_type != fp->fmt_type)
            continue;

        subs = &as->substream[stream];
        if (subs->ep_num == fp->endpoint) {
            list_add_tail(&fp->list, &subs->fmt_list);
            subs->num_formats++;
            subs->formats |= fp->formats;
            return 0;
        }
    }

    list_for_each_entry(as, &dev->pcm_list, list) {
        if (as->fmt_type != fp->fmt_type)
            continue;

        subs = &as->substream[stream];
        if (subs->ep_num)
            continue;

        goto init_subs;
    }

    /* create a new stream */
    as = malloc(sizeof(*as));
    if (!as)
        return -ENOMEM;
    memset(as, 0, sizeof(*as));

    as->uac = dev;
    as->fmt_type = fp->fmt_type;
    as->pcm_index = snd_usb_get_list_num(&dev->pcm_list);
    list_add(&as->list, &dev->pcm_list);

init_subs:
    snd_usb_init_substream(as, stream, fp);

    return 0;
}

static int snd_usb_parse_audio_interface(struct uac_device *dev, int iface_no)
{
    struct usb_device *udev;
    struct usb_interface *iface;
    struct usb_host_interface *alts;
    struct usb_interface_descriptor *altsd;
    int i, altno, stream, err = 0;
    unsigned int format = 0, num_channels = 0;
    int num, protocol, clock = 0;
    struct uac_format_type_i_continuous_descriptor *fmt;

    udev = dev->udev;
    iface = usb_ifnum_to_if(udev, iface_no);
    num = iface->num_altsetting;

    for (i = 0; i < num; i++) {
        alts = &iface->altsetting[i];
        altsd = snd_usb_get_iface_desc(alts);
        altno = altsd->bAlternateSetting;
        protocol = altsd->bInterfaceProtocol;

        /* skip invalid one */
        if ((altsd->bInterfaceClass != USB_CLASS_AUDIO ) ||
            (altsd->bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING) ||
            altsd->bNumEndpoints < 1 ||
            snd_usb_get_ep_desc(alts, 0)->wMaxPacketSize == 0)
            continue;

        /* must be isochronous */
        if ((snd_usb_get_ep_desc(alts, 0)->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) !=
            USB_ENDPOINT_XFER_ISOC)
            continue;

        /* check direction */
        stream = (snd_usb_get_ep_desc(alts, 0)->bEndpointAddress & USB_DIR_IN) ?
            UAC_SUBSTREAM_CAPTURE : UAC_SUBSTREAM_PLAYBACK;

        /* get audio formats */
        switch (protocol) {
        case UAC_VERSION_1: {
            struct uac1_as_header_descriptor *as = (struct uac1_as_header_descriptor *)
                snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_AS_GENERAL);
            if (!as)
                continue;

            if (as->bLength < sizeof(*as))
                continue;

            format = as->wFormatTag;
            break;
        }

        case UAC_VERSION_2: {
            struct uac2_input_terminal_descriptor *input_term;
            struct uac2_output_terminal_descriptor *output_term;
            struct uac2_as_header_descriptor *as = (struct uac2_as_header_descriptor *)
                    snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_AS_GENERAL);
            if (!as)
                continue;

            format = as->bmFormats;
            num_channels = as->bNrChannels;

            input_term = snd_usb_find_input_terminal_descriptor(dev->ctrl_intf, as->bTerminalLink);
            if (input_term) {
                clock = input_term->bCSourceID;
                break;
            }

            output_term = snd_usb_find_output_terminal_descriptor(dev->ctrl_intf, as->bTerminalLink);
            if (output_term) {
                clock = output_term->bCSourceID;
                break;
            }

            break;
        }
        }

        fmt = snd_usb_find_csint_desc(alts->extra, alts->extralen, NULL, UAC_FORMAT_TYPE);
        if (!fmt)
            continue;

        struct audioformat *fp = malloc(sizeof(*fp));
        if (!fp)
            return -ENOMEM;
        memset(fp, 0, sizeof(*fp));

        fp->iface = iface_no;
        fp->altsetting = altno;
        fp->altset_idx = i;
        fp->endpoint = snd_usb_get_ep_desc(alts, 0)->bEndpointAddress;
        fp->ep_attr = snd_usb_get_ep_desc(alts, 0)->bmAttributes;
        fp->channels = num_channels;
        fp->clock = clock;
        fp->maxpacksize = snd_usb_get_ep_desc(alts, 0)->wMaxPacketSize;
        if (snd_usb_get_speed(udev) == USB_SPEED_HIGH)
            fp->maxpacksize = (((fp->maxpacksize >> 11) & 3) + 1) * (fp->maxpacksize & 0x7ff);
        fp->datainterval = snd_usb_parse_data_interval(dev, alts);
        fp->attributes = snd_usb_parse_uac_endpoint_attributes(dev, alts, protocol, iface_no);

        if (snd_usb_parse_audio_format(dev, fp, format, fmt, stream, alts) < 0) {
            if (fp->rate_table)
                free(fp->rate_table);
            free(fp);
            continue;
        }

        err = snd_usb_add_audio_stream(dev, stream, fp);
        if (err < 0) {
            free(fp->rate_table);
            free(fp);
            return err;
        }

        /* try to set the interface... */
        snd_usb_init_interface(dev, iface_no, altno, alts, fp);
    }

    return 0;
}

static int snd_usb_stream_create(struct uac_device *dev, int ctrlif, int interface)
{
    struct usb_device *udev = dev->udev;
    struct usb_host_interface *alts;
    struct usb_interface_descriptor *altsd;
    struct usb_interface *iface = usb_ifnum_to_if(udev, interface);

    if (!iface)
        return -EINVAL;

    alts = &iface->altsetting[0];
    altsd = snd_usb_get_iface_desc(alts);

    if (usb_interface_claimed(iface)) {
        UAC_ERROR("%d:%d: skipping, already claimed", ctrlif, interface);
        return -EINVAL;
    }

    if ((altsd->bInterfaceClass != USB_CLASS_AUDIO) ||
         altsd->bInterfaceSubClass != USB_SUBCLASS_AUDIOSTREAMING) {
        UAC_ERROR("%u:%d: skipping non-supported interface %d",
            ctrlif, interface, altsd->bInterfaceClass);
        return -EINVAL;
    }

    if (snd_usb_get_speed(udev) == USB_SPEED_LOW) {
        UAC_ERROR("low speed audio streaming not supported");
        return -EINVAL;
    }

    if (!snd_usb_parse_audio_interface(dev, interface)) {
        usb_set_interface(udev, interface, 0);
        return usb_driver_claim_interface(&uac_driver, iface, UAC_IFACE_UNUSED);
    }

    return 0;
}

static int snd_usb_streams_create(struct uac_device *dev, int ctrlif)
{
    struct usb_device *udev = dev->udev;
    struct usb_host_interface *host_iface;
    struct usb_interface_descriptor *altsd;
    struct snd_usb_stream *as;
    int i, protocol, ret;

    /* find audiocontrol interface */
    host_iface = &usb_ifnum_to_if(udev, ctrlif)->altsetting[0];
    altsd = snd_usb_get_iface_desc(host_iface);
    protocol = altsd->bInterfaceProtocol;

    /* parse audio streaming interface */
    switch (protocol) {
    case UAC_VERSION_1: {
        struct uac1_ac_header_descriptor *h1;
        int rest_bytes;

        h1 = snd_usb_find_csint_desc(host_iface->extra,
                             host_iface->extralen,
                             NULL, UAC_HEADER);
        if (!h1 || h1->bLength < sizeof(*h1)) {
            UAC_ERROR("cannot find uac header descriptor (v1)");
            return -EINVAL;
        }

        rest_bytes = (void *)(host_iface->extra +
                host_iface->extralen) - (void *)h1;

        if (rest_bytes <= 0) {
            UAC_ERROR("invalid control header (v1)");
            return -EINVAL;
        }

        if (!h1->bInCollection) {
            UAC_ERROR("skipping empty audio interface (v1)");
            return -EINVAL;
        }

        if (rest_bytes < h1->bLength) {
            UAC_ERROR("invalid buffer length (v1)");
            return -EINVAL;
        }

        if (h1->bLength < sizeof(*h1) + h1->bInCollection) {
            UAC_ERROR("invalid uac header descriptor (v1)");
            return -EINVAL;
        }

        for (i = 0; i < h1->bInCollection; i++)
            snd_usb_stream_create(dev, ctrlif, h1->baInterfaceNr[i]);

        break;
    }

    case UAC_VERSION_2: {
        struct usb_interface_assoc_descriptor *assoc = (struct usb_interface_assoc_descriptor *)
                usb_ifnum_to_if(udev, ctrlif)->intf_assoc;

        if (!assoc) {
            struct usb_interface *iface =
                usb_ifnum_to_if(udev, ctrlif + 1);
            if (iface &&
                iface->intf_assoc &&
                iface->intf_assoc->bFunctionClass == USB_CLASS_AUDIO &&
                iface->intf_assoc->bFunctionProtocol == UAC_VERSION_2)
                assoc = iface->intf_assoc;
        }

        if (!assoc) {
            UAC_ERROR("Audio class v2/v3 interfaces need an interface association");
            return -EINVAL;
        }

        for (i = 0; i < assoc->bInterfaceCount; i++) {
            int intf = assoc->bFirstInterface + i;

            if (intf != ctrlif)
                snd_usb_stream_create(dev, ctrlif, intf);
        }

        break;
    }

    default: {
        UAC_ERROR("UAC version (%#02x) is not supported yet", protocol);
        return -EINVAL;
    }
    }

    /* register pcm devices */
    list_for_each_entry(as, &dev->pcm_list, list) {
        struct snd_usb_substream *subs;
        int idx;

        for (idx = 0; idx < UAC_SUBSTREAM_NUMS; idx++) {
            subs = &(as->substream[idx]);
            if (!subs->dev)
                continue;

            ret = snd_usb_pcm_new(as, idx);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static void snd_usb_streams_delete(struct uac_device *dev)
{
    struct snd_usb_stream *as, *n1;
    struct snd_usb_substream *subs;
    struct audioformat *fp, *n;
    int idx;

    if (list_empty(&dev->pcm_list))
        return;

    list_for_each_entry_safe(as, n1, &dev->pcm_list, list) {
        list_del(&as->list);

        for (idx = 0; idx < UAC_SUBSTREAM_NUMS; idx++) {
            subs = &(as->substream[idx]);
            if (!subs->dev)
                continue;

            subs->dev = NULL;
            subs->stream = NULL;
            subs->data_endpoint = NULL;

            if (subs->pcm) {
                struct pcm_dev_data *pcm = subs->pcm;
                struct pcm_device *pcm_dev = pcm_get(pcm->name);
                if (!pcm_dev)
                    continue;

                pcm_dev->is_exiting = 1;

                if (!wake_lock_is_locked(&pcm_dev->w_lock)) {
                    pcm_unregister(pcm);
                    snd_usb_pcm_free(pcm);
                } else {
                    as->opened_pcm += 1;
                }

                subs->pcm = NULL;
            }

            if (!subs->num_formats)
                continue;

            list_for_each_entry_safe(fp, n, &subs->fmt_list, list) {
                list_del(&fp->list);
                if (fp->rate_table)
                    free(fp->rate_table);
                free(fp);
            }
        }

        if (!as->opened_pcm) {
            free(as);
        } else {
            dev->opened_stream += 1;
        }
    }
}

static int uac_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct uac_device *dev;
    int function;
    u8 minor;
    int ret = 0;

    for (minor = 0; minor < UAC_MINORS && uac_table[minor]; minor++);

    if (minor == UAC_MINORS) {
        UAC_ERROR("no more free uac devices");
        return -ENODEV;
    }

    /* Allocate memory for the device and initialize it. */
    dev = malloc(sizeof(*dev));
    if (!dev)
        return -ENOMEM;
    memset(dev, 0, sizeof(*dev));

    dev->minor = minor;
    dev->udev = udev;
    dev->intf = intf;
    dev->ctrl_intf = &intf->altsetting[0];

    INIT_LIST_HEAD(&dev->ep_list);
    INIT_LIST_HEAD(&dev->pcm_list);

    if (udev->product != NULL)
        strncpy(dev->name, udev->product, sizeof(dev->name));
    else
        snprintf(dev->name, sizeof(dev->name), "UAC Device (%04x:%04x)",
             udev->descriptor.idVendor, udev->descriptor.idProduct);

    /*
     * Add iFunction or iInterface to names when available as additional
     * distinguishers between interfaces. iFunction is prioritized over
     * iInterface which matches Windows behavior at the point of writing.
     */
    if (intf->intf_assoc && intf->intf_assoc->iFunction != 0)
        function = intf->intf_assoc->iFunction;
    else
        function = intf->cur_altsetting->desc.iInterface;
    if (function != 0) {
        size_t len;

        strlcat(dev->name, ": ", sizeof(dev->name));
        len = strlen(dev->name);
        usb_string(udev, function, dev->name + len, sizeof(dev->name) - len);
    }

    /* parse audio control descriptor and create pcm streams */
    ret = snd_usb_streams_create(dev, intf->cur_altsetting->desc.bInterfaceNumber);
    if (ret < 0)
        goto err_stream;

    /* parse feature unit descriptor and create mixer */
    ret = snd_usb_mixer_create(dev, intf->cur_altsetting->desc.bInterfaceNumber);
    if (ret < 0)
        goto err_stream;

    /* Save our data pointer in the interface data. */
    usb_set_intfdata(intf, dev);

    uac_table[minor] = dev;

    UAC_INFO("UAC device (%d) initialized\n", minor);
    update_devices_bit();

    return 0;

err_stream:
    snd_usb_streams_delete(dev);

    free(dev);
    return -ENODEV;
}

static void uac_disconnect(struct usb_interface *intf)
{
    u8 id;
    struct uac_device *dev = usb_get_intfdata(intf);

    usb_set_intfdata(intf, NULL);

    if (dev == UAC_IFACE_UNUSED)
        return;

    if (!dev || !dev->udev)
        return;

    id = dev->minor;
    assert(id < UAC_MINORS);

    mutex_lock(&device_lock[id]);

    dev->udev = NULL;
    dev->exiting = true;

    while (dev->used)
        thread_cond_wait(&free_cond[id], &device_lock[id]);

    /* release the mixer resources */
    snd_usb_mixer_delete(dev);

    /* release the pcm resources */
    snd_usb_streams_delete(dev);

    /* release the endpoint resources */
    snd_usb_endpoints_release(dev);

    if (!dev->opened_stream) {
        free(dev);
        uac_table[id] = NULL;
    }

    mutex_unlock(&device_lock[id]);

    update_devices_bit();
    UAC_INFO("UAC device (%d) disconnect\n", id);
}

static const struct usb_device_id uac_ids[] = {
    { USB_INTERFACE_INFO(USB_CLASS_AUDIO, USB_SUBCLASS_AUDIOCONTROL, UAC_VERSION_1) },
    { USB_INTERFACE_INFO(USB_CLASS_AUDIO, USB_SUBCLASS_AUDIOCONTROL, UAC_VERSION_2) },
    {}
};

static struct usb_driver uac_driver = {
    .name        = "uacaudio",
    .probe       = uac_probe,
    .disconnect  = uac_disconnect,
    .id_table    = uac_ids,
};

void usb_uac_driver_register(void)
{
    int i;

    for (i = 0; i < UAC_MINORS; i++) {
        mutex_init(&device_lock[i]);
        thread_cond_init(&free_cond[i]);
    }

    usb_register_driver(&uac_driver);
}

void usb_uac_driver_deregister(void)
{
    usb_deregister(&uac_driver);
}