#include <os.h>
#include <common.h>
#include <stdlib.h>
#include <driver/clk.h>
#include <driver/dma.h>
#include <soc/base.h>
#include <driver/pcm.h>
#include <spinlock.h>
#include "aic_regs.h"
#include "aic_gpio.c"
#include "aic.h"

struct aic_data {
    struct mutex lock;

    int inner_codec;
    struct aic_params playback;
    struct aic_params capture;
};

struct aic_data aic_datas[AIC_NUMS];

static struct pcm_dev_data aic_playback_device[AIC_NUMS];
static struct pcm_dev_data aic_capture_device[AIC_NUMS];

static const char *gate_names[AIC_NUMS] = {
    "gate_i2s0", "gate_i2s1", "gate_i2s2", "gate_i2s3", "gate_pcm"
};

static const char *rx_names[AIC_NUMS] = {
    "cgu_i2s0", "cgu_i2s0", "cgu_i2s2", NULL, "i2s_pcm"
};

static const char *tx_names[AIC_NUMS] = {
    "cgu_i2s1", "cgu_i2s1", NULL, "cgu_i2s3", "i2s_pcm"
};

static const char *playback_name[AIC_NUMS] = {
    "aic0-playback", "aic1-playback", NULL, "aic3-playback", "aic4-playback"
};

static const char *capture_name[AIC_NUMS] = {
    "aic0-icodec-capture", "aic1-capture", "aic2-capture", NULL, "aic4-capture"
};

static int split_clk[AIC_NUMS] = {
    1, 1, 1, 1, 0
};

static const unsigned long iobase[] = {
    KSEG1ADDR(AUDIO_AIC0_BASE),
    KSEG1ADDR(AUDIO_AIC1_BASE),
    KSEG1ADDR(AUDIO_AIC2_BASE),
    KSEG1ADDR(AUDIO_AIC3_BASE),
    KSEG1ADDR(AUDIO_AIC4_BASE),
};

#define AIC_ADDR(id, reg) (io_addr(iobase[id] + reg))

static inline void aic_write_reg(int id, unsigned int reg, unsigned int value)
{
    *AIC_ADDR(id, reg) = value;
}

static inline unsigned int aic_read_reg(int id, unsigned int reg)
{
    return *AIC_ADDR(id, reg);
}

static inline void aic_set_bit(int id, unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(AIC_ADDR(id, reg), start, end, val);
}

static inline unsigned int aic_get_bit(int id, unsigned int reg, int start, int end)
{
    return get_bit_field_v(AIC_ADDR(id, reg), start, end);
}

static int to_data_bits(int format)
{
    if (format == pcm_fmt_S8 || format == pcm_fmt_U8)
        return 8;
    if (format == pcm_fmt_S16LE || format == pcm_fmt_U16LE)
        return 16;
    if (format == pcm_fmt_S24LE || format == pcm_fmt_U24LE)
        return 24;
    if (format == pcm_fmt_S32LE || format == pcm_fmt_U32LE)
        return 32;
    return 16;
}

static inline int to_ss(int data_bits)
{
    if (data_bits == 8) return 0;
    if (data_bits == 12) return 1;
    if (data_bits == 13) return 2;
    if (data_bits == 16) return 3;
    if (data_bits == 18) return 4;
    if (data_bits == 20) return 5;
    if (data_bits == 24) return 6;
    if (data_bits == 32) return 7;
    return 3;
}

static int to_aic_id(struct pcm_dev_data *dev)
{
    int id = 0;

    if (dev->stream_type == pcm_stream_capture)
        id = (dev - aic_capture_device);
    else
        id = (dev - aic_playback_device);

    if (id > AIC_NUMS - 1)
        return -1;

    return id;
}

static inline void aic_dump_regs(int id)
{
    printf("BAICRCFG: 0x%x\n", aic_read_reg(id, BAICRCFG));
    printf("BAICTCFG: 0x%x\n", aic_read_reg(id, BAICTCFG));
    printf("BAICRDIV: 0x%x\n", aic_read_reg(id, BAICRDIV));
    printf("BAICTDIV: 0x%x\n", aic_read_reg(id, BAICTDIV));
    printf("BAICCCR: 0x%x\n",  aic_read_reg(id, BAICCCR));
}

static void aic_start_playback(int id)
{
    aic_set_bit(id, BAICCCR, TEN, 1);
}

static void aic_stop_playback(int id)
{
    aic_set_bit(id, BAICCCR, TEN, 0);
}

static void aic_start_capture(int id)
{
    aic_set_bit(id, BAICCCR, REN, 1);
}

static void aic_stop_capture(int id)
{
    aic_set_bit(id, BAICCCR, REN, 0);
}

