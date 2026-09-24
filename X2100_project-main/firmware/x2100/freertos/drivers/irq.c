#include <driver/irq.h>

/*
 * arch 需要实现
 */
extern void arch_request_irq(
    int irq,
    unsigned int irq_flags,
    irq_handler_t handler,
    const char *name,
    void *data
    );

extern void arch_request_irq_disabled(
    int irq,
    unsigned int irq_flags,
    irq_handler_t handler,
    const char *name,
    void *data
    );

extern void arch_irq_init(void);
extern void arch_release_irq(int irq);
extern void arch_release_all_irq(void);
extern void arch_enable_irq(int irq);
extern void arch_disable_irq(int irq);
extern void arch_handle_irq(int irq);

void irq_init(void)
{
    arch_irq_init();
}

void request_irq_disabled(int irq, unsigned int irq_flags, irq_handler_t handler, const char *name, void *data)
{
    arch_request_irq_disabled(irq, irq_flags, handler, name, data);
}

void request_irq(int irq, unsigned int irq_flags, irq_handler_t handler, const char *name, void *data)
{
    arch_request_irq(irq, irq_flags, handler, name, data);
}

void release_irq(int irq)
{
    arch_release_irq(irq);
}

void release_all_irq(void)
{
    arch_release_all_irq();
}

void enable_irq(int irq)
{
    arch_enable_irq(irq);
}

void disable_irq(int irq)
{
    arch_disable_irq(irq);
}

void handle_irq(int irq)
{
    arch_handle_irq(irq);
}

int gpio_to_irq(int gpio)
{
    return soc_gpio_to_irq(gpio);
}

int irq_to_gpio(int irq)
{
    return soc_irq_to_gpio(irq);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(request_irq_disabled);
EXPORT_SYMBOL(request_irq);
EXPORT_SYMBOL(release_irq);
EXPORT_SYMBOL(release_all_irq);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(handle_irq);
EXPORT_SYMBOL(gpio_to_irq);
EXPORT_SYMBOL(irq_to_gpio);