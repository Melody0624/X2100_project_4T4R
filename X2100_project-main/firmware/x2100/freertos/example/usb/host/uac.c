#include <stdio.h>
#include <errno.h>
#include <driver/gpio.h>
#include <driver/pcm.h>
#include <include/usb/host_uac.h>

#include <include_bin.h>
#include <wav_utils.h>

#define DEFAULT_CAPTURE_TIME_SEC        10
#define DEFAULT_BUFFER_TIME_MS          500
#define DEFAULT_BUFFER_PERIO_MS         125
#define MIN_PERIODS                     3

#define PLAY_THREAD_STATE_WAIT          0
#define PLAY_THREAD_STATE_OK            1
#define PLAY_THREAD_STATE_ERR           2

INCBIN(wav_mono_s16le_16000, "example/resource/mono_s16le_16000.wav");
INCBIN(wav_mono_s16le_44100, "example/resource/mono_s16le_44100.wav");
INCBIN(wav_mono_s16le_48000, "example/resource/mono_s16le_48000.wav");
INCBIN(wav_stereo_s16le_16000, "example/resource/stereo_s16le_16000.wav");
INCBIN(wav_stereo_s16le_44100, "example/resource/stereo_s16le_44100.wav");
INCBIN(wav_stereo_s16le_48000, "example/resource/stereo_s16le_48000.wav");

static struct uac_pcm_param {
    struct pcm_device *pcm_dev;
    struct pcm_params params;
};

static struct uac_param {
    u8 minor;
    thread_ptr_t uac_thread;

    struct uac_pcm_param capture;
    struct uac_pcm_param playback;

    /* for playback thread */
    volatile int play_state;
    void *audio;
    int frames;
} uac_params[UAC_MINORS];

static volatile u8 uac_devices_open_bit = 0;

/* --------------------------------------------------------------------------
 * pcm data format convert
 */

extern int pcm_channels_convert_2_to_1(void *dst_, void *src_, int frame_count, pcm_data_fmt data_fmt);
extern int pcm_channels_convert_1_to_2(void *dst_, void *src_, int frame_count, pcm_data_fmt data_fmt);
extern int pcm_format_convert(void *dst_, void *src_, int samples, pcm_data_fmt src_fmt, pcm_data_fmt dst_fmt);

static int channel_convert(void *src_buf, int src_ch, void *dst_buf,int dst_ch,
                            int frame_count, pcm_data_fmt fmt)
{
    if (src_ch == dst_ch)
        return 0;

    if (src_ch == 1 && dst_ch == 2)
        return pcm_channels_convert_1_to_2(dst_buf, src_buf, frame_count, fmt);

    if (src_ch == 2 && dst_ch == 1)
        return pcm_channels_convert_2_to_1(dst_buf, src_buf, frame_count, fmt);

    return -EINVAL;
}

static int format_convert(void *src_buf, pcm_data_fmt src_fmt, void *dst_buf, pcm_data_fmt dst_fmt,
                            int frame_count, int channels)
{
    return pcm_format_convert(dst_buf, src_buf, frame_count * channels, src_fmt, dst_fmt);
}

static int uac_pcm_data_convert(struct pcm_params *src, void *src_buf, int src_len,
                        struct pcm_params *dst, void *dst_buf, int dst_len)
{
    int ret = 0;

    memset(dst_buf, 0, dst_len);

    if (src->channels != dst->channels) {
        ret = channel_convert(src_buf, src->channels,
                              dst_buf, dst->channels,
                              src_len / pcm_frame_size(src),
                              src->pcm_data_fmt);
        if (ret < 0)
            return ret;
    }

    if (src->pcm_data_fmt != dst->pcm_data_fmt) {
        ret = format_convert(src_buf, src->pcm_data_fmt,
                             dst_buf, dst->pcm_data_fmt,
                             src_len / pcm_frame_size(src),
                             dst->channels);
        if (ret < 0)
            return ret;
    }

    if (src->pcm_sample_rate != dst->pcm_sample_rate) {
        printf("convert for sample rate not yet support\n");
        ret = -EINVAL;
    }

    return ret;
}

/* --------------------------------------------------------------------------
 * main functions
 */

