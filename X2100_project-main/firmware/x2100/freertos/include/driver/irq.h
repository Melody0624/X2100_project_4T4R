#ifndef _IRQ_H_
#define _IRQ_H_

#include <soc/irq.h>

#define IRQ_TYPE_NONE            0
#define IRQ_TYPE_EDGE_RISING     1
#define IRQ_TYPE_EDGE_FALLING    2
#define IRQ_TYPE_EDGE_BOTH       (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING)
#define IRQ_TYPE_LEVEL_HIGH      4
#define IRQ_TYPE_LEVEL_LOW       8

#define to_irqtype(flags) ((flags) & 0x0f)

typedef void (*irq_handler_t)(int irq, void *data);

void irq_init(void);

void request_irq(
    int irq,
    unsigned int irq_flags,
    irq_handler_t handler,
    const char *name,
    void *data
    );

void request_irq_disabled(
    int irq,
    unsigned int irq_flags,
    irq_handler_t handler,
    const char *name,
    void *data
    );

void release_irq(int irq);
void release_all_irq(void);

void enable_irq(int irq);
void disable_irq(int irq);

int gpio_to_irq(int gpio);
int irq_to_gpio(int irq);

/**
 * 低级api,用于irq 分发
 */
void handle_irq(int irq);

#endif /* _IRQ_H_ */