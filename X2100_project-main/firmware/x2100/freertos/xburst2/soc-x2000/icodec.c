#include <common.h>
#include <os.h>
#include <soc/base.h>
#include <driver/pcm.h>
#include "icodec.h"

#define ICODEC_BASE 0x10020000

#define ICODEC_ADDR(reg) (io_addr(ICODEC_BASE + reg))

struct icodec_data {
    int is_power_on;
    int is_bais;
    int is_standby;
    int hpout_vol;
};

static struct icodec_data icodec;
static DEFINE_MUTEX(lock);

static inline void icodec_write_reg(unsigned int reg, unsigned int value)
{
    *ICODEC_ADDR(reg) = value;
}

static inline unsigned int icodec_read_reg(unsigned int reg)
{
    return *ICODEC_ADDR(reg);
}

static inline void icodec_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(ICODEC_ADDR(reg), start, end, val);
}

static inline unsigned int icodec_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(ICODEC_ADDR(reg), start, end);
}

#ifdef DEBUG
static void icodec_dump_regs(void)
{
    printf("Reset: 0x%x\n", icodec_read_reg(Reset));
    printf("Select_charge: 0x%x\n", icodec_read_reg(Select_charge));
    printf("Chooose_ALC_Gain: 0x%x\n", icodec_read_reg(Chooose_ALC_Gain));
    printf("ADC_DAC_Configure1: 0x%x\n", icodec_read_reg(ADC_DAC_Configure1));
    printf("ADC_DAC_Configure2: 0x%x\n", icodec_read_reg(ADC_DAC_Configure2));
    printf("ADC_DAC_Configure3: 0x%x\n", icodec_read_reg(ADC_DAC_Configure3));
    printf("ADC_DAC_Configure4: 0x%x\n", icodec_read_reg(ADC_DAC_Configure4));

    printf("Control1: 0x%x\n", icodec_read_reg(Control1));
    printf("Control2: 0x%x\n", icodec_read_reg(Control2));
    printf("Control3: 0x%x\n", icodec_read_reg(Control3));
    printf("Control4: 0x%x\n", icodec_read_reg(Control4));
    printf("Control5: 0x%x\n", icodec_read_reg(Control5));
    printf("Control6: 0x%x\n", icodec_read_reg(Control6));
    printf("Control7: 0x%x\n", icodec_read_reg(Control7));

    printf("ALG_MAXL: 0x%x\n", icodec_read_reg(ALG_MAXL));
    printf("ALG_MAXH: 0x%x\n", icodec_read_reg(ALG_MAXH));
    printf("ALG_MINL: 0x%x\n", icodec_read_reg(ALG_MINL));
    printf("ALG_MINH: 0x%x\n", icodec_read_reg(ALG_MINH));
    printf("ALG_CONFIG1: 0x%x\n", icodec_read_reg(ALG_CONFIG1));
    printf("ALG_CONFIG2: 0x%x\n", icodec_read_reg(ALG_CONFIG2));
}
#endif

static void icodec_power_on(void)
{
    if (icodec.is_power_on ++)
        return;

    icodec_set_bit(Control5, POP_CTL, 1);

    icodec_write_reg(Select_charge, 0x01);

    icodec_set_bit(Control1, REF_VOL_EN, 1);

    int i;
    for (i = 0; i <= 0xff; i++) {
        icodec_write_reg(Select_charge, i);
        udelay(200);
    }

    icodec_write_reg(Select_charge, 0x7e);
    msleep(20);
}

static void icodec_power_off(void)
{
    if (--icodec.is_power_on)
        return;

    icodec_write_reg(Select_charge, 0x01);

    icodec_set_bit(Control1, REF_VOL_EN, 0);

    int i;
    for (i = 0; i <= 0xff; i++) {
        icodec_write_reg(Select_charge, i);
        udelay(200);
    }

	/*soft reset*/
    icodec_write_reg(Reset, 0x0);
    udelay(200);
    icodec_write_reg(Reset, 0x3);
}