static int uac_volume_control(struct pcm_device *pcm_dev, uac_volume_ctrl_type_t ctrl_type, int *volume)
{
    if (!pcm_dev)
        return -EINVAL;

    int ret = 0;

    switch (ctrl_type)
    {
    case UAC_GET_VOLUME:
        ret = pcm_get_volume(pcm_dev);
        if (ret > 0 && ret <= 100)
            *volume = ret;
        break;

    case UAC_SET_VOLUME:
        if (*volume == 0)
            ret = pcm_set_mute(pcm_dev, 1);
        else {
            /* in case of is mute */
            ret = pcm_set_mute(pcm_dev, 0);
            if (ret < 0)
                return ret;

            ret = pcm_set_volume(pcm_dev, *volume);
        }
        break;

    default:
        printf("invalid control type of volume\n");
    }

    return ret;
}

static int uac_capture_read_frames(u8 idx, struct pcm_device *pcm_dev, char *buffer, int buf_size)
{
    if (!pcm_dev)
        return -EINVAL;

    struct pcm_params capture_params = uac_params[idx].capture.params;
    unsigned int frame_size = pcm_frame_size(&capture_params);
    int frames = 0, read_len = 0, ret = 0;

    memset(buffer, 0, buf_size);

    do {
        frames = (buf_size - read_len) / frame_size;
        ret = pcm_read_frame(pcm_dev, buffer + read_len, frames);
        read_len += ret * frame_size;
    } while ((ret >= 0) && (read_len + frame_size <= buf_size));

    return ret;
}

static int uac_playback_write_frames(u8 idx, struct pcm_device *pcm_dev, void *data, int data_len)
{
    if (!pcm_dev)
        return -EINVAL;

    struct pcm_params playback_params = uac_params[idx].playback.params;
    unsigned int frame_size = pcm_frame_size(&playback_params);
    int frames = 0, offset = 0, ret = 0;

    do {
        frames = (data_len - offset) / frame_size;
        ret = pcm_write_frame(pcm_dev, data + offset, frames);
        offset += ret * frame_size;
    } while ((ret >= 0) && (offset + frame_size <= data_len));

    return ret;
}

static void uac_playback_thread_func(void *data)
{
    u8 idx = *((u8 *) data);
    int ret = 0;

    void *audio = uac_params[idx].audio;
    int frames = uac_params[idx].frames;
    int frame_size = pcm_frame_size(&uac_params[idx].playback.params);
    int audio_len = frames * frame_size;

    if (!audio || audio_len <= 0) {
        ret = -EINVAL;
        goto err_exit;
    }

    if (!uac_params[idx].playback.pcm_dev) {
        ret = -ENODEV;
        goto err_exit;
    }

    ret = uac_playback_write_frames(idx,
        uac_params[idx].playback.pcm_dev, audio, audio_len);

err_exit:
    if (ret < 0)
        uac_params[idx].play_state = PLAY_THREAD_STATE_ERR;
    else
        uac_params[idx].play_state = PLAY_THREAD_STATE_OK;
}

static int uac_init_pcm_params(uac_substream_inform_t *sub_inform, struct pcm_params *params)
{
    if (!sub_inform ||
        !sub_inform->channels_list ||
        !sub_inform->pcm_data_fmt_list ||
        !sub_inform->pcm_sample_rate_list)
        return -EINVAL;

    int i;

    memset(params, 0, sizeof(*params));

    params->buffer_time_ms = DEFAULT_BUFFER_TIME_MS;
    params->period_time_ms = DEFAULT_BUFFER_PERIO_MS;
    if (params->buffer_time_ms / params->period_time_ms < MIN_PERIODS)
        params->period_time_ms = params->buffer_time_ms / MIN_PERIODS;

    for (i = 1; i < 32; i++) {
        if (sub_inform->channels_list & BIT(i))
            break;
    }
    params->channels = i;

    for (i = 0; i < pcm_fmt_nums; i++) {
        if (sub_inform->pcm_data_fmt_list & BIT(i))
            break;
    }
    params->pcm_data_fmt = (pcm_data_fmt) i;

    for (i = 0; i < pcm_rate_nums; i++) {
        if (sub_inform->pcm_sample_rate_list & BIT(i))
            break;
    }
    params->pcm_sample_rate = (pcm_sample_rate) i;

    if (params->channels == 32 ||
        params->pcm_data_fmt == pcm_fmt_nums ||
        params->pcm_sample_rate == pcm_rate_nums)
        return -ERANGE;

    return 0;
}

