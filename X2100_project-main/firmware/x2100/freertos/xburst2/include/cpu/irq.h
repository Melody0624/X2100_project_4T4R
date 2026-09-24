#ifndef _CPU_IRQ_H_
#define _CPU_IRQ_H_

enum cpu_irq_type {
    IRQ_V_IP0,
    IRQ_V_IP1,
    IRQ_V_IP2,
    IRQ_V_IP3,
    IRQ_V_IP4,
    IRQ_V_IP5,
    IRQ_V_IP6,
    IRQ_V_IP7,

    IRQ_CPU_END,
};

void irq_dispatcher_init(void);

void enable_vect_int_irq(int irq);
void disable_vect_int_irq(int irg);

void set_exception_handler(unsigned int i, void (*handler)(void));

/*
 * 给soc中irq处理调用
 */
void arch_enable_irq_nolock(int irq);
void arch_disable_irq_nolock(int irq);

/* 
 * soc 实现的部分
 */
void soc_irq_init(void);
void soc_gpio_irq_init(void);

void soc_gpio_startup_irq(int irq, unsigned int flags);
void soc_gpio_shutdown_irq(int irq);

void soc_gpio_enable_irq(int irq);
void soc_gpio_disable_irq(int irq);

void enable_intc_irq(int irq);
void disable_intc_irq(int irq);

/*
 * 可以不用实现
 */
void soc_startup_extra_irq(int irq, unsigned int flags);
void soc_shutdown_extra_irq(int irq);

void soc_enable_extra_irq(int irq);
void soc_disable_extra_irq(int irq);

#endif /* _CPU_IRQ_H_ */
