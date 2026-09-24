#include <common.h>
#include <io.h>
#include <soc/base.h>
#include <driver/pcm.h>

#include "dmic_regs.h"

void dmic_write_reg(unsigned int reg, int val)
{
    *DMIC_ADDR(reg) = val;
}

unsigned int dmic_read_reg(unsigned int reg)
{
    return *DMIC_ADDR(reg);
}

void dmic_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(DMIC_ADDR(reg), start, end, val);
}

unsigned int dmic_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(DMIC_ADDR(reg), start, end);
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

#include <driver/clk.h>
#include <driver/irq.h>
#include <driver/gpio.h>

#define DMIC_GPIO_CLK  GPIO_PC(20)
#define DMIC_GPIO_DAT0 GPIO_PC(21)
#define DMIC_GPIO_DAT1 GPIO_PC(22)
#define DMIC_GPIO_DAT2 GPIO_PC(23)
#define DMIC_GPIO_DAT3 GPIO_PC(24)

struct dmic_params dmic_param;

static void init_gpio(int gpio, const char *name, int func)
{
    int ret = gpio_request(gpio, name);
    assert(!ret);
    gpio_set_func(gpio, func);
}

void dmic_init_gpio(struct dmic_params *param)
{
    static int state = 0;
    int channels = param->channels;
    int dma_channels;

    if (!(state & BIT(0))) {
        state |= BIT(0);
        init_gpio(DMIC_GPIO_CLK, "dmic-clk", GPIO_FUNC_0);
        dma_channels = 2;
    }

    if (!(state & BIT(1))) {
        state |= BIT(1);
        init_gpio(DMIC_GPIO_DAT0, "dmic-dat0", GPIO_FUNC_0);
        dma_channels = 2;
    }

    if (channels > 2 && !(state & BIT(2))) {
        state |= BIT(2);
        init_gpio(DMIC_GPIO_DAT1, "dmic-dat1", GPIO_FUNC_0);
        dma_channels = 4;
    }

    if (channels > 4 && !(state & BIT(3))) {
        state |= BIT(3);
        init_gpio(DMIC_GPIO_DAT2, "dmic-dat2", GPIO_FUNC_0);
        dma_channels = 6;
    }

    if (channels > 6 && !(state & BIT(4))) {
        state |= BIT(4);
        init_gpio(DMIC_GPIO_DAT3, "dmic-dat3", GPIO_FUNC_0);
        dma_channels = 8;
    }

    param->dma_channels = dma_channels;
}

void dmic_init_setting(struct dmic_params *param)
{
    dmic_init_gpio(param);
    int channels = param->dma_channels;
    int sample_rate = param->sample_rate;
    int fmt_width = param->data_bits == 16 ? 0 : 1;

    int sr = 0;
    if (sample_rate == 8000)
        sr = 0;
    if (sample_rate == 16000)
        sr = 1;
    if (sample_rate == 48000)
        sr = 2;
    if (sample_rate == 96000)
        sr = 3;

    dmic_set_bit(DMIC_CR0, D_HPF1_EN, 1);
    dmic_set_bit(DMIC_CR0, D_HPF2_EN, 1);
    dmic_set_bit(DMIC_CR0, D_LPF_EN, 1);
    dmic_set_bit(DMIC_CR0, D_SW_LR, 1);

    dmic_set_bit(DMIC_GCR, D_DGAIN, 4);

    dmic_set_bit(DMIC_CR0, D_RESET, 1);
    while (dmic_get_bit(DMIC_CR0, D_RESET));

    dmic_set_bit(DMIC_CR0, D_CHNUM, channels - 1);
    dmic_set_bit(DMIC_CR0, D_OSS, fmt_width);
    dmic_set_bit(DMIC_CR0, D_SR, sr);
}

static void dmic_start(void)
{
    dmic_set_bit(DMIC_CR0, D_DMIC_EN, 1);
}

static void dmic_stop(void)
{
    dmic_set_bit(DMIC_CR0, D_DMIC_EN, 0);
}