static int uac_audio_probe_audio_data(struct pcm_params *params, void **audio, int *frames)
{
    if (params->channels == 1
        && params->pcm_data_fmt == pcm_fmt_S16LE
        && params->pcm_sample_rate == pcm_rate_16000)
    {
        *audio = wav_pcm_data((void *) wav_mono_s16le_16000Data);
        *frames = wav_pcm_frames((void *) wav_mono_s16le_16000Data);
    } else if (params->channels == 1
        && params->pcm_data_fmt == pcm_fmt_S16LE
        && params->pcm_sample_rate == pcm_rate_44100)
    {
        *audio = wav_pcm_data((void *) wav_mono_s16le_44100Data);
        *frames = wav_pcm_frames((void *) wav_mono_s16le_44100Data);
    } else if (params->channels == 1
        && params->pcm_data_fmt == pcm_fmt_S16LE
        && params->pcm_sample_rate == pcm_rate_48000)
    {
        *audio = wav_pcm_data((void *) wav_mono_s16le_48000Data);
        *frames = wav_pcm_frames((void *) wav_mono_s16le_48000Data);
    } else if (params->channels == 2
        && params->pcm_data_fmt == pcm_fmt_S16LE
        && params->pcm_sample_rate == pcm_rate_16000)
    {
        *audio = wav_pcm_data((void *) wav_stereo_s16le_16000Data);
        *frames = wav_pcm_frames((void *) wav_stereo_s16le_16000Data);
    } else if (params->channels == 2
        && params->pcm_data_fmt == pcm_fmt_S16LE
        && params->pcm_sample_rate == pcm_rate_44100)
    {
        *audio = wav_pcm_data((void *) wav_stereo_s16le_44100Data);
        *frames = wav_pcm_frames((void *) wav_stereo_s16le_44100Data);
    } else if (params->channels == 2
        && params->pcm_data_fmt == pcm_fmt_S16LE
        && params->pcm_sample_rate == pcm_rate_48000)
    {
        *audio = wav_pcm_data((void *) wav_stereo_s16le_48000Data);
        *frames = wav_pcm_frames((void *) wav_stereo_s16le_48000Data);
    } else
    {
        printf("audio data not found\n");
        return -EINVAL;
    }

    return 0;
}