static void icodec_enable_adc(int bias_enable, int bias_level, int mic_in_gain, int vol)
{
    icodec_set_bit(Control2, ADCL_MUTE, 1);

    icodec_set_bit(Control1, ADC_CUR_EN, 1);

    /* set adc current value*/
    icodec_set_bit(Control3, 0, 3, 8);

    icodec_set_bit(Control2, ADCL_VOL_EN, 1);

    icodec_set_bit(Control3, ADCL_MIC_EN, 1);

    icodec_set_bit(Control3, ADCL_EN, 1);

    icodec_set_bit(Control5, ADCL_CLK_EN, 1);

    icodec_set_bit(Control5, ADCL_ADC_EN, 1);

    icodec_set_bit(Control5, ADCL_ADC_INIT, 1);

    icodec_set_bit(Control5, ADCL_ALC_INIT, 1);

    icodec_set_bit(Control2, ADCL_INITIAL, 1);

    /*set high filter */
    icodec_set_bit(Chooose_ALC_Gain, 0, 2, 1);

    icodec_set_bit(Control3, ADCL_MIC_GAIN, mic_in_gain); // 30b mic gain

    icodec_set_bit(Chooose_ALC_Gain, PGA_GAIN_SEL, 0);

    icodec_set_bit(Control4, ALCL_GAIN, vol); // alc gain

    icodec_set_bit(Control1, BIAS_VOL_EN, bias_enable);

    icodec_set_bit(Control1, BIAS_VOL, bias_level);

    icodec_set_bit(Control2, ADCL_ZERO, 0);

    icodec_set_bit(Control2, ADCL_MUTE, 0);

    udelay(100);

    icodec_set_bit(Control2, ADCL_MUTE, 1);
}

static void icodec_disable_adc(void)
{
    icodec_set_bit(Control2, ADCL_ZERO, 0);

    icodec_set_bit(Control5, ADCL_ADC_EN, 0);

    icodec_set_bit(Control5, ADCL_CLK_EN, 0);

    icodec_set_bit(Control3, ADCL_EN, 0);

    icodec_set_bit(Control2, ADCL_VOL_EN, 0);

    icodec_set_bit(Control1, ADC_CUR_EN, 0);

    icodec_set_bit(Control5, ADCL_ADC_INIT, 0);

    icodec_set_bit(Control5, ADCL_ALC_INIT, 0);

    icodec_set_bit(Control2, ADCL_MUTE, 0);
}

int icodec_get_software_vol(void)
{
    return icodec.is_standby ? icodec.hpout_vol : 100;
}

static void icodec_dac_enter_standby(void)
{
    icodec_set_bit(Control5, DAC_CUR_EN, 1);
    udelay(10);
    icodec_set_bit(Control5, DAC_REF_EN, 1);
    udelay(10);
    icodec_set_bit(Control5, POP_CTL, 2);
    udelay(10);
}

void icodec_dac_exit_standby(void)
{
    icodec_set_bit(Control5, POP_CTL, 1);
    udelay(10);
    icodec_set_bit(Control5, DAC_REF_EN, 0);
    udelay(10);
    icodec_set_bit(Control5, DAC_CUR_EN, 0);
    udelay(10);
}

static void icodec_enable_dac_standby(int is_mute, int vol)
{
    icodec_write_reg(0x20*4, 7<<0);

    icodec_set_bit(Control7, HPOUTL_INIT, 1);
    icodec_set_bit(Control7, HPOUTL_EN, 1);
    icodec_set_bit(Control7, HPOUTL_MUTE, 0);

    icodec_set_bit(Control6, DACL_REF_EN, 1);
    icodec_set_bit(Control6, DACL_CLK_EN, 1);
    icodec_set_bit(Control6, DACL_EN, 1);
    icodec_set_bit(Control6, DACL_INIT, 1);

    icodec_set_bit(Control7, HPOUTL_MUTE, !is_mute);
    icodec_set_bit(Control7, HPOUTL_GAIN, vol);
}

