static void convert_short(uint16_t *dst, const uint16_t *src,
                          int src_frames, int dst_frames,
                          int channels, uint64_t step)
{
    for (int ch = 0; ch < channels; ch++) {
        const uint16_t *src_ch = src + ch;
        uint16_t *dst_ch = dst + ch;
        uint64_t pos = 0;

        for (int i = 0; i < dst_frames; i++) {
            int idx = pos >> 32;
            if (idx >= src_frames)
                idx = src_frames - 1;
            dst_ch[0] = src_ch[idx * channels];
            dst_ch += channels;
            pos += step;
        }
    }
}

static void convert_int(uint32_t *dst, const uint32_t *src,
                        int src_frames, int dst_frames,
                        int channels, uint64_t step)
{
    for (int ch = 0; ch < channels; ch++) {
        const uint32_t *src_ch = src + ch;
        uint32_t *dst_ch = dst + ch;
        uint64_t pos = 0;

        for (int i = 0; i < dst_frames; i++) {
            int idx = pos >> 32;
            if (idx >= src_frames)
                idx = src_frames - 1;
            dst_ch[0] = src_ch[idx * channels];
            dst_ch += channels;
            pos += step;
        }
    }
}

int pcm_sample_rate_convert(void *dst, void *src, int frames,
                            int channels, pcm_data_fmt data_fmt,
                            pcm_sample_rate src_rate, pcm_sample_rate dst_rate)
{
    if (!dst || !src || frames <= 0 || channels <= 0)
        return -EINVAL;

    unsigned int src_hz = pcm_data_sample_rate(src_rate);
    unsigned int dst_hz = pcm_data_sample_rate(dst_rate);

    uint64_t step = ((uint64_t)src_hz << 32) / dst_hz;
    if (!step)
        step = 1;

    int dst_frames = frames * dst_hz / src_hz;

    int sample_size = pcm_data_sample_size(data_fmt);
    switch (sample_size)
    {
    case 2:
        convert_short(dst, src, frames, dst_frames, channels, step);
        break;

    case 4:
        convert_int(dst, src, frames, dst_frames, channels, step);
        break;

    default:
        return -EINVAL;
    }

    return dst_frames;
}