void aic_start_playback_dma(int aic_id);
void aic_stop_playback_dma(int aic_id);
void aic_init_playback_dma(struct aic_params *param);
int aic_write_frame(void *mem, int frames, unsigned int timeout_ms, int aic_id);

void aic_start_capture_dma(int aic_id);
void aic_stop_capture_dma(int aic_id);
void aic_init_capture_dma(struct aic_params *param);
int aic_read_frame(void *mem, int frames, unsigned int timeout_ms, int aic_id);

static int aic_playback_pcm_start(struct pcm_dev_data *dev)
{
    int id = to_aic_id(dev);

    mutex_lock(&aic_datas[id].lock);

    aic_start_playback_dma(id);
    aic_start_playback(id);
    aic_dump_regs(id);
    mutex_unlock(&aic_datas[id].lock);
    return 0;
}

static void aic_playback_pcm_stop(struct pcm_dev_data *dev)
{
    int id = to_aic_id(dev);

    mutex_lock(&aic_datas[id].lock);

    aic_stop_playback_dma(id);
    msleep(10);
    aic_stop_playback(id);

    mutex_unlock(&aic_datas[id].lock);
}

static int aic_capture_pcm_start(struct pcm_dev_data *dev)
{
    int id = to_aic_id(dev);

    mutex_lock(&aic_datas[id].lock);

    aic_start_capture(id);
    aic_start_capture_dma(id);

    mutex_unlock(&aic_datas[id].lock);
    return 0;
}

static void aic_capture_pcm_stop(struct pcm_dev_data *dev)
{
    int id = to_aic_id(dev);

    mutex_lock(&aic_datas[id].lock);

    aic_stop_capture_dma(id);
    msleep(10);
    aic_stop_capture(id);

    mutex_unlock(&aic_datas[id].lock);
}

static int aic_playback_pcm_write_frame(struct pcm_dev_data *dev, void *buf, int frame_count, unsigned int timeout_ms)
{
    int id = to_aic_id(dev);
    return aic_write_frame(buf, frame_count, timeout_ms, id);
}

static int aic_capture_pcm_read_frame(struct pcm_dev_data *dev, void *buf, int frame_count, unsigned int timeout_ms)
{
    int id = to_aic_id(dev);
    return aic_read_frame(buf, frame_count, timeout_ms, id);
}

static void aic_init_setting(struct aic_params *param)
{
    int cfg_reg = param->is_split_clk && param->is_capture ? BAICRCFG : BAICTCFG;
    int div_reg = param->is_split_clk && param->is_capture ? BAICRDIV : BAICTDIV;

    aic_write_reg(param->id, BAICTLCR, param->is_split_clk);

    unsigned long cfg_value = 0;

    set_bit_field(&cfg_value, R_CHANNEL, param->channels / 2);
    set_bit_field(&cfg_value, R_SWLR, 0);

    set_bit_field(&cfg_value, R_ISYNC, 0);  // Not used Invert SYNC
    set_bit_field(&cfg_value, R_NEG, 0);    // use BCLK rising edge
    set_bit_field(&cfg_value, R_ASVTSU, 0);
    set_bit_field(&cfg_value, ISS, to_ss(param->data_bits));
    set_bit_field(&cfg_value, R_MODE, AIC_I2S); // i2s mode
    set_bit_field(&cfg_value, R_MASTER, param->is_master);
    aic_write_reg(param->id, cfg_reg, cfg_value);

    unsigned long div_value = 0;
    param->sys_freq = clk_get_rate(param->sysclk);

    int bclk = param->sample_rate * 64;
    int bclk_div = ((param->sys_freq + bclk - 1) / bclk) & ~0x01ul;
    set_bit_field(&div_value, R_BCLKDIV, bclk_div);
    set_bit_field(&div_value, R_SYNC_DIV, 3);
    aic_write_reg(param->id, div_reg, div_value);
}

static int aic_capture_gpio_init(struct aic_params *param)
{
    int ret = 0;

    if (param->id == 0)
        return 0;

    ret = aic_rx_gpio_request(param->id, param->channels);

    if (param->sysclk_out)
        aic_rx_mclk_gpio_request(param->id);

    return ret;
}

static int aic_playback_gpio_init(struct aic_params *param)
{
    int ret = 0;

    if (param->id == 0)
        return 0;

    ret = aic_tx_gpio_request(param->id, param->channels);

    if (param->sysclk_out)
        aic_tx_mclk_gpio_request(param->id);

    return ret;
}

void aic_select_inner_codec(int id, int value)
{
    aic_datas[id].inner_codec = value;
}