static void icodec_disable_dac_standby(void)
{
    icodec_set_bit(Control7, HPOUTL_GAIN, 0);

    icodec_set_bit(Control7, HPOUTL_MUTE, 0);

    icodec_set_bit(Control7, HPOUTL_INIT, 0);

    icodec_set_bit(Control7, HPOUTL_EN, 0);

    icodec_set_bit(Control6, DACL_EN, 0);

    icodec_set_bit(Control6, DACL_CLK_EN, 0);

    icodec_set_bit(Control6, DACL_REF_EN, 0);

    icodec_set_bit(Control6, DACL_INIT, 0);
}

/*注意， 若icodec没声音，或者滋滋声大概率是这里的问题*/
void icodec_enable_dac(int is_mute, int vol)
{
    mdelay(30);
    icodec_set_bit(Control5, DAC_CUR_EN, 0);
    icodec_set_bit(Control5, DAC_REF_EN, 0);
    mdelay(30);

    unsigned long control7 = icodec_read_reg(Control7);
    set_bit_field(&control7, HPOUTL_INIT, 1);
    set_bit_field(&control7, HPOUTL_EN, 1);
    set_bit_field(&control7, HPOUTL_MUTE, 0);
    icodec_write_reg(Control7, control7);

    unsigned long control6 = icodec_read_reg(Control6);
    set_bit_field(&control6, DACL_REF_EN, 1);
    set_bit_field(&control6, DACL_CLK_EN, 1);
    set_bit_field(&control6, DACL_EN, 1);
    set_bit_field(&control6, DACL_INIT, 1);
    icodec_write_reg(Control6, control6);

    unsigned long control5 = icodec_read_reg(Control5);
    set_bit_field(&control5, DAC_CUR_EN, 1);
    set_bit_field(&control5, DAC_REF_EN, 1);
    set_bit_field(&control5, POP_CTL, 2);
    icodec_write_reg(Control5, control5);

    control7 = icodec_read_reg(Control7);
    set_bit_field(&control7, HPOUTL_MUTE, !is_mute);
    set_bit_field(&control7, HPOUTL_GAIN, vol);
    icodec_write_reg(Control7, control7);
}

static void icodec_disable_dac(void)
{
    icodec_set_bit(Control7, HPOUTL_GAIN, 0); // 增益调到最小

    icodec_set_bit(Control7, HPOUTL_MUTE, 0);

    icodec_set_bit(Control7, HPOUTL_INIT, 0);

    icodec_set_bit(Control7, HPOUTL_EN, 0);

    icodec_set_bit(Control6, DACL_EN, 0);

    icodec_set_bit(Control6, DACL_CLK_EN, 0);

    icodec_set_bit(Control6, DACL_REF_EN, 0);

    icodec_set_bit(Control5, POP_CTL, 1);

    icodec_set_bit(Control5, DAC_REF_EN, 0);

    icodec_set_bit(Control5, DAC_CUR_EN, 0);

    icodec_set_bit(Control6, DACL_INIT, 0);
}

static void icodec_config_adc(int data_bits, int is_master)
{
    int len = 0;

    if (data_bits == 32) len = 3;
    if (data_bits == 24) len = 2;
    if (data_bits == 20) len = 1;
    if (data_bits == 16) len = 0;

    unsigned long adc_dac_cfg2 = icodec_read_reg(ADC_DAC_Configure2);
    set_bit_field(&adc_dac_cfg2, ADC_LENGTH, 3); // 32 bit clk
    set_bit_field(&adc_dac_cfg2, ADC_IO_Master, is_master);
    set_bit_field(&adc_dac_cfg2, ADC_inner_Master, is_master);
    set_bit_field(&adc_dac_cfg2, ADC_RESET, 1);
    set_bit_field(&adc_dac_cfg2, ADC_BIT_POLARITY, 0);
    icodec_write_reg(ADC_DAC_Configure2, adc_dac_cfg2);

    unsigned long adc_dac_cfg1 = 0;
    set_bit_field(&adc_dac_cfg1, ADC_LRC_POL, 0);
    set_bit_field(&adc_dac_cfg1, ADC_VALID_LEN, len);
    set_bit_field(&adc_dac_cfg1, ADC_MODE, 2); // i2s mode
    set_bit_field(&adc_dac_cfg1, ADC_SWAP, 0);
    icodec_write_reg(ADC_DAC_Configure1, adc_dac_cfg1);
}

