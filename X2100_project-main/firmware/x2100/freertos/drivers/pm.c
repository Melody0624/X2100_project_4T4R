#include <driver/pm.h>
#include<common.h>

/*
 * soc 需要实现
 */
void soc_pm_enter_sleep(void);
void soc_pm_power_off(void);
void soc_pm_init(void);

int soc_pm_get_wakeup_irq(void);

__weak void pm_gpio_low_power_set_init(void)
{

}

__weak void pm_gpio_low_power_runtime_set_before_sleep(void)
{

}

__weak void pm_gpio_low_power_runtime_set_after_sleep(void)
{

}
__weak int soc_pm_get_wakeup_irq(void)
{
    printf("soc not supprt get wakeup src\n");
    return 0;
};

void pm_enter_sleep(void)
{
    soc_pm_enter_sleep();
}

void pm_power_off(void)
{
    soc_pm_power_off();
}

void pm_init(void)
{
    soc_pm_init();
}

int pm_get_wakeup_irq(void)
{
    return soc_pm_get_wakeup_irq();
}