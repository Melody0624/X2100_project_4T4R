#include <stdio.h>
#include <common.h>
#include <driver/irq.h>
#include <driver/gpio.h>

static void m_gpio_irq_handler(int irq, void *data)
{
    printf("irq: %d %d\n", irq, gpio_get_value(irq_to_gpio(irq)));
}

void pm_gpio_set_init(void)
{
    /* PD */
    gpio_set_func(GPIO_PD(0),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_set_func(GPIO_PD(1),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_set_func(GPIO_PD(2),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_set_func(GPIO_PD(3),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_set_func(GPIO_PD(4),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_set_func(GPIO_PD(5),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));

    /* PE */
    gpio_set_func(GPIO_PE(5),  (GPIO_OUTPUT0));
    gpio_set_func(GPIO_PE(6),  (GPIO_OUTPUT0));

    request_irq(gpio_to_irq(GPIO_PD(14)), IRQ_TYPE_EDGE_BOTH, m_gpio_irq_handler, "gpio-pd14", NULL);
    request_irq(gpio_to_irq(GPIO_PD(15)), IRQ_TYPE_EDGE_BOTH, m_gpio_irq_handler, "gpio-pd15", NULL);
}