#include <driver/cache.h>
#include <driver/irq.h>
#include "irqflags.h"
#include "asm/mipsregs.h"
#include "common.h"
#include "lds_symbol.h"
#include "asm_symbol.h"
#include "stdint.h"

static unsigned int clk_freq_mHz = 456;

static uint64_t hi;

static int may_overflow;

#define HALF_MAX (0X80000000)
#define MAX      (0Xffffffff)

static inline void update_hi(void)
{
    may_overflow = 0;
    hi += 0x100000000ull / clk_freq_mHz;
}

static void cp0_timer_handler(int irq, void *data)
{
    if (read_c0_count() < HALF_MAX) {
        write_c0_compare(HALF_MAX);
        update_hi();
    } else {
        write_c0_compare(MAX);
        may_overflow = 1;
    }

    printf("timer: %x %x\n", read_c0_count(), read_c0_compare());
}

uint64_t get_time_us(void)
{
    unsigned long flags;
    uint64_t us;

    local_irq_save(flags);

    unsigned int count = read_c0_count();
    if (may_overflow && count < HALF_MAX)
        update_hi();

    us = hi + (count / clk_freq_mHz);

    local_irq_restore(flags);

    //printf("--> count: %d\n", read_c0_count() - count);

    return us;
}

#include <soc/ccu.h>

void cp0_timer_init(void)
{
    volatile unsigned long *cfcr = (void *)io_addr(CCU_IO_BASE+CCU_CFCR);

    set_bit_field_v(cfcr, CFCR_CP0_TIMER_ENABLE);

    write_c0_count(0);
    write_c0_compare(HALF_MAX);

    /* 打开 timer
     */
    clear_c0_cause(CAUSEF_DC);

    /* 打开 timer 中断
     */
    request_irq(IRQ_V_IP7, 0, cp0_timer_handler, "cp0 timer", NULL);
}

