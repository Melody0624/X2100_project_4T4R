#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/pcm.h>
#include <driver/pcm_adapter.h>
#include <include_bin.h>

#include <libmad/mad.h>

#ifndef CONFIG_LIBMAD
#error CONFIG_LIBMAD not configure!!
#endif

#ifndef CONFIG_PCM
#error CONFIG_PCM not configure!!
#endif

INCBIN(music, "example/resource/48000_stereo_fltp.mp3");

struct buffer {
    const unsigned char *start;
    unsigned long length;
};

struct mp3_playback {
    struct pcm_device *dai;
    struct pcm_device *codec;
    struct pcm_adapter *adapter;
    int started;
};

struct mp3_decoder_ctx {
    struct buffer input;
    struct mp3_playback playback;
};

static struct pcm_params playback_params = {
    .channels = 2,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_48000,
    .pcm_interface = pcm_interface_i2s,
    .i2s_frame_mode = i2s_LR_mode,
    .i2s_bclk_direction = i2s_bclk_codec_master,
    .i2s_frame_direction = i2s_frame_codec_master,
};

static int gpio_pa_enable = GPIO_PC(5);

static int mp3_probe_stream_info(const unsigned char *data, unsigned long size,
                                 unsigned int *samplerate, unsigned int *channels)
{
    struct mad_stream stream;
    struct mad_header header;
    int ret = -1;

    mad_stream_init(&stream);
    mad_header_init(&header);
    mad_stream_buffer(&stream, data, size);

    while (1) {
        if (mad_stream_sync(&stream) == -1)
            break;

        if (mad_header_decode(&header, &stream) == 0) {
            *samplerate = header.samplerate;
            *channels = MAD_NCHANNELS(&header);
            ret = 0;
            break;
        }

        if (!MAD_RECOVERABLE(stream.error) || stream.error == MAD_ERROR_BUFLEN)
            break;
    }

    mad_header_finish(&header);
    mad_stream_finish(&stream);

    return ret;
}

static int pcm_rate_from_hz(unsigned int hz, pcm_sample_rate *rate)
{
    switch (hz) {
    case 5512:
        *rate = pcm_rate_5512;
        return 0;
    case 8000:
        *rate = pcm_rate_8000;
        return 0;
    case 11025:
        *rate = pcm_rate_11025;
        return 0;
    case 12000:
        *rate = pcm_rate_12000;
        return 0;
    case 16000:
        *rate = pcm_rate_16000;
        return 0;
    case 22050:
        *rate = pcm_rate_22050;
        return 0;
    case 24000:
        *rate = pcm_rate_24000;
        return 0;
    case 32000:
        *rate = pcm_rate_32000;
        return 0;
    case 44100:
        *rate = pcm_rate_44100;
        return 0;
    case 48000:
        *rate = pcm_rate_48000;
        return 0;
    case 64000:
        *rate = pcm_rate_64000;
        return 0;
    case 88200:
        *rate = pcm_rate_88200;
        return 0;
    case 96000:
        *rate = pcm_rate_96000;
        return 0;
    case 176400:
        *rate = pcm_rate_176400;
        return 0;
    case 192000:
        *rate = pcm_rate_192000;
        return 0;
    default:
        return -1;
    }
}

static int mp3_pcm_setup(struct mp3_playback *pb, unsigned int samplerate, unsigned int channels)
{
    pcm_sample_rate src_rate;
    struct pcm_adapter_param adapter_param = {
        .channels = channels,
        .data_fmt = pcm_fmt_S16LE,
    };

    if (pcm_rate_from_hz(samplerate, &src_rate) < 0) {
        printf("mp3: unsupported samplerate %u\n", samplerate);
        return -1;
    }
    adapter_param.sample_rate = src_rate;

    pb->dai = pcm_get("aic-playback");
    pb->codec = pcm_get("icodec-playback");

    if (!pb->dai || !pb->codec) {
        printf("mp3: pcm device not found\n");
        return -1;
    }

    playback_params.channels = 1;
    playback_params.pcm_sample_rate = adapter_param.sample_rate;

    pcm_private_ctrl(pb->dai, "sysclk-set-rate",
                     256 * pcm_data_sample_rate(adapter_param.sample_rate));

    pcm_enable(pb->codec, &playback_params);
    pcm_enable(pb->dai, &playback_params);
    pcm_start(pb->codec);
    pcm_start(pb->dai);

    pcm_set_volume(pb->codec, 80);

    pb->adapter = pcm_adapter_create(pb->dai, &adapter_param);
    if (!pb->adapter) {
        printf("mp3: adapter create failed\n");
        pcm_disable(pb->dai);
        pcm_disable(pb->codec);
        return -1;
    }

    return 0;
}

