#ifndef _CCU_H_
#define _CCU_H_

#include <io.h>
#include <os.h>

#define CCU_IO_BASE 0x12200000

#define CCU_CCCR 0x0000
#define CCU_CSSR 0x0020
#define CCU_CSRR 0x0040
#define CCU_MSCR 0x0060
#define CCU_MSIR 0x0064
#define CCU_CCR  0x0070
#define CCU_PIPR 0x0100
#define CCU_PIMR 0x0120
#define CCU_MIPR 0x0140
#define CCU_MIMR 0x0160
#define CCU_OIPR 0x0180
#define CCU_OIMR 0x01a0
#define CCU_DIPR 0x01c0
#define CCU_GDIMR 0x01e0
#define CCU_LDIMR(N) (0x0300+(N)*32)
#define CCU_RER  0x0f00
#define CCU_CSLR 0x0fa0
#define CCU_CSAR 0x0fa4
#define CCU_GIMR 0x0fc0
#define CCU_CFCR 0x0fe0
#define CCU_MBR(N) (0x1000+(N)*4)
#define CCU_BCER 0x1f00

#define CFCR_CP0_TIMER_ENABLE 0, 0, 1
#define CFCR_CP0_TIMER_DISABLE 0, 0, 0

static inline void ccu_write_reg(int reg, unsigned long value)
{
    writel(value, CCU_IO_BASE + reg);
}

static inline unsigned long ccu_read_reg(int reg)
{
    return readl(CCU_IO_BASE + reg);
}

static inline void ccu_set_bit(int reg, int bit, int v)
{
    unsigned long value = ccu_read_reg(reg);

    if (v)
        set_bits(value, BIT(bit));
    else
        clear_bits(value, BIT(bit));

    ccu_write_reg(reg, value);
}

static inline int ccu_get_bit(int reg, int bit)
{
    unsigned long value = ccu_read_reg(reg);

    return !!(value & BIT(bit));
}

static inline void ccu_spin_lock(unsigned int id)
{
    unsigned long cslr;

    while (1) {
        ccu_write_reg(CCU_CSAR, id);
        cslr = ccu_read_reg(CCU_CSLR);
        if ((cslr & BIT(31)) && ((cslr & 0x7fffffff) == id))
            break;
    }
}

static inline void ccu_spin_unlock(unsigned int id)
{
    unsigned long cslr;

    cslr = ccu_read_reg(CCU_CSLR);
    if ((cslr & BIT(31)) && ((cslr & 0x7fffffff) == id))
        ccu_write_reg(CCU_CSLR, 0);

}

static inline void ccu_spin_lock_critical(unsigned int id)
{
    unsigned long cslr;
    os_enter_critical();

    while (1) {
        ccu_write_reg(CCU_CSAR, id);
        cslr = ccu_read_reg(CCU_CSLR);
        if ((cslr & BIT(31)) && ((cslr & 0x7fffffff) == id))
            break;
    }
}

static inline void ccu_spin_unlock_critical(unsigned int id)
{
    unsigned long cslr;

    cslr = ccu_read_reg(CCU_CSLR);
    if ((cslr & BIT(31)) && ((cslr & 0x7fffffff) == id))
        ccu_write_reg(CCU_CSLR, 0);

    os_exit_critical();
}

#endif
