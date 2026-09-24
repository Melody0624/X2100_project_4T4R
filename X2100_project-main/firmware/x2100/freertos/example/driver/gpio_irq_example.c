#include <common.h>
#include <driver/irq.h>
#include <driver/gpio.h>

static void m_gpio_irq_handler(int irq, void *data)
{
    printf("irq: %d %d\n", irq, gpio_get_value(irq_to_gpio(irq)));
}

void test_gpio_irq(void)
{
    request_irq(gpio_to_irq(GPIO_PA(19)), IRQ_TYPE_EDGE_BOTH, m_gpio_irq_handler, "gpio-pa19", NULL);

    // request_irq_disabled(gpio_to_irq(GPIO_PA(19)), IRQ_TYPE_LEVEL_HIGH, m_gpio_irq_handler, "gpio-pa19", NULL);
    // enable_irq(gpio_to_irq(GPIO_PA(19)));
}
