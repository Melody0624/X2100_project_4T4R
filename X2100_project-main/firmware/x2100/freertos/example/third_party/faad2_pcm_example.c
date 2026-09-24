#include <stdio.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/pcm.h>
#include <driver/pcm_adapter.h>
#include <include_bin.h>
#include <os.h>

#include <neaacdec.h>

#ifndef CONFIG_FAAD2
#error CONFIG_FAAD2 not configure!!
#endif

#ifndef CONFIG_PCM
#error CONFIG_PCM not configure!!
#endif

INCBIN(aac_music, "example/resource/48000_stereo_fltp.aac");

struct aac_playback {
    struct pcm_device *dai;
    struct pcm_device *codec;
    struct pcm_adapter *adapter;
    int started;
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

static int aac_pcm_setup(struct aac_playback *pb, unsigned int samplerate, unsigned int channels)
{
    pcm_sample_rate src_rate;
    struct pcm_adapter_param adapter_param = {
        .channels = channels,
        .data_fmt = pcm_fmt_S16LE,
    };

    if (pcm_rate_from_hz(samplerate, &src_rate) < 0) {
        printf("aac: unsupported samplerate %u\n", samplerate);
        return -1;
    }
    adapter_param.sample_rate = src_rate;

    pb->dai = pcm_get("aic-playback");
    pb->codec = pcm_get("icodec-playback");

    if (!pb->dai || !pb->codec) {
        printf("aac: pcm device not found\n");
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
        printf("aac: adapter create failed\n");
        pcm_disable(pb->dai);
        pcm_disable(pb->codec);
        return -1;
    }

    return 0;
}

static void aac_pcm_teardown(struct aac_playback *pb)
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

static void aac_playback_start(struct aac_playback *pb)
{
    if (pb->started)
        return;

    if (gpio_pa_enable >= 0)
        gpio_direction_output(gpio_pa_enable, 1);

    pb->started = 1;
}

static void aac_playback_stop(struct aac_playback *pb)
{
    if (!pb->started)
        return;

    if (gpio_pa_enable >= 0)
        gpio_direction_output(gpio_pa_enable, 0);

    pb->started = 0;
}

static int decode_aac(const unsigned char *start, unsigned long length)
{
    NeAACDecHandle decoder;
    NeAACDecConfigurationPtr config;
    NeAACDecFrameInfo info;
    struct aac_playback playback;
    unsigned long samplerate = 0;
    unsigned char channels = 0;
    unsigned long offset = 0;
    long init_bytes;

    memset(&playback, 0, sizeof(playback));

    decoder = NeAACDecOpen();
    if (!decoder) {
        printf("aac: decoder open failed\n");
        return -1;
    }

    config = NeAACDecGetCurrentConfiguration(decoder);
    config->defObjectType = LC;
    config->defSampleRate = 48000;
    config->outputFormat = FAAD_FMT_16BIT;
    if (!NeAACDecSetConfiguration(decoder, config)) {
        printf("aac: decoder config failed\n");
        NeAACDecClose(decoder);
        return -1;
    }

    init_bytes = NeAACDecInit(decoder, (unsigned char *)start, length, &samplerate, &channels);
    if (init_bytes < 0) {
        printf("aac: init failed\n");
        NeAACDecClose(decoder);
        return -1;
    }
    offset = (unsigned long)init_bytes;

    if (aac_pcm_setup(&playback, samplerate, channels) < 0) {
        NeAACDecClose(decoder);
        return -1;
    }

    aac_playback_start(&playback);

    while (offset < length) {
        void *pcm;
        int frames;
        int ret;

        pcm = NeAACDecDecode(decoder, &info, (unsigned char *)start + offset, length - offset);
        if (info.bytesconsumed == 0)
            break;
        offset += info.bytesconsumed;

        if (info.error) {
            printf("aac: decode error %s\n", NeAACDecGetErrorMessage(info.error));
            continue;
        }

        if (!pcm || info.samples == 0 || info.channels == 0)
            continue;

        frames = info.samples / info.channels;
        ret = pcm_adapter_write_frame(playback.adapter, pcm, frames);
        if (ret < 0) {
            printf("aac: write failed %d\n", ret);
            break;
        }
    }

    aac_playback_stop(&playback);
    aac_pcm_teardown(&playback);
    NeAACDecClose(decoder);

    return 0;
}

static void aac_thread(void *data)
{
    (void)data;
    decode_aac(aac_musicData, aac_musicSize);
}

void faad2_pcm_test(void)
{
    /* faad2 栈开销比较大，建议不低于64k */
    thread_create("faad2-aac", 64*1024, aac_thread, NULL);
}
