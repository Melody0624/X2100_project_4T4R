#include <driver/pwm.h>

int soc_pwm_request(int gpio, const char *name);

int soc_pwm_config(int id, struct pwm_config_data *config);

void soc_pwm_release(int id);

void soc_pwm_set_level(int id, unsigned long level);

unsigned long soc_pwm_get_freq(int id);

void soc_pwm_init(void);

void __attribute__((weak)) soc_pwm_init(void) { }

void pwm_init(void)
{
    soc_pwm_init();
}

int pwm_request(int gpio, const char *name)
{
    return soc_pwm_request(gpio, name);
}

int pwm_config(int ch, struct pwm_config_data *config)
{
    return soc_pwm_config(ch, config);
}

void pwm_release(int ch)
{
    soc_pwm_release(ch);
}

void pwm_set_level(int ch, unsigned long level)
{
    soc_pwm_set_level(ch, level);
}

unsigned long pwm_get_freq(int ch)
{
    return soc_pwm_get_freq(ch);
}

/* ---- dma mode ---- */
int __attribute__((weak)) soc_pwm_dma_init(int id, struct pwm_dma_config *dma_config) { return -1; }
int __attribute__((weak)) soc_pwm_dma_update(int id, struct pwm_dma_data *dma_data) { return -1; }
int __attribute__((weak)) soc_pwm_dma_disable_loop(int id) { return -1; }
unsigned long __attribute__((weak)) soc_pwm_dma_read_src_addr(int id) { return 0; }
int __attribute__((weak)) soc_pwm_dma_set_div(int id, int div) {return -1; }
int __attribute__((weak)) soc_pwm_dma_set_start(int id, struct pwm_data *data) {return -1; }

int pwm_dma_init(int id, struct pwm_dma_config *dma_config)
{
    return soc_pwm_dma_init(id, dma_config);
}

int pwm_dma_update(int id, struct pwm_dma_data *dma_data)
{
    return soc_pwm_dma_update(id, dma_data);
}

int pwm_dma_disable_loop(int id)
{
    return soc_pwm_dma_disable_loop(id);
}

unsigned long pwm_dma_read_src_addr(int id)
{
    return soc_pwm_dma_read_src_addr(id);
}

int pwm_dma_set_start(int id, struct pwm_data *data)
{
   return soc_pwm_dma_set_start(id, data);
}

int pwm_dma_set_div(int id, int div)
{
    return soc_pwm_dma_set_div(id, div);
}


/* ---- sync mode ---- */
void soc_pwm_set_not_really_disable(int id, int enable);
void soc_pwm_set_not_really_enable(int id, int enable);
void soc_pwm_enable_channels(unsigned int channels);
void soc_pwm_disable_channels(unsigned int channels);


void pwm_set_not_really_disable(int id, int enable)
{
    soc_pwm_set_not_really_disable(id, enable);
}

void pwm_set_not_really_enable(int id, int enable)
{
    soc_pwm_set_not_really_enable(id, enable);
}

void pwm_enable_channels(unsigned int channels)
{
    soc_pwm_enable_channels(channels);
}

void pwm_disable_channels(unsigned int channels)
{
    soc_pwm_disable_channels(channels);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(pwm_init);
EXPORT_SYMBOL(pwm_request);
EXPORT_SYMBOL(pwm_config);
EXPORT_SYMBOL(pwm_release);
EXPORT_SYMBOL(pwm_set_level);
EXPORT_SYMBOL(pwm_get_freq);

EXPORT_SYMBOL(pwm_dma_init);
EXPORT_SYMBOL(pwm_dma_update);
EXPORT_SYMBOL(pwm_dma_disable_loop);
EXPORT_SYMBOL(pwm_dma_read_src_addr);
EXPORT_SYMBOL(pwm_dma_set_start);
EXPORT_SYMBOL(pwm_dma_set_div);

EXPORT_SYMBOL(pwm_set_not_really_disable);
EXPORT_SYMBOL(pwm_set_not_really_enable);
EXPORT_SYMBOL(pwm_enable_channels);
EXPORT_SYMBOL(pwm_disable_channels);