static void usb_uac_test(void *pdata)
{
    u8 idx = *((u8 *) pdata);

    thread_ptr_t playback_thread = NULL;
    struct pcm_device *uac_playback = NULL, *uac_capture = NULL;
    int skip_capture = 0, skip_playback = 0, skip_convert = 0;
    int need_convert = 0, volume = 85, ret = 0;

    char *buffer = NULL, *convert_buffer = NULL;
    int buf_size = 0, convert_buf_size = 0;

    struct uac_param *params = &uac_params[idx];
    memset(params, 0, sizeof(*params));
    struct pcm_params *capture_params = &params->capture.params;
    struct pcm_params *playback_params = &params->playback.params;

    /*
     * get uac information
     */
    uac_stream_inform_t inform[3] = {0};
    uac_substream_inform_t *play_inform = &inform[0].subs[UAC_SUBSTREAM_PLAYBACK];
    uac_substream_inform_t *cap_inform = &inform[0].subs[UAC_SUBSTREAM_CAPTURE];

    ret = usb_host_uac_get_inform(idx, inform, 3);
    if (ret < 0) {
        printf("Error in getting information of uac(%d)\n", idx);
        return;
    }

    if (play_inform->pcm_name)
        uac_playback = pcm_get(play_inform->pcm_name);

    if (cap_inform->pcm_name)
        uac_capture = pcm_get(cap_inform->pcm_name);

    /*
     * 1. init playback pcm params
     * 2. probe audio data for play
     * 3. enable pcm playback dev
     * 4. set playback volume
     * 5. start pcm playback dev
     */
    if (uac_playback) {
        params->playback.pcm_dev = uac_playback;

        if (uac_init_pcm_params(play_inform, playback_params) < 0)
            goto skip_play;

        if (uac_audio_probe_audio_data(playback_params, &params->audio, &params->frames) < 0)
            goto skip_play;

        if (pcm_enable(uac_playback, playback_params) < 0)
            goto skip_play;

        if (uac_volume_control(uac_playback, UAC_SET_VOLUME, &volume) < 0)
            goto skip_play;

        if (pcm_start(uac_playback) < 0)
            goto skip_play;

        goto init_cap;
skip_play:
        skip_playback = 1;
    }

init_cap:
    /*
     * 1. init capture pcm params
     * 2. alloc buf for saving records
     * 3. enable pcm capture dev
     * 4. start pcm capture dev
     */
    if (uac_capture) {
        params->capture.pcm_dev = uac_capture;

        if (uac_init_pcm_params(cap_inform, capture_params) < 0)
            goto skip_cap;

        buf_size = pcm_frame_size(capture_params) * DEFAULT_CAPTURE_TIME_SEC \
                            * pcm_data_sample_rate(capture_params->pcm_sample_rate);
        buffer = malloc(buf_size);
        if (!buffer)
            goto skip_cap;

        if (pcm_enable(uac_capture, capture_params) < 0)
            goto skip_cap;

        if (pcm_start(uac_capture) < 0)
            goto skip_cap;

        goto init_convert;
skip_cap:
        skip_capture = 1;
    }

init_convert:
    /*
     * if audio format incompatible between capture and playback,
     * need convert audio format of record data for playback
     */
    if (uac_playback && uac_capture) {
        /* support converting channel and data fmt, excluding sample rate */
        if (capture_params->pcm_sample_rate != playback_params->pcm_sample_rate) {
            need_convert = 1;
            skip_convert = 1;
            goto start;
        }

        if (capture_params->channels != playback_params->channels ||
            capture_params->pcm_data_fmt != playback_params->pcm_data_fmt)
            need_convert = 1;

        if (need_convert) {
            convert_buf_size = pcm_frame_size(playback_params) * DEFAULT_CAPTURE_TIME_SEC \
                            * pcm_data_sample_rate(playback_params->pcm_sample_rate);
            convert_buffer = malloc(convert_buf_size);
            if (!convert_buffer)
                skip_convert = 1;
        }
    }
start:
    while (1) {
        /*
         * 开启播放线程
         */
        if (!skip_playback) {
            params->play_state = PLAY_THREAD_STATE_WAIT;
            playback_thread = thread_create("write_thread", 4096, uac_playback_thread_func, &idx);
        } else {
            params->play_state = PLAY_THREAD_STATE_OK;
        }

        /*
         * 开始录音
         */
        if (!skip_capture) {
            ret = uac_capture_read_frames(idx, uac_capture, buffer, buf_size);
            if (ret < 0)
                goto err_exit;
        }

        /*
         * 等待播放线程结束
         */
        while (params->play_state == PLAY_THREAD_STATE_WAIT)
            msleep(1);

        if (params->play_state == PLAY_THREAD_STATE_ERR)
            goto err_exit;

        /*
         * 播放录音数据
         */
        if (need_convert && !skip_convert) {
            /*
             * 音频格式转换
             */
            ret = uac_pcm_data_convert(capture_params, buffer, buf_size,
                    playback_params, convert_buffer, convert_buf_size);
            if (ret < 0)
                goto err_exit;

            ret = uac_playback_write_frames(idx, uac_playback, convert_buffer, convert_buf_size);
            if (ret < 0)
                goto err_exit;

        } else if (!need_convert && !skip_playback) {
            ret = uac_playback_write_frames(idx, uac_playback, buffer, buf_size);
            if (ret < 0)
                goto err_exit;
        }
    }

err_exit:
    if (playback_thread)
        thread_join(playback_thread, NULL);

    if (!skip_capture) {
        pcm_stop(uac_capture);
        pcm_disable(uac_capture);
    }

    if (!skip_playback) {
        pcm_stop(uac_playback);
        pcm_disable(uac_playback);
    }

    if (buffer)
        free(buffer);

    if (convert_buffer)
        free(convert_buffer);
}

static void uac_insert_wakeup_display(u8 devices_bit)
{
    int i;

    for (i = 0; i < UAC_MINORS; i++) {
        /* open device */
        if ((devices_bit & BIT(i)) && !(uac_devices_open_bit & BIT(i))) {
            uac_params[i].minor = i;
            uac_params[i].uac_thread = thread_create("uac_test_thread", 16*1024, usb_uac_test, &uac_params[i].minor);

            uac_devices_open_bit |= BIT(i);
        }

        /* close device */
        if (!(devices_bit & BIT(i)) && (uac_devices_open_bit & BIT(i))) {
            if (uac_params[i].uac_thread)
                thread_join(uac_params[i].uac_thread, NULL);

            memset(&uac_params[i], 0, sizeof(struct uac_param));

            uac_devices_open_bit &= ~BIT(i);
        }
    }
}

void uac_mutil_play_record_test(void)
{
    usb_host_uac_register_callback(uac_insert_wakeup_display);
}