static void icodec_config_dac(int data_bits, int is_master)
{
    int len = 0;
    if (data_bits == 32) len = 3;
    if (data_bits == 24) len = 2;
    if (data_bits == 20) len = 1;
    if (data_bits == 16) len = 0;

    unsigned long adc_dac_cfg4 = 0;
    set_bit_field(&adc_dac_cfg4, DAC_LENGTH, 3);
    set_bit_field(&adc_dac_cfg4, DAC_RESET, 1);
    set_bit_field(&adc_dac_cfg4, DAC_BIT_POLARITY, 0);
    icodec_write_reg(ADC_DAC_Configure4, adc_dac_cfg4);

    unsigned long adc_dac_cfg2 = icodec_read_reg(ADC_DAC_Configure2);
    set_bit_field(&adc_dac_cfg2, DAC_IO_Master, is_master);
    set_bit_field(&adc_dac_cfg2, DAC_inner_Master, is_master);
    icodec_write_reg(ADC_DAC_Configure2, adc_dac_cfg2);

    unsigned long adc_dac_cfg3 = 0;
    set_bit_field(&adc_dac_cfg3, DAC_LRC_POLARITY, 0);
    set_bit_field(&adc_dac_cfg3, DAC_VALID_LENGTH, len);
    set_bit_field(&adc_dac_cfg3, DAC_MODE, 2); // i2s mode
    set_bit_field(&adc_dac_cfg3, DAC_SWAP, 1);
    icodec_write_reg(ADC_DAC_Configure3, adc_dac_cfg3);
}

#define ICODEC_RATE_LIST \
    BIT(pcm_rate_8000) | \
    BIT(pcm_rate_12000) | \
    BIT(pcm_rate_16000) | \
    BIT(pcm_rate_24000) | \
    BIT(pcm_rate_32000) | \
    BIT(pcm_rate_44100) | \
    BIT(pcm_rate_48000) | \
    BIT(pcm_rate_96000) | \
    BIT(pcm_rate_176400) | \
    BIT(pcm_rate_192000)

static int to_data_bits(int format)
{
    if (format == pcm_fmt_S16LE)
        return 16;
    if (format == pcm_fmt_S24LE)
        return 24;
    if (format == pcm_fmt_S32LE)
        return 32;

    return 16;
}

extern int aic_is_inner_codec(int aic_id);

static int icodec_playback_pcm_enable_standby(struct pcm_dev_data *dev, struct pcm_params *params)
{
    int hpout_mute = 0;
    int hpout_gain = 26;
    int data_bits = to_data_bits(params->pcm_data_fmt);

    mutex_lock(&lock);

    icodec_disable_dac_standby();
    icodec_config_dac(data_bits, params->i2s_bclk_direction == i2s_bclk_codec_master);
    icodec_enable_dac_standby(hpout_mute, hpout_gain);

    mutex_unlock(&lock);

    return 0;
}

static void icodec_playback_pcm_disable_standby(struct pcm_dev_data *dev)
{
    mutex_lock(&lock);
    icodec_disable_dac_standby();
    mutex_unlock(&lock);
}

