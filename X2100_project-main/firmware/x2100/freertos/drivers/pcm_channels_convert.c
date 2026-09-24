int pcm_channels_convert_1_to_2(void *dst_, void *src_, int frame_count, pcm_data_fmt data_fmt)
{
    if (data_fmt == pcm_fmt_S8 || data_fmt == pcm_fmt_U8) {
        char *dst = dst_;
        char *src = src_;

        while(frame_count--) {
            dst[0] = *src;
            dst[1] = *src;
            src ++;
            dst += 2;
        }
        return 0;
    }

    if (data_fmt == pcm_fmt_S16LE || data_fmt == pcm_fmt_U16LE) {
        short *dst = dst_;
        short *src = src_;

        while(frame_count--) {
            dst[0] = *src;
            dst[1] = *src;
            src ++;
            dst += 2;
        }
        return 0;
    }

    if (data_fmt == pcm_fmt_S32LE || data_fmt == pcm_fmt_U32LE\
        || data_fmt == pcm_fmt_S24LE || data_fmt == pcm_fmt_U24LE) {
        int *dst = dst_;
        int *src = src_;

        while(frame_count--) {
            dst[0] = *src;
            dst[1] = *src;
            src ++;
            dst += 2;
        }
        return 0;
    }

    printf("%s: audio format:%d is not supported\n", __FUNCTION__, data_fmt);
    return -1;
}

int pcm_channels_convert_2_to_1(void *dst_, void *src_, int frame_count, pcm_data_fmt data_fmt)
{
    if (data_fmt == pcm_fmt_S8 || data_fmt == pcm_fmt_U8) {
        char *dst = dst_;
        char *src = src_;

        while(frame_count--) {
            *dst++ = ((int)src[0] + (int)src[1]) / 2;
            src += 2;
        }
        return 0;
    }

    if (data_fmt == pcm_fmt_S16LE || data_fmt == pcm_fmt_U16LE) {
        short *dst = dst_;
        short *src = src_;

        while(frame_count--) {
            *dst++ = ((int)src[0] + (int)src[1]) / 2;
            src += 2;
        }

        return 0;
    }

    if (data_fmt == pcm_fmt_S32LE || data_fmt == pcm_fmt_U32LE\
        || data_fmt == pcm_fmt_S24LE || data_fmt == pcm_fmt_U24LE) {
        int *dst = dst_;
        int *src = src_;
        while(frame_count--) {
            *dst++ = ((long long)src[0] + (long long)src[1]) / 2;
            src += 2;
        }
        return 0;
    }

    printf("%s: audio format:%d is not supported\n", __FUNCTION__, data_fmt);
    return -1;
}