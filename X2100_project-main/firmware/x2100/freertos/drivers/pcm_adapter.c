#include <common.h>
#include <driver/pcm.h>
#include <driver/pcm_adapter.h>
#include "pcm_channels_convert.c"
#include "pcm_format_convert.c"
#include "pcm_sample_rate_convert.c"

struct pcm_adapter {
    int src_channels;
    pcm_data_fmt src_fmt;
    pcm_sample_rate src_sample_rate;
    struct pcm_device *dev;
};

struct pcm_adapter *pcm_adapter_create(struct pcm_device *dev, struct pcm_adapter_param *adapter_param)
{
    struct pcm_adapter *adapter = malloc(sizeof(*adapter));

    adapter->dev = dev;
    adapter->src_fmt = adapter_param->data_fmt;
    adapter->src_channels = adapter_param->channels;
    adapter->src_sample_rate = adapter_param->sample_rate;

    return adapter;
}

void pcm_adapter_release(struct pcm_adapter *adapter)
{
    free(adapter);
}

struct adapter_param {
    int fmt;
    void *buf;
    int channels;
    int frame_count;
    int sample_rate;
};

static void set_param(struct adapter_param *param, void *buf, int frame_count,
                      int channels, int format, int sample_rate)
{
    param->buf = buf;
    param->fmt = format;
    param->channels = channels;
    param->frame_count = frame_count;
    param->sample_rate = sample_rate;
}

#define set_frame_count()                                                   \
    do {                                                                    \
        src_frame_size = pcm_data_sample_size(src->fmt) * src->channels;    \
        dst_frame_size = pcm_data_sample_size(dst->fmt) * dst->channels;    \
                                                                            \
        frame_count = buf_size / dst_frame_size;                            \
        if (frame_count > src->frame_count)                                 \
            frame_count = src->frame_count;                                 \
                                                                            \
        dst->buf = buf;                                                     \
        dst->frame_count = frame_count;                                     \
                                                                            \
        src->frame_count -= frame_count;                                    \
        src->buf += frame_count * src_frame_size;                           \
    } while (0)

static int do_channel_convert(struct pcm_params *target, struct adapter_param *src, struct adapter_param *dst, void *buf, int buf_size)
{
    dst->fmt = src->fmt;
    dst->channels = target->channels;
    dst->sample_rate = src->sample_rate;

    if (src->channels == dst->channels) {
        dst->frame_count = src->frame_count;
        dst->buf = src->buf;
        src->frame_count = 0;

        return 0;
    }

    int frame_count, src_frame_size, dst_frame_size;

    void *src_buf = src->buf;
    set_frame_count();

    if (src->channels == 1 && dst->channels == 2)
        return pcm_channels_convert_1_to_2(dst->buf, src_buf, frame_count, src->fmt);
    if (src->channels == 2 && dst->channels == 1)
        return pcm_channels_convert_2_to_1(dst->buf, src_buf, frame_count, src->fmt);

    printf("pcm: channels convert check failed, not suport channel:%d to %d\n", src->channels, dst->channels);

    return -1;
}

static int do_format_convert(struct pcm_params *target, struct adapter_param *src, struct adapter_param *dst, void *buf, int buf_size)
{
    dst->channels = src->channels;
    dst->fmt = target->pcm_data_fmt;
    dst->sample_rate = src->sample_rate;

    if (src->fmt == dst->fmt) {
        dst->frame_count = src->frame_count;
        dst->buf = src->buf;
        src->frame_count = 0;

        return 0;
    }

    int frame_count, src_frame_size, dst_frame_size;

    void *src_buf = src->buf;
    set_frame_count();

    return pcm_format_convert(dst->buf, src_buf, frame_count * src->channels, src->fmt, dst->fmt);
}

static int do_sample_rate_convert(struct pcm_params *target, struct adapter_param *src, struct adapter_param *dst, void *buf, int buf_size)
{
    dst->channels = src->channels;
    dst->fmt = src->fmt;
    dst->sample_rate = target->pcm_sample_rate;

    if (src->sample_rate == dst->sample_rate) {
        dst->frame_count = src->frame_count;
        dst->buf = src->buf;
        src->frame_count = 0;
        return 0;
    }

    dst->buf = buf;

    unsigned int src_rate = pcm_data_sample_rate(src->sample_rate);
    unsigned int dst_rate = pcm_data_sample_rate(dst->sample_rate);
    int frame_size = pcm_data_sample_size(src->fmt) * src->channels;

    int max_frame_count = (buf_size / frame_size) * src_rate / dst_rate;
    int frame_count = (src->frame_count > max_frame_count) ? max_frame_count : src->frame_count;

    int frames = pcm_sample_rate_convert(dst->buf, src->buf, frame_count,
                                         src->channels, src->fmt, src->sample_rate, dst->sample_rate);
    if (frames < 0)
        return frames;

    dst->frame_count = frames;
    src->frame_count -= frame_count;
    src->buf += frame_count * frame_size;

    return 0;
}

int pcm_adapter_write_frame_timeout(
    struct pcm_adapter *adapter, void *buf, int frame_count, unsigned int timeout_ms)
{
    int ret;
    struct adapter_param src;
    unsigned char mems[3][128];
    struct adapter_param params[3];
    struct pcm_params *target = pcm_get_device_params(adapter->dev);

    memset(params, 0, sizeof(params));
    set_param(&src, buf, frame_count, adapter->src_channels,
              adapter->src_fmt, adapter->src_sample_rate);

    while(1) {

        /* 采样率转换插件 */
        if (!params[0].frame_count) {
            if (src.frame_count) {
                ret = do_sample_rate_convert(target, &src, &params[0], mems[0], sizeof(mems[0]));
                if (ret < 0)
                    return -3;
            }
        }

        /* 通道转换插件 */
        if (!params[1].frame_count) {
            if (params[0].frame_count) {
                ret = do_channel_convert(target, &params[0], &params[1], mems[1], sizeof(mems[1]));
                if (ret < 0)
                    return -1;
            }
        }

        /* 格式转换插件 */
        if (!params[2].frame_count) {
            if (params[1].frame_count) {
                ret = do_format_convert(target, &params[1], &params[2], mems[2], sizeof(mems[2]));
                if (ret < 0)
                    return -2;
            }
        }

        if (!params[2].frame_count)
            break;

        ret = pcm_write_frame_timeout(adapter->dev, params[2].buf, params[2].frame_count, timeout_ms);
        if (ret < 0)
            return ret;

        params[2].frame_count = 0;
    }

    return 0;
}

int pcm_adapter_write_frame(struct pcm_adapter *adapter, void *buf, int frame_count)
{
    return pcm_adapter_write_frame_timeout(adapter, buf, frame_count, OS_TIMEOUT_NOT_LIMIT_MS);
}