int aic_is_inner_codec(int id)
{
    return aic_datas[id].inner_codec;
}

static int aic_set_sysclk_rate(struct aic_params *aic_param, unsigned long clk_freq)
{
    int div = 0;
    unsigned long freq;
    int id = aic_param->id;
    int sample_rate = aic_param->sample_rate;

    if (aic_datas[id].inner_codec) {
        div = 256;
        freq = sample_rate *div;

        if (clk_freq != 0 && freq != clk_freq)
            printf("AIC: no support clk freq %ld and forced to use clk freq %ld\n", clk_freq, freq);
    } else {
        if (clk_freq == 0) {
            if (sample_rate <= 16000)
                div = 768;
            else if (sample_rate <= 24000)
                div = 512;
            else if (sample_rate <= 32000)
                div = 384;
            else if (sample_rate <= 48000)
                div = 256;
            else
                div = 256;
            freq = sample_rate * div;
        } else {
            freq = clk_freq;
            div = freq / sample_rate;

            if ((sample_rate <= 16000 && div < 768) || (sample_rate <= 24000 && div < 512) ||
                (sample_rate <= 32000 && div < 384) || (sample_rate <= 48000 && div < 256)) {
                printf("AIC: sample_rate = %d, div can't be %d\n", sample_rate, div);
                return -EINVAL;
            }

            if (div != 256 && div != 384 && div != 512 && div != 768) {
                printf("AIC: set_clk_rate failed, div can't be %d.\n", div);
                return -EINVAL;
            }
        }
    }

    aic_param->sys_freq = freq;
    clk_set_rate(aic_param->sysclk, aic_param->sys_freq);

    return 0;
}

static int aic_enable_clk(struct aic_params *aic_param, int id)
{
    int ret = 0;

    clk_enable(aic_param->gate_clk);

    if (!aic_param->sysclk)
        return -1;

    if (id != 4) {
        ret = aic_set_sysclk_rate(aic_param, aic_param->sys_freq);
        if (ret < 0)
            return ret;
    }
    clk_enable(aic_param->sysclk);

    return 0;
}

static void aic_param_config(struct aic_params *aic_param, struct pcm_params *params)
{
    aic_param->is_split_clk = split_clk[aic_param->id];
    aic_param->channels = params->channels;
    aic_param->buffer_time_ms = params->buffer_time_ms;
    aic_param->period_time_ms = params->period_time_ms;
    aic_param->data_bits = to_data_bits(params->pcm_data_fmt);
    aic_param->sample_rate = pcm_data_sample_rate(params->pcm_sample_rate);
    aic_param->is_master = params->i2s_bclk_direction == i2s_bclk_codec_slave;
}

static int aic_playback_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    int ret = 0;
    int id = to_aic_id(dev);
    struct aic_data *aic = &aic_datas[id];
    int is_capture = dev->stream_type == pcm_stream_capture;
    struct aic_params *aic_param = is_capture ? &aic->capture : &aic->playback;

    mutex_lock(&aic->lock);

    aic_param->id = id;
    aic_param->is_capture = is_capture;
    aic_param_config(aic_param, params);

    ret = aic_enable_clk(aic_param, id);
    if (ret < 0)
        goto unlock;

    ret = aic_playback_gpio_init(aic_param);
    if (ret < 0)
        goto unlock;

    aic_init_setting(aic_param);
    aic_init_playback_dma(aic_param);

unlock:
    mutex_unlock(&aic->lock);

    return ret;
}

static void aic_playback_pcm_disable(struct pcm_dev_data *dev)
{
    return;
}

static int aic_capture_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    int ret = 0;
    int id = to_aic_id(dev);
    struct aic_data *aic = &aic_datas[id];
    int is_capture = dev->stream_type == pcm_stream_capture;
    struct aic_params *aic_param = is_capture ? &aic->capture : &aic->playback;

    mutex_lock(&aic->lock);

    aic_param->id = id;
    aic_param->is_capture = is_capture;
    aic_param_config(aic_param, params);

    ret = aic_enable_clk(aic_param, id);
    if (ret < 0)
        goto unlock;

    ret = aic_capture_gpio_init(aic_param);
    if (ret < 0)
        goto unlock;

    aic_init_setting(aic_param);
    aic_init_capture_dma(aic_param);

unlock:
    mutex_unlock(&aic->lock);

    return ret;
}

static void aic_capture_pcm_disable(struct pcm_dev_data *dev)
{
    return;
}

