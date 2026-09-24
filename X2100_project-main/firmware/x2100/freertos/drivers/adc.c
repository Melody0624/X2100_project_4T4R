#include <soc/adc.h>
#include <driver/adc.h>

int soc_adc_read_data_poll(unsigned int channel);
void soc_adc_enable_poll_mode(void);
void soc_adc_disable_poll_mode(void);

int soc_adc_read_raw_channel_data(unsigned int channel);
void soc_adc_start_channels_sampling(unsigned int channels);
void soc_adc_set_irq_cb(adc_irq_cb_t cb_func);
void soc_adc_enable_repeat_sampling(int enable);
unsigned long soc_adc_clk_get_rate(void);

void adc_init(void)
{
    soc_adc_init();
}

int adc_read_data(unsigned int channel)
{
    return soc_adc_read_data(channel);
}

void adc_deinit(void)
{
    soc_adc_deinit();
}

unsigned long adc_clk_get_rate(void)
{
    return soc_adc_clk_get_rate();
}

void adc_enable_poll_mode(void)
{
    soc_adc_enable_poll_mode();
}

void adc_disable_poll_mode(void)
{
    soc_adc_disable_poll_mode();
}

int adc_read_data_poll(unsigned int channel)
{
    return soc_adc_read_data_poll(channel);
}

int adc_read_raw_channel_data(unsigned int channel)
{
    return soc_adc_read_raw_channel_data(channel);
}

void adc_start_channels_sampling(unsigned int channels)
{
    soc_adc_start_channels_sampling(channels);
}

void adc_set_irq_cb(adc_irq_cb_t cb_func)
{
    soc_adc_set_irq_cb(cb_func);
}

void adc_enable_repeat_sampling(int enable)
{
    soc_adc_enable_repeat_sampling(enable);
}
