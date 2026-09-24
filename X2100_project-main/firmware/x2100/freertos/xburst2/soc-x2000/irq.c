#include <driver/cache.h>
#include <cpu/cpu.h>
#include "asm/mipsregs.h"
#include "common.h"
#include "lds_symbol.h"
#include "asm_symbol.h"
#include <driver/irq.h>
#include <soc/base.h>
#include <driver/gpio.h>
#include <cpu/cp0_timer.h>
#include "__ffs.h"

#define INTC_ICSR0  0x00
#define INTC_ICMR0  0x04
#define INTC_ICMSR0 0x08
#define INTC_ICMCR0 0x0C
#define INTC_ICPR0  0x10
#define INTC_ICSR1  0x20
#define INTC_ICMR1  0x24
#define INTC_ICMSR1 0x28
#define INTC_ICMCR1 0x2C
#define INTC_ICPR1  0x30
#define INTC_DSR0   0x34
#define INTC_DMR0   0x38
#define INTC_DPR0   0x3C
#define INTC_DSR1   0x40
#define INTC_DMR1   0x44
#define INTC_DPR1   0x48

static unsigned int intc_io_base = 0;

#define INTC_ADDR(reg) io_addr(intc_io_base + reg)

static inline void intc_write(unsigned int reg, int val)
{
    *INTC_ADDR(reg) = val;
}

static inline unsigned int intc_read(unsigned int reg)
{
    return *INTC_ADDR(reg);
}

static inline void hal_intc_mask_irq(int i)
{
    if (i < 32)
        intc_write(INTC_ICMSR0, 1 << i);
    else
        intc_write(INTC_ICMSR1, 1 << (i - 32));
}

static inline void hal_intc_unmask_irq(int i)
{
    if (i < 32)
        intc_write(INTC_ICMCR0, 1 << i);
    else
        intc_write(INTC_ICMCR1, 1 << (i - 32));
}

void enable_intc_irq(int irq)
{
    hal_intc_unmask_irq(irq - IRQ_INTC_0);
}

void disable_intc_irq(int irq)
{
    hal_intc_mask_irq(irq - IRQ_INTC_0);
}

int intc_irq_get_src(int irq)
{
    int i = irq - IRQ_INTC_0;
    if (i < 32)
        return intc_read(INTC_ICSR0) & (1 << i);
    else
        return intc_read(INTC_ICSR1) & (1 << (i - 32));
}

void intc_irq_handler(int irq, void *data)
{
    unsigned long ipr;

    ipr = intc_read(INTC_ICPR0);
    if (ipr) {
        handle_irq(__ffs(ipr) + IRQ_INTC_0);
        return;
    }

    ipr = intc_read(INTC_ICPR1);
    if (ipr)
        handle_irq(__ffs(ipr) + IRQ_INTC_1);
}

void soc_intc_clk_enable(void);

void soc_irq_init(void)
{
    soc_intc_clk_enable();

    intc_io_base = INTC_IOBASE + arch_get_cpu_id() * 0x100;

    request_irq(IRQ_V_IP2, 0, intc_irq_handler, "intc", NULL);

#ifdef CONFIG_GPIO
    soc_gpio_irq_init();
#endif

#ifdef CONFIG_XBURST2_CP0_TIMER
    cp0_timer_init();
#endif
}