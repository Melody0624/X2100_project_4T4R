#include <driver/irq.h>
#include <driver/clk.h>
#include <soc/base.h>
#include <common.h>
#include <os.h>
#include <errno.h>
#include <soc/adc.h>
#include <driver/adc.h>

#include "hal/adc_hal.h"

#define DEV_NAME            "adc"

#define CLKDIV                 (0)     // 偶数分频, 最小值是0,表示24M不分频
#define CLKDIV_US              (2)     // 偶数分频, 最小是2
#define CLKDIV_MS              (2 - 1) // 分频值是x+1, x最小是1

#define STABLE_TIME 1
#define REPEAT_TIME 1

#define ADC_SAMPLE_TIMEOUT      100
#define ADC_MAX_CHANNEL         5

struct adc_dev {
    struct clk* clk;
    struct mutex mutex;
    thread_waiter_t   data_wait;
    critical_thread_cond_t  exit_wait;

    uint32_t        is_cb;
    uint32_t        ops_busy;
    uint32_t        exit_flag;
    uint32_t        is_repeat;

    int irq;
};

static struct adc_dev *adc_device;

static adc_irq_cb_t m_cb;

static void adc_irq_handler(int irq, void *data)
{
    assert(adc_device);

    adc_hal_clean_all_interrupt_flag();

    if (adc_device->is_cb) {
        if (!adc_device->is_repeat)
            adc_device->is_cb = 0;
        m_cb();
    }

    if (adc_device->ops_busy)
        thread_waiter_wakeup(&adc_device->data_wait);
}

int soc_adc_read_data(unsigned int channel)
{
    int val;

    if(channel > ADC_MAX_CHANNEL) {
            return -EINVAL;
    }

    os_enter_critical();
    if (adc_device == NULL) {
        os_exit_critical();
        return -ENODEV;
    }

    adc_device->ops_busy++;
    os_exit_critical();
    mutex_lock(&adc_device->mutex);

    if (adc_device->exit_flag) {
        val = -ENODEV;
        goto adc_sample_exit;
    }

    adc_hal_enable_channel(channel);

    val = thread_waiter_wait_timeout(&adc_device->data_wait, ADC_SAMPLE_TIMEOUT);
    if (adc_device->exit_flag) {
        val = -ENODEV;
        goto adc_sample_exit;
    }

    if (val) {
        val = -ETIMEDOUT;
        goto adc_sample_exit;
    }

    val = adc_hal_read_channel_data(channel);

adc_sample_exit:
    adc_hal_disable_channel(channel);
    mutex_unlock(&adc_device->mutex);
    os_enter_critical();
    adc_device->ops_busy--;
    critical_thread_cond_signal(&adc_device->exit_wait);
    os_exit_critical();
    return val;
}

int soc_adc_read_data_poll(unsigned int channel)
{
    if (channel > ADC_MAX_CHANNEL) {
        return -EINVAL;
    }

    adc_hal_enable_channel(channel);

    while (!adc_hal_get_interrupt_flag(channel));

    adc_hal_clean_interrupt_flag(channel);

    return adc_hal_read_channel_data(channel);
}

int soc_adc_read_raw_channel_data(unsigned int channel)
{
    return adc_hal_read_channel_data(channel);
}

void soc_adc_enable_repeat_sampling(int enable)
{
    adc_device->is_repeat = !!enable;
    if (enable)
        adc_hal_enable_repeat_sampling();
    else
        adc_hal_disable_repeat_sampling();
}

void soc_adc_start_channels_sampling(unsigned int channels)
{
    assert(m_cb);

    os_enter_critical();
    if (adc_device->ops_busy) {
        printf("adc: ops is busy!\n");
        goto out;
    }

    if (adc_device->is_cb)
        goto out;

    adc_device->is_cb = 1;

    adc_hal_enable_channels_interrupt(channels);

    adc_hal_enable_channels(channels);

out:
    os_exit_critical();
}

void soc_adc_set_irq_cb(adc_irq_cb_t cb_func)
{
    m_cb = cb_func;
}

void soc_adc_enable_poll_mode(void)
{
    disable_irq(IRQ_SADC);
}

void soc_adc_disable_poll_mode(void)
{
    enable_irq(IRQ_SADC);
}

void soc_adc_init(void)
{
    os_enter_critical();
    assert(adc_device == NULL);

    adc_device = malloc(sizeof(struct adc_dev));
    if (adc_device == NULL)
        panic("malloc adc device error\n");

    memset(adc_device, 0, sizeof(struct adc_dev));

    adc_device->clk = clk_get("gate_sadc");
    clk_enable(adc_device->clk);

    adc_hal_disable_controller();
    adc_hal_mask_all_interrupt();
    adc_hal_clean_all_interrupt_flag();

    adc_hal_set_clkdiv(CLKDIV, CLKDIV_US, CLKDIV_MS);
    adc_hal_set_wait_sampling_stable_time(STABLE_TIME);
    adc_hal_set_repeat_sampling_time(REPEAT_TIME);

    mutex_init(&adc_device->mutex);
    thread_waiter_init(&adc_device->data_wait);
    critical_thread_cond_init(&adc_device->exit_wait);

    adc_device->ops_busy++;

    mutex_lock(&adc_device->mutex);
    os_exit_critical();

    adc_hal_enable_controller();

    request_irq(IRQ_SADC, 0, adc_irq_handler, DEV_NAME, NULL);

    adc_hal_enable_all_interrupt();

    mutex_unlock(&adc_device->mutex);

    os_enter_critical();
    adc_device->ops_busy--;
    critical_thread_cond_signal(&adc_device->exit_wait);
    os_exit_critical();
}

void soc_adc_deinit(void)
{
    os_enter_critical();
    assert(adc_device);
    assert(!adc_device->exit_flag);
    adc_device->exit_flag = 1;

    while (adc_device->ops_busy) {
        thread_waiter_wakeup(&adc_device->data_wait);
        critical_thread_cond_wait(&adc_device->exit_wait);
    }

    disable_irq(IRQ_SADC);
    release_irq(IRQ_SADC);

    adc_hal_disable_controller();

    clk_disable(adc_device->clk);

    free(adc_device);
    adc_device = NULL;

    os_exit_critical();
}

