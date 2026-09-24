#include <stdio.h>
#include <driver/cache.h>
#include <driver/irq.h>
#include <cpu/cpu.h>
#include "asm/mipsregs.h"
#include "common.h"
#include "lds_symbol.h"
#include "asm_symbol.h"
#include "spinlock.h"

#ifdef CONFIG_TCSM_SECTION_IRQ
#include <cpu/tcsm_section.h>
#else
#include <tcsm_section_null.h>
#endif


#define MAX_CPU_NUMS 2

struct irq_flag {
    unsigned int enabled:1;
    unsigned int requested:1;
    unsigned int irq_force_disable:1;
} ;

struct irq_data {
    void *data;
    const char *name;
    irq_handler_t handler;;
};

#define M_CPU_IRQ_NUMS (IRQ_CPU_END*MAX_CPU_NUMS)
#define M_IRQ_NUMS (M_CPU_IRQ_NUMS + (IRQ_NUMS-IRQ_CPU_END))

struct irq_flag irqflag[M_IRQ_NUMS];
struct irq_data irqdata[M_IRQ_NUMS];

static DEFINE_SPINLOCK(lock);

static inline int irq_index(int irq)
{
    if (irq >= IRQ_CPU_END)
        return M_CPU_IRQ_NUMS + (irq - IRQ_CPU_END);

    int cpu_id = arch_get_cpu_id();
    assert(cpu_id < MAX_CPU_NUMS);

    return cpu_id * IRQ_CPU_END + irq;
}

/*
 * Reserved interrupt ISR.
 */
static void do_reservedIRQ(int irq, void *data)
{
    (void) data;
    int index = irq_index(irq);
    panic("** Interrupt %d (%p %s) - miss ISR **\n", irq, data, irqdata[index].name);
}

#ifdef CONFIG_ENABLE_NESTED_IRQ
extern int os_handler_mode;
unsigned long save_sp;

void save_and_disable_other_vect_int_irqs(unsigned int not_disable_irqs);
void restore_save_vect_int_irqs(void);

void check_disable_common_irqs(unsigned int pending)
{
    os_handler_mode++;
    if (os_handler_mode > 2)
        panic("too many irq nested: 0x%08x\n", pending);

    if (pending & (CAUSEF_IP3|CAUSEF_IP4))
        return;

    if (os_handler_mode != 1)
        panic("too many common irq nested: 0x%08x %d\n", pending, os_handler_mode);

    save_and_disable_other_vect_int_irqs((STATUSF_IP3|STATUSF_IP4) >> STATUSB_IP0);

    local_irq_enable();
}

void check_enable_common_irqs(unsigned int pending)
{
    local_irq_disable();

    os_handler_mode--;

    if (pending & (CAUSEF_IP3|CAUSEF_IP4))
        return;

    restore_save_vect_int_irqs();
}
#endif

__tcsm_section static void do_handle_irq(int irq)
{
    int index = tcsm_call(irq_index, irq);
    irqdata[index].handler(irq, irqdata[index].data);
}


/*
 * 被 handle_int 调用
 */
__tcsm_section void handle_int_c(void)
{
    unsigned int cause = read_c0_cause();
    unsigned int status = read_c0_status();
    unsigned int pending = cause & status & ST0_IM;

    /* IP[0]: SW0
     * IP[1]: SW1
     * IP[2]: External interrupt
     * IP[3]: OST for x1000
     * IP[4]: OST for x1021/x1830 x1600 x2000
     * IP[7]: cpu timer (x1000 应该是没有)
     */

#ifdef CONFIG_ENABLE_NESTED_IRQ
    tcsm_call(check_disable_common_irqs, pending);
#endif

    if (pending & CAUSEF_IP4)      /* OST IP4 */
        do_handle_irq(IRQ_V_IP4);
    else if (pending & CAUSEF_IP3) /* OST IP3 */
        do_handle_irq(IRQ_V_IP3);
    else if (pending & CAUSEF_IP2) /* External interrupt IP2 */
        do_handle_irq(IRQ_V_IP2);
    else if (pending & CAUSEF_IP0) /* SW0 IP0 */
        do_handle_irq(IRQ_V_IP0);
    else if (pending & CAUSEF_IP1) /* SW1 IP1 */
        do_handle_irq(IRQ_V_IP1);
    else if (pending & CAUSEF_IP7) /* cp0 timer IP7 */
        do_handle_irq(IRQ_V_IP7);
    else if (pending & CAUSEF_IP5)
        do_handle_irq(IRQ_V_IP5);
    else if (pending & CAUSEF_IP6)
        do_handle_irq(IRQ_V_IP6);

#ifdef CONFIG_ENABLE_NESTED_IRQ
    tcsm_call(check_enable_common_irqs, pending);
#endif


}

void irq_dispatcher_init(void)
{
    int index;

    for (index = 0; index < M_IRQ_NUMS; index++)
        irqdata[index].handler = do_reservedIRQ;
}

