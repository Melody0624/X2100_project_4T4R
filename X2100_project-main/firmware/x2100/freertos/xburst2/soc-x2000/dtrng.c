#include <driver/irq.h>
#include <driver/clk.h>
#include <soc/base.h>
#include <common.h>
#include <os.h>
#include <errno.h>
#include <spinlock.h>
#include <soc/dtrng.h>

#define DTRNGCFG 0x0000
#define DTRNGRANDOMNUM  0x0004
#define DTRNGSTAT  0x0008

#define DTRNGCFG_Reserved   26, 31
#define DTRNGCFG_LINE_EN    16, 25
#define DTRNGCFG_Reserved1  13, 15
#define DTRNGCFG_RDY_CLR    12, 12
#define DTRNGCFG_INT_MASK   11, 11
#define DTRNGCFG_DIV_NUM    1,  10
#define DTRNGCFG_GEN_EN     0,  0


#define DTRNGSTAT_Reserved0         1, 31
#define DTRNGSTAT_RANDOM_RDY        0, 0

#define DTRNG_DIV   0
#define DTRNG_TIMEOUT_US  10

#define DTRNG_REG_BASE  KSEG1ADDR(DTRNG_IOBASE)

#define DTRNG_ADDR(reg) ((volatile unsigned long *)(DTRNG_REG_BASE + reg))

static inline void dtrng_write_reg(unsigned int reg, unsigned int value)
{
    *DTRNG_ADDR(reg) = value;
}

static inline unsigned int dtrng_read_reg(unsigned int reg)
{
    return *DTRNG_ADDR(reg);
}

static inline void dtrng_set_bits(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(DTRNG_ADDR(reg), start, end, val);
}

static inline unsigned int dtrng_get_bits(unsigned int reg, int start, int end)
{
    return get_bit_field_v(DTRNG_ADDR(reg), start, end);
}

/**************************/
static DEFINE_SPINLOCK(lock);
static struct clk* dtrng_clk;

static void dtrng_enable(void)
{
    dtrng_set_bits(DTRNGCFG, DTRNGCFG_GEN_EN, 1);
}

static void dtrng_disable(void)
{
    dtrng_set_bits(DTRNGCFG, DTRNGCFG_GEN_EN, 0);
}

static void dtrng_clean_flag(void)
{
    dtrng_set_bits(DTRNGCFG, DTRNGCFG_RDY_CLR, 1);
    dtrng_set_bits(DTRNGCFG, DTRNGCFG_RDY_CLR, 0);
}

void soc_dtrng_init(void)
{
    dtrng_clk = clk_get("gate_dtrng");
    assert(dtrng_clk);
    clk_enable(dtrng_clk);

    dtrng_set_bits(DTRNGCFG, DTRNGCFG_LINE_EN, 0x3FF);
    dtrng_set_bits(DTRNGCFG, DTRNGCFG_DIV_NUM, DTRNG_DIV);
    dtrng_set_bits(DTRNGCFG, DTRNGCFG_INT_MASK, 1);
}

/*
使能位 gen_en 是连续触发的
当 random_rdy 位被置为 1 时，使能位 gen_en 不会被自动置为 0
*/
unsigned int soc_dtrng_read_random_data(void)
{
    uint64_t timeout;
    unsigned int value;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    dtrng_enable();
    dtrng_clean_flag();

    timeout = systick_get_time_us();
    while(!dtrng_get_bits(DTRNGSTAT, DTRNGSTAT_RANDOM_RDY)) {
        if ((systick_get_time_us() - timeout) > DTRNG_TIMEOUT_US)
            panic("error:cpu mode random data timeout!\n");
    }

    value = dtrng_read_reg(DTRNGRANDOMNUM);
    dtrng_disable();

    spin_unlock_irqrestore(&lock, flags);

    return value;
}

void soc_dtrng_deinit(void)
{
    clk_disable(dtrng_clk);
    clk_put(dtrng_clk);
}