static int icodec_playback_pcm_enable_complete(struct pcm_dev_data *dev, struct pcm_params *params)
{
    int hpout_mute = 0;  // 默认不静音
    int hpout_gain = 16; // 初始增益
    int data_bits = to_data_bits(params->pcm_data_fmt);

    mutex_lock(&lock);

    icodec_power_on();
    icodec_disable_dac();
    icodec_config_dac(data_bits, params->i2s_bclk_direction == i2s_bclk_codec_master);
    icodec_enable_dac(hpout_mute, hpout_gain);
    /* 初始化两次,解决第一次上电没有声音的问题
     */
    msleep(20);
    icodec_disable_dac();
    icodec_config_dac(data_bits, params->i2s_bclk_direction == i2s_bclk_codec_master);
    icodec_enable_dac(hpout_mute, hpout_gain);

    mutex_unlock(&lock);
    return 0;
}

static void icodec_playback_pcm_disable_complete(struct pcm_dev_data *dev)
{
    mutex_lock(&lock);
    icodec_disable_dac();
    icodec_power_off();
    mutex_unlock(&lock);
}

static int icodec_playback_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    if (icodec.is_standby)
        return icodec_playback_pcm_enable_standby(dev, params);
    else
        return icodec_playback_pcm_enable_complete(dev, params);
}

static void icodec_playback_pcm_disable(struct pcm_dev_data *dev)
{
    if (icodec.is_standby)
        icodec_playback_pcm_disable_standby(dev);
    else
        icodec_playback_pcm_disable_complete(dev);
}

static int icodec_playback_pcm_start(struct pcm_dev_data *dev)
{
    return 0;
}

static void icodec_playback_pcm_stop(struct pcm_dev_data *dev)
{
    return;
}

int icodec_playback_pcm_set_mute(struct pcm_dev_data *dev, int is_mute)
{
    if (icodec_get_bit(Control7, HPOUTL_MUTE) != is_mute)
        icodec_set_bit(Control7, HPOUTL_MUTE, ! is_mute);

    return 0;
}

int icodec_playback_pcm_set_volume(struct pcm_dev_data *dev, int val)
{
    if (icodec.is_standby) {
        icodec.hpout_vol = val;
        if (val == 0)
            icodec_set_bit(Control7, HPOUTL_MUTE, 0);
    } else {
        // 当音量等于0时,选择0作为dac的输出
        val = val * 31 / 100;

        icodec_set_bit(Control7, HPOUTL_GAIN, val);
    }

    return 0;
}

int icodec_playback_pcm_get_volume(struct pcm_dev_data *dev)
{
    if (icodec.is_standby)
        return icodec.hpout_vol;

    int val;

    val = icodec_get_bit(Control7, HPOUTL_GAIN);
    val = val * 100 / 31;

    return val == 0 ? 1 : val;
}

static int icodec_capture_pcm_enable(struct pcm_dev_data *dev, struct pcm_params *params)
{
    int bias_level = 7;     // 偏置电压的寄存器数值(范围0-7)
    int mic_in_gain = 3;
    int mic_alc_gain = 12;
    int data_bits = to_data_bits(params->pcm_data_fmt);

    mutex_lock(&lock);

    icodec_power_on();
    icodec_disable_adc();
    icodec_config_adc(data_bits, params->i2s_bclk_direction == i2s_bclk_codec_master);
    icodec_enable_adc(icodec.is_bais, bias_level, mic_in_gain, mic_alc_gain);
    /* 初始化两次,解决第一次上电没有声音的问题
     */
    msleep(20);
    icodec_disable_adc();
    icodec_config_adc(data_bits, params->i2s_bclk_direction == i2s_bclk_codec_master);
    icodec_enable_adc(icodec.is_bais, bias_level, mic_in_gain, mic_alc_gain);

    mutex_unlock(&lock);
    return 0;
}

static void icodec_capture_pcm_disable(struct pcm_dev_data *dev)
{
    mutex_lock(&lock);
    icodec_disable_adc();
    icodec_power_off();
    mutex_unlock(&lock);
}