void arch_handle_irq(int irq)
{
    int index = irq_index(irq);
    irqdata[index].handler(irq, irqdata[index].data);
}

void arch_request_irq_disabled(int irq, unsigned int irq_flags, irq_handler_t handler, const char *name, void *data)
{
    unsigned long flags;

    assert(handler);
    assert_range(irq, 0, IRQ_NUMS - 1);

    spin_lock_irqsave(&lock, flags);

    int index = irq_index(irq);
    assert(irqdata[index].handler == do_reservedIRQ);

    if (!irqflag[index].requested) {
        irqdata[index].data = data;
        irqdata[index].handler = handler;
        irqdata[index].name = name;

        if (irq <= IRQ_V_IP7)
            disable_vect_int_irq(irq);
        else if (irq < IRQ_INTC_END)
            disable_intc_irq(irq);
        else if (irq < IRQ_GPIO_END)
            soc_gpio_startup_irq(irq, irq_flags);
        else
            soc_startup_extra_irq(irq, irq_flags);

        irqflag[index].requested = 1;
    }

    spin_unlock_irqrestore(&lock, flags);
}

void arch_request_irq(int irq, unsigned int irq_flags, irq_handler_t handler, const char *name, void *data)
{
    request_irq_disabled(irq, irq_flags, handler, name, data);
    enable_irq(irq);
}

void arch_release_irq(int irq)
{
    unsigned long flags;

    assert_range(irq, 0, IRQ_NUMS - 1);

    spin_lock_irqsave(&lock, flags);

    int index = irq_index(irq);

    assert(irqdata[index].handler);
    assert(!irqflag[index].enabled);

    if (irqflag[index].requested) {
        irqdata[index].handler = do_reservedIRQ;

        if (irq <= IRQ_V_IP7)
            disable_vect_int_irq(irq);
        else if (irq < IRQ_INTC_END)
            disable_intc_irq(irq);
        else if (irq < IRQ_GPIO_END)
            soc_gpio_shutdown_irq(irq);
        else
            soc_shutdown_extra_irq(irq);

        irqflag[index].requested = 0;
    }

    spin_unlock_irqrestore(&lock, flags);
}

void arch_release_all_irq(void)
{
    int irq;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    for (irq = 0; irq < M_IRQ_NUMS; irq++) {
        int index = irq_index(irq);
        if (!irqflag[index].requested)
            continue;

        irqdata[index].handler = do_reservedIRQ;

        if (irq <= IRQ_V_IP7)
            disable_vect_int_irq(irq);
        else if (irq < IRQ_INTC_END)
            disable_intc_irq(irq);
        else if (irq < IRQ_GPIO_END) {
            soc_gpio_disable_irq(irq);
            soc_gpio_shutdown_irq(irq);
        } else {
            soc_disable_extra_irq(irq);
            soc_shutdown_extra_irq(irq);
        }

        irqflag[index].enabled = 0;
        irqflag[index].requested = 0;
    }

    spin_unlock_irqrestore(&lock, flags);
}

void arch_enable_irq_nolock(int irq)
{
    assert_range(irq, 0, IRQ_NUMS - 1);

    int index = irq_index(irq);
    assert(irqdata[index].handler);

    if (!irqflag[index].enabled) {
        if (irq <= IRQ_V_IP7)
            enable_vect_int_irq(irq);
        else if (irq < IRQ_INTC_END)
            enable_intc_irq(irq);
        else if (irq < IRQ_GPIO_END)
            soc_gpio_enable_irq(irq);
        else
            soc_enable_extra_irq(irq);

        irqflag[index].enabled = 1;
    }
}

void arch_disable_irq_nolock(int irq)
{
    assert_range(irq, 0, IRQ_NUMS - 1);

    int index = irq_index(irq);
    assert(irqdata[index].handler);

    if (irqflag[index].enabled) {
        if (irq <= IRQ_V_IP7)
            disable_vect_int_irq(irq);
        else if (irq < IRQ_INTC_END)
            disable_intc_irq(irq);
        else if (irq < IRQ_GPIO_END)
            soc_gpio_disable_irq(irq);
        else
            soc_disable_extra_irq(irq);

        irqflag[index].enabled = 0;
    }
}

void arch_enable_irq(int irq)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    arch_enable_irq_nolock(irq);

    spin_unlock_irqrestore(&lock, flags);
}

void arch_disable_irq(int irq)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    arch_disable_irq_nolock(irq);

    spin_unlock_irqrestore(&lock, flags);
}

__weak void soc_enable_extra_irq(int irq)
{
    (void) irq;
    assert(0);
}

__weak void soc_disable_extra_irq(int irq)
{
    (void) irq;
    assert(0);
}

__weak void soc_startup_extra_irq(int irq, unsigned int irq_flags)
{
    (void) irq;
    (void) irq_flags;
    assert(0);
}

__weak void soc_shutdown_extra_irq(int irq)
{
    (void) irq;
    assert(0);
}