#include <driver/pcm.h>

static void dmic_param_config(struct pcm_params *params)
{
    dmic_param.channels = params->channels;
    dmic_param.buffer_time_ms = params->buffer_time_ms;
    dmic_param.period_time_ms = params->period_time_ms;
    dmic_param.data_bits = to_data_bits(params->pcm_data_fmt);
    dmic_param.sample_rate = pcm_data_sample_rate(params->pcm_sample_rate);
}

void dmic_init_capture_dma(struct dmic_params *param);
void calculate_data_offset(int dma_channels);
void dmic_start_capture_dma(void);
void dmic_stop_capture_dma(void);
int dmic_read_frame(void *mem, int frames, unsigned int timeout_ms);

static int dmic_capture_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    dmic_param_config(params);
    dmic_init_setting(&dmic_param);
    dmic_init_capture_dma(&dmic_param);
    calculate_data_offset(dmic_param.dma_channels);
    return 0;
}

static void dmic_capture_pcm_disable(struct pcm_dev_data *dev)
{
    return ;
}

static int dmic_capture_pcm_start(struct pcm_dev_data *dev)
{
    dmic_start();
    dmic_start_capture_dma();
    return 0;
}

static void dmic_capture_pcm_stop(struct pcm_dev_data *dev)
{
    dmic_stop_capture_dma();
    dmic_stop();
}

static int dmic_capture_pcm_read_frame(struct pcm_dev_data *dev, void *buf, int frame_count, unsigned int timeout_ms)
{
    return dmic_read_frame(buf, frame_count, timeout_ms);
}

int dmic_capture_pcm_set_volume(struct pcm_dev_data *dev, int val)
{
    dmic_write_reg(DMIC_GCR, bit_field_val(D_DGAIN, val * 15 / 100));

    return 0;
}

int dmic_capture_pcm_get_volume(struct pcm_dev_data *dev)
{
    int val = dmic_get_bit(DMIC_GCR, D_DGAIN);

    val = val * 100 / 15;

    return val;
}

static int dmic_capture_pcm_private_ctrl(struct pcm_dev_data *dev,
                                    const char *ctrl_id, unsigned long value)
{
    int ret = -EINVAL;

    return ret;
}

static struct pcm_dev_data dmic_capture_device = {
    .name = "dmic-capture",
    .stream_type = pcm_stream_capture,
    .pcm_interface_list = BIT(pcm_interface_dmic),
    .channels_list = BIT(1) | BIT(2) | BIT(4) | BIT(6) | BIT(8),
    .pcm_data_fmt_list = BIT(pcm_fmt_S16LE) | BIT(pcm_fmt_S24LE),
    .pcm_sample_rate_list = BIT(pcm_rate_8000) | BIT(pcm_rate_16000) | BIT(pcm_rate_48000) | BIT(pcm_rate_96000),
    .pcm_enable = dmic_capture_pcm_enable,
    .pcm_disable = dmic_capture_pcm_disable,
    .pcm_set_volume = dmic_capture_pcm_set_volume,
    .pcm_get_volume = dmic_capture_pcm_get_volume,
    .pcm_start = dmic_capture_pcm_start,
    .pcm_stop = dmic_capture_pcm_stop,
    .pcm_read_frame = dmic_capture_pcm_read_frame,
    .pcm_private_ctrl = dmic_capture_pcm_private_ctrl,
};

void dmic_init(void)
{
    struct clk *parent;

    dmic_param.clk = clk_get("i2s_dmic");
    assert(dmic_param.clk);

    parent = clk_get("ext1");
    clk_set_parent(dmic_param.clk, parent);
    clk_put(parent);

    dmic_param.clk_gate = clk_get("gate_dmic");
    assert(dmic_param.clk_gate);

    clk_set_rate(dmic_param.clk, 24000000);

    clk_enable(dmic_param.clk);
    clk_enable(dmic_param.clk_gate);

    pcm_register(&dmic_capture_device);
}
