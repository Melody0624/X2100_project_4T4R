#include <common.h>
#include <driver/clk.h>
#include <cpu/cpu.h>
#include <soc/base.h>
#include <soc/extal.h>
#include <asm/mipsregs.h>
#include <driver/irq.h>
#include <driver/systick.h>

#define TCU_OST_PRECALE                  1
#define OST_TIMER_PRECALE                1

#define EXTAL_CLOCK_MHZ             (JZ_EXTAL / USEC_PER_SEC)

union cycle_type {
    uint64_t cycle64;
    uint32_t cycle32[2];
};

void soc_ost_clk_enable(int on);
void soc_tcu_clk_enable(int on);

#define TCU_TESR    (0x14)    /*Timer Counter Enable Set Register */

/* TCU TER */
#define TER_OSTEN       15
#define TER_OSTCL       15

/*OST*/
#define TCU_OSTCNTL        (0x104)
#define TCU_OSTCNTH        (0x108)
#define TCU_OSTCSR         (0x110)

/* OST TCSR */
#define OSTCSR_CNT_MD      (1 << 22)
#define OSTCSR_SD          (1 << 15)

#define OSTCSR_EXT_EN      (1 << 2)

#define TCU_ADDR(reg)    (io_addr(TCU_IOBASE + reg))

static inline uint32_t tcu_read_reg(int reg)
{
    return *TCU_ADDR(reg);
}

static inline void tcu_write_reg(int reg, uint32_t value)
{
    *TCU_ADDR(reg) = value;
}

static inline void tcu_set_bit(int reg, int n)
{
    set_bits(*TCU_ADDR(reg), BIT(n));
}

static inline void tcu_clear_bit(int reg, int n)
{
    clear_bits(*TCU_ADDR(reg), BIT(n));
}

static inline int tcu_prescale(int prescale)
{
    if (prescale == 1) return 0;
    if (prescale == 4) return 1 << 3;
    if (prescale == 16) return 1 << 4;
    return 2;
}

static inline union cycle_type tcu_ost_get_cycle(void)
{
    union cycle_type cycle;

    do {
        cycle.cycle32[1] = tcu_read_reg(TCU_OSTCNTH);
        cycle.cycle32[0] = tcu_read_reg(TCU_OSTCNTL);
    } while(cycle.cycle32[1] != tcu_read_reg(TCU_OSTCNTH));

    return cycle;
}

static void tcu_ost_init(void)
{
    /* enable clk */
    soc_tcu_clk_enable(1);

    /* enable systick counter */
    tcu_set_bit(TCU_TESR, TER_OSTEN);

    tcu_write_reg(TCU_OSTCNTL, 0);
    tcu_write_reg(TCU_OSTCNTH, 0);

    tcu_write_reg(TCU_OSTCSR, (OSTCSR_CNT_MD | tcu_prescale(TCU_OST_PRECALE) | OSTCSR_EXT_EN));
}

#define OST_TIMER_MAX_VALUE         0xFFFFFFFFULL
#define OST_TIMER_MAX_DELTA_US      (OST_TIMER_MAX_VALUE * OST_TIMER_PRECALE / EXTAL_CLOCK_MHZ)

#define OSTCCR  0x00
#define OSTER   0x04
#define OSTCR   0x08
#define OSTFR    0x0C
#define OSTMR    0x10
#define OSTDFR   0x14
#define OSTCNT   0X18

static unsigned int ost_io_base = 0;

static void m_default_handler(void) {}

static systick_event_handler_t ost_callback_handler = m_default_handler;

#define OST_ADDR(reg)   (io_addr(ost_io_base + reg))

static inline void ost_write(unsigned int reg, int val)
{
    *OST_ADDR(reg) = val;
}

static inline unsigned int ost_read(unsigned int reg)
{
    return *OST_ADDR(reg);
}

static inline int ost_prescale(int prescale)
{
    if (prescale == 1) return 0;
    if (prescale == 4) return 1;
    if (prescale == 16) return 2;
    return 2;
}

static void ost_timer_start(void)
{
    ost_write(OSTMR, 0);
    ost_write(OSTER, 1);
}

static void ost_timer_stop(void)
{
    ost_write(OSTMR, 1);
    ost_write(OSTER, 0);
}

static inline void ost_timer_set(uint32_t count)
{
    ost_write(OSTCR,  1);        /* OST Timer1 clear count Register */
    ost_write(OSTFR,  0);       /* OST Timer1 clear Comparison match  */
    ost_write(OSTDFR, count);
}

static void ost_timer_init(void)
{
    ost_write(OSTCCR, ost_prescale(OST_TIMER_PRECALE));
    ost_write(OSTDFR, 0xffffffff);
}

static int is_inited = 0;

uint64_t soc_systick_get_time_us(void)
{
    uint64_t us;
    union cycle_type cycle;

    if (!is_inited)
        return 0;

    cycle = tcu_ost_get_cycle();

    us = cycle.cycle64 * TCU_OST_PRECALE / EXTAL_CLOCK_MHZ;

    return us;
}

uint64_t soc_systick_get_time_ns(void)
{
    union cycle_type now;
    uint64_t ns;

    now = tcu_ost_get_cycle();

    ns = now.cycle64 * TCU_OST_PRECALE * 1000 / EXTAL_CLOCK_MHZ;

    return ns;
}

void ndelay(unsigned int nsecs)
{
    union cycle_type start, now;

    start = tcu_ost_get_cycle();

    uint64_t cycles = (uint64_t)nsecs * EXTAL_CLOCK_MHZ / (1000 * TCU_OST_PRECALE);

    while (1) {
        now = tcu_ost_get_cycle();

        if (now.cycle64 - start.cycle64 > cycles)
            break;
    }
}

static void ost_timer_handler(int irq, void *data)
{
    ost_timer_stop();
    ost_callback_handler();
}

void soc_systick_set_next_time(uint64_t expires)
{
    uint64_t now = systick_get_time_us();
    uint64_t usec = expires - now;
    uint32_t delta_count = 0;

    if (now >= expires) {
        usec = 1;
    }

    if (usec < OST_TIMER_MAX_DELTA_US) {
        delta_count = usec * EXTAL_CLOCK_MHZ / OST_TIMER_PRECALE;
    } else {
        delta_count = OST_TIMER_MAX_VALUE;
    }

    ost_timer_set(delta_count);
    ost_timer_start();
}

void soc_systick_set_event_callback(systick_event_handler_t callback)
{
    ost_callback_handler = callback;
}

void soc_systick_init(void)
{
    ost_io_base = OST_IOBASE + 0x100000 + arch_get_cpu_id() * 0x100;

    soc_ost_clk_enable(1);

    tcu_ost_init();
    ost_timer_init();
    request_irq(IRQ_V_IP4, 0, ost_timer_handler, "ost timer", NULL);

    is_inited = 1;
}