void aic_clk_init(struct aic_data *aic, int id)
{
    struct clk *parent;

    if (rx_names[id]) {

        aic->capture.gate_clk = clk_get(gate_names[id]);
        aic->capture.sysclk = clk_get(rx_names[id]);

        if (id == 4) {
            parent = clk_get("cgu_i2s0");
            clk_set_parent(aic->capture.sysclk, parent);
        } else {

            clk_set_rate(aic->capture.sysclk, 24000000);
            aic->capture.sys_freq = 24000000;
        }
    }

    if (tx_names[id]) {

        aic->playback.gate_clk = clk_get(gate_names[id]);
        aic->playback.sysclk = clk_get(tx_names[id]);

        if (id == 4) {
            parent = clk_get("cgu_i2s0");
            clk_set_parent(aic->playback.sysclk, parent);
        } else {

            clk_set_rate(aic->playback.sysclk, 24000000);
            aic->playback.sys_freq = 24000000;
        }
    }
}

static int aic_pcm_private_ctrl(struct pcm_dev_data *dev, const char *ctrl_id, unsigned long value)
{
    int id = to_aic_id(dev);
    struct aic_params *aic_param;

    if (dev->stream_type == pcm_stream_capture)
        aic_param = &aic_datas[id].capture;
    else
        aic_param = &aic_datas[id].playback;

    if (!strcmp(ctrl_id, "sysclk-set-rate")) {
        if (id == 4) {
            printf("aic: aic4 clk cannot be set, please set its parent clk\n");
            return -1;
        }

        aic_param->sys_freq = value;
        return 0;
    }

    if (!strcmp(ctrl_id, "sysclk-set-output")) {
        aic_param->sysclk_out = value;
        return 0;
    }

    return 0;
}

#define X2000_AIC_FORMATS \
      BIT(pcm_fmt_S8)    | BIT(pcm_fmt_U8) \
    | BIT(pcm_fmt_S16LE) | BIT(pcm_fmt_U16LE) \
    | BIT(pcm_fmt_S24LE) | BIT(pcm_fmt_U24LE) \
    | BIT(pcm_fmt_S32LE) | BIT(pcm_fmt_U32LE)

void aic_playback_register(int aic_id)
{
    struct pcm_dev_data *playback = &aic_playback_device[aic_id];

    playback->name = playback_name[aic_id];
    playback->stream_type = pcm_stream_playback;
    playback->pcm_interface_list = BIT(pcm_interface_i2s);
    playback->pcm_data_fmt_list = X2000_AIC_FORMATS;
    playback->pcm_sample_rate_list = 0;
    playback->i2s_frame_mode_list = 0;
    playback->i2s_bclk_direction_list = 0;
    playback->i2s_frame_direction_list = 0;
    playback->pcm_write_frame = aic_playback_pcm_write_frame;
    playback->pcm_enable = aic_playback_pcm_enable;
    playback->pcm_disable = aic_playback_pcm_disable;
    playback->pcm_start = aic_playback_pcm_start;
    playback->pcm_stop = aic_playback_pcm_stop;
    playback->pcm_private_ctrl = aic_pcm_private_ctrl;
    playback->channels_list = BIT(1)|BIT(2);

    if (aic_id == 3)
        playback->channels_list = BIT(1)|BIT(2)|BIT(4)|BIT(6)|BIT(8);

    pcm_register(playback);
}

void aic_capture_register(int aic_id)
{
    struct pcm_dev_data *capture = &aic_capture_device[aic_id];

    capture->name = capture_name[aic_id];
    capture->stream_type = pcm_stream_capture;
    capture->pcm_interface_list = BIT(pcm_interface_i2s);
    capture->pcm_data_fmt_list = X2000_AIC_FORMATS;
    capture->pcm_sample_rate_list = 0;
    capture->i2s_frame_mode_list = 0;
    capture->i2s_bclk_direction_list = 0;
    capture->i2s_frame_direction_list = 0;
    capture->pcm_read_frame = aic_capture_pcm_read_frame;
    capture->pcm_enable = aic_capture_pcm_enable;
    capture->pcm_disable = aic_capture_pcm_disable;
    capture->pcm_start = aic_capture_pcm_start;
    capture->pcm_stop = aic_capture_pcm_stop;
    capture->pcm_private_ctrl = aic_pcm_private_ctrl;
    capture->channels_list = BIT(1)|BIT(2);

    if (aic_id == 2)
        capture->channels_list = BIT(1)|BIT(2)|BIT(4)|BIT(6)|BIT(8);

    pcm_register(capture);
}

void aic_init(void)
{
    int i = 0;

    for (i = 0; i < AIC_NUMS; i++) {
        mutex_init(&aic_datas[i].lock);
        aic_clk_init(&aic_datas[i], i);

        if (playback_name[i])
            aic_playback_register(i);
        if (capture_name[i])
            aic_capture_register(i);
    }
}