static int icodec_capture_pcm_start(struct pcm_dev_data *dev)
{
    return 0;
}

static void icodec_capture_pcm_stop(struct pcm_dev_data *dev)
{
    return;
}

int icodec_capture_pcm_set_volume(struct pcm_dev_data *dev, int val)
{
    icodec_set_bit(Control4, ALCL_GAIN, val *31 / 100);

    return 0;
}

int icodec_capture_pcm_get_volume(struct pcm_dev_data *dev)
{
    int val = icodec_get_bit(Control4, ALCL_GAIN);
    val = val * 100 / 31;

    return val == 0 ? 1 : val;
}

int icodec_capture_pcm_private_ctrl(struct pcm_dev_data *dev,
                                    const char *ctrl_id, unsigned long value)
{
    int ret = 0;

    if (!strcmp(ctrl_id, "bias-on"))
        icodec.is_bais = value;
    else
        ret = -EINVAL;

    return ret;
}

static struct pcm_dev_data icodec_playback_device = {
    .name = "icodec-playback",
    .stream_type = pcm_stream_playback,
    .pcm_interface_list = BIT(pcm_interface_i2s),
    .channels_list = BIT(1),
    .pcm_data_fmt_list = BIT(pcm_fmt_S16LE) | BIT(pcm_fmt_S24LE),
    .pcm_sample_rate_list = ICODEC_RATE_LIST,
    .i2s_frame_mode_list = BIT(i2s_LR_mode),
    .i2s_bclk_direction_list = BIT(i2s_bclk_codec_slave) | BIT(i2s_bclk_codec_master),
    .i2s_frame_direction_list = BIT(i2s_frame_codec_slave) | BIT(i2s_frame_codec_master),
    .pcm_enable = icodec_playback_pcm_enable,
    .pcm_disable = icodec_playback_pcm_disable,
    .pcm_set_volume = icodec_playback_pcm_set_volume,
    .pcm_get_volume = icodec_playback_pcm_get_volume,
    .pcm_set_mute = icodec_playback_pcm_set_mute,
    .pcm_start = icodec_playback_pcm_start,
    .pcm_stop = icodec_playback_pcm_stop,
};

static struct pcm_dev_data icodec_capture_device = {
    .name = "icodec-capture",
    .stream_type = pcm_stream_capture,
    .pcm_interface_list = BIT(pcm_interface_i2s),
    .channels_list = BIT(1),
    .pcm_data_fmt_list = BIT(pcm_fmt_S16LE) | BIT(pcm_fmt_S24LE),
    .pcm_sample_rate_list = ICODEC_RATE_LIST,
    .i2s_frame_mode_list = BIT(i2s_LR_mode),
    .i2s_bclk_direction_list = BIT(i2s_bclk_codec_slave) | BIT(i2s_bclk_codec_master),
    .i2s_frame_direction_list = BIT(i2s_frame_codec_slave) | BIT(i2s_frame_codec_master),
    .pcm_enable = icodec_capture_pcm_enable,
    .pcm_disable = icodec_capture_pcm_disable,
    .pcm_set_volume = icodec_capture_pcm_set_volume,
    .pcm_get_volume = icodec_capture_pcm_get_volume,
    .pcm_start = icodec_capture_pcm_start,
    .pcm_stop = icodec_capture_pcm_stop,
    .pcm_private_ctrl = icodec_capture_pcm_private_ctrl,
};

void aic_select_inner_codec(int id, int value);

void icodec_init(void)
{
    pcm_register(&icodec_playback_device);
    pcm_register(&icodec_capture_device);
    aic_select_inner_codec(0, 1);

    /* codec初始化进入待机状态，播放时配置dac余下步骤(避免播放无声或滋滋声) */
    icodec.is_standby = 0;
    if (icodec.is_standby) {
        icodec.hpout_vol = 100;
        icodec_power_on();
        icodec_dac_enter_standby();
    }
}