static void mp3_pcm_teardown(struct mp3_playback *pb)
{
    if (pb->adapter) {
        pcm_adapter_release(pb->adapter);
        pb->adapter = NULL;
    }

    if (pb->dai)
        pcm_disable(pb->dai);
    if (pb->codec)
        pcm_disable(pb->codec);
}

static void mp3_playback_start(struct mp3_playback *pb)
{
    if (pb->started)
        return;

    if (gpio_pa_enable >= 0)
        gpio_direction_output(gpio_pa_enable, 1);

    pb->started = 1;
}

static void mp3_playback_stop(struct mp3_playback *pb)
{
    if (!pb->started)
        return;

    if (gpio_pa_enable >= 0)
        gpio_direction_output(gpio_pa_enable, 0);

    pb->started = 0;
}

static inline signed int scale(mad_fixed_t sample)
{
    sample += (1L << (MAD_F_FRACBITS - 16));

    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;

    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static enum mad_flow input(void *data, struct mad_stream *stream)
{
    struct mp3_decoder_ctx *ctx = data;

    if (!ctx->input.length)
        return MAD_FLOW_STOP;

    mad_stream_buffer(stream, ctx->input.start, ctx->input.length);
    ctx->input.length = 0;

    return MAD_FLOW_CONTINUE;
}

static enum mad_flow output(void *data,
                            struct mad_header const *header,
                            struct mad_pcm *pcm)
{
    struct mp3_decoder_ctx *ctx = data;
    struct mp3_playback *pb = &ctx->playback;
    unsigned int nsamples = pcm->length;
    unsigned int channels = (pcm->channels == 2) ? 2 : 1;
    static signed short pcm_out[1152 * 2];
    unsigned int i;
    int ret;

    (void)header;

    if (!pb->adapter)
        return MAD_FLOW_STOP;

    if (channels == 1) {
        const mad_fixed_t *left_ch = pcm->samples[0];

        for (i = 0; i < nsamples; i++)
            pcm_out[i] = (signed short)scale(left_ch[i]);
    } else {
        const mad_fixed_t *left_ch = pcm->samples[0];
        const mad_fixed_t *right_ch = pcm->samples[1];

        for (i = 0; i < nsamples; i++) {
            pcm_out[2 * i] = (signed short)scale(left_ch[i]);
            pcm_out[2 * i + 1] = (signed short)scale(right_ch[i]);
        }
    }

    ret = pcm_adapter_write_frame(pb->adapter, pcm_out, nsamples);
    if (ret < 0) {
        printf("mp3: write failed %d\n", ret);
        return MAD_FLOW_STOP;
    }

    return MAD_FLOW_CONTINUE;
}

static enum mad_flow error(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
    struct mp3_decoder_ctx *ctx = data;
    struct buffer *buffer = &ctx->input;

    (void)frame;

    fprintf(stderr, "decoding error 0x%04x (%s) at byte offset %td\n",
            stream->error, mad_stream_errorstr(stream),
            (ptrdiff_t)(stream->this_frame - buffer->start));

    return MAD_FLOW_CONTINUE;
}

static int decode_mp3(const unsigned char *start, unsigned long length)
{
    struct mp3_decoder_ctx ctx;
    struct mad_decoder decoder;
    int result;
    unsigned int samplerate = 0;
    unsigned int channels = 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.input.start = start;
    ctx.input.length = length;

    if (mp3_probe_stream_info(start, length, &samplerate, &channels) < 0) {
        printf("mp3: probe header failed\n");
        return -1;
    }

    if (mp3_pcm_setup(&ctx.playback, samplerate, channels) < 0)
        return -1;

    mp3_playback_start(&ctx.playback);

    mad_decoder_init(&decoder, &ctx,
                     input, 0, 0, output,
                     error, 0);

    result = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
    mad_decoder_finish(&decoder);

    mp3_playback_stop(&ctx.playback);
    mp3_pcm_teardown(&ctx.playback);

    return result;
}

void mad_pcm_test(void)
{
    decode_mp3(musicData, musicSize);
}
