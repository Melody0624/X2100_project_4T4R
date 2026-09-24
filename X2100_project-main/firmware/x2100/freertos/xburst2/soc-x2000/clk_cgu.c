#include <soc/cpm.h>
#include <soc/base.h>
#include <soc/ddr.h>
#include <driver/cache.h>
#include "clk.h"
#include "spinlock.h"
#include "error-base.h"
#include "common.h"

static DEFINE_SPINLOCK(cpm_cgu_lock);

#define cgu_cs   30, 31
#define cgu_ce   29
#define cgu_busy 28
#define cgu_stop 27

struct cgu_clk {
    unsigned char src[4];
    unsigned char cdr_bits; // size of cdr
    unsigned char coe;      // 分频值div = (cdr + 1)*coe
    unsigned char reg;
    unsigned short save_div;
};

#define index(id) ((id) - CLK_ID_CGU_DDR)

static struct cgu_clk cgu_clks[] = {
    [index(CLK_ID_CGU_DDR)]       = {{CLK_ID_EXT,    CLK_ID_SCLK_A, CLK_ID_MPLL,  CLK_ID_EXT}, 4, 1, CPM_DDCDR,},
    [index(CLK_ID_CGU_MACPHY)]    = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_MACPHYCDR,},
    [index(CLK_ID_CGU_MACTXPHY)]  = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_MACTXCDR,},
    [index(CLK_ID_CGU_MACTXPHY1)] = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_MACTXCDR1,},
    [index(CLK_ID_CGU_MACPTP)]    = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_MACPTPCDC,},
    [index(CLK_ID_CGU_LPC)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_LPCDR,},
    [index(CLK_ID_CGU_MSC0)]      = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EXT1,  CLK_ID_EXT}, 8, 4, CPM_MSC0CDR,},
    [index(CLK_ID_CGU_MSC1)]      = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EXT1,  CLK_ID_EXT}, 8, 4, CPM_MSC1CDR,},
    [index(CLK_ID_CGU_MSC2)]      = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EXT1,  CLK_ID_EXT}, 8, 4, CPM_MSC2CDR,},
    [index(CLK_ID_CGU_SFC)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_SFCCDR,},
    [index(CLK_ID_CGU_SSI)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_SSICDR,},
    [index(CLK_ID_CGU_CIM)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_CIMCDR,},
    [index(CLK_ID_CGU_PWM)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 8, 1, CPM_PWMCDR,},
    [index(CLK_ID_CGU_ISP)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 4, 1, CPM_ISPCDR,},
    [index(CLK_ID_CGU_RSA)]       = {{CLK_ID_SCLK_A, CLK_ID_MPLL,   CLK_ID_EPLL,  CLK_ID_EXT}, 4, 1, CPM_RSACDR,},
};

static inline void cpm_set_xcdr(unsigned long xcdr, int id)
{
    cpm_outl(xcdr, cgu_clks[id].reg);
}

static inline unsigned long cpm_get_xcdr(int id)
{
    return cpm_inl(cgu_clks[id].reg);
}

static inline void cpm_set_parent(int id, int parent)
{
    unsigned long xcdr = cpm_get_xcdr(id);

    /* 设置时钟源 */
    set_bit_field(&xcdr, cgu_cs, parent);
    cpm_set_xcdr(xcdr, id);
}

static inline void cpm_cgu_disable(unsigned long *xcdr, int id)
{
    set_bit_field(xcdr, cgu_stop, cgu_stop, 1);
    set_bit_field(xcdr, cgu_ce, cgu_ce, 1);
    cpm_set_xcdr(*xcdr, id);

    cpm_clear_bit(cgu_ce, cgu_clks[id].reg);
}

static inline void cpm_cgu_enable(unsigned long *xcdr, int id)
{
    set_bit_field(xcdr, cgu_stop, cgu_stop, 0);
    set_bit_field(xcdr, cgu_ce, cgu_ce, 1);
    cpm_set_xcdr(*xcdr, id);

    while(cpm_test_bit(cgu_busy, cgu_clks[id].reg))
        debug("wait stable.[%d]\n",__LINE__);

    cpm_clear_bit(cgu_ce, cgu_clks[id].reg);
}

static inline int cgu_is_enabled(unsigned long xcdr)
{
    return !get_bit_field(&xcdr, cgu_stop, cgu_stop);
}

static inline int caculate_div(unsigned int rate, unsigned int parent_rate, int id)
{
    /* 通过parent_rate 和rate算出最合理的分频值 */
    int i;
    unsigned int max_div = 1 << cgu_clks[id].cdr_bits;

    parent_rate = parent_rate / cgu_clks[id].coe;
    for (i = 1; i <= max_div; i ++) {
        if (parent_rate/i <= rate)
            break;
    }

    if (i > max_div)
        i = max_div;

    return i * cgu_clks[id].coe;
}

static inline struct clk *to_parent_clk(unsigned long xcdr, int id)
{
    int n = get_bit_field(&xcdr, cgu_cs);
    int parent = cgu_clks[id].src[n];

    return get_clk_from_id(parent);;
}

static inline int cgu_get_div(unsigned long xcdr, int id)
{
    int div = get_bit_field(&xcdr, 0, cgu_clks[id].cdr_bits);
    div = (div + 1) * cgu_clks[id].coe;

    return div;
}

static inline void cgu_set_div(unsigned long *xcdr, int div, int id)
{
    /* 如果分频值没有变化，就不用设置 */
    if (cgu_get_div(*xcdr, id) == div)
        return ;

    int cdr = (div / cgu_clks[id].coe) - 1;
    set_bit_field(xcdr, 0, cgu_clks[id].cdr_bits, cdr);
    set_bit_field(xcdr, cgu_ce, cgu_ce, 1);
    cpm_set_xcdr(*xcdr, id);

    while(cpm_test_bit(cgu_busy, cgu_clks[id].reg))
        debug("wait stable.[%d]\n",__LINE__);

    cpm_clear_bit(cgu_ce, cgu_clks[id].reg);
}

unsigned long cgu_get_rate(struct clk *clk)
{
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);
    struct clk *parent = get_clk_from_id(clk->parent);

    int div = cgu_get_div(xcdr, id);

    if (parent->id == CLK_ID_EXT1)
        return parent->rate;

    return parent->rate / div;
}

int cgu_set_rate(struct clk *clk, unsigned int rate)
{
    unsigned long flags;
    int id = index(clk->id);
    struct clk *parent = get_clk_from_id(clk->parent);

    spin_lock_irqsave(&cpm_cgu_lock, flags);
    unsigned long xcdr = cpm_get_xcdr(id);

    /* 算出最合理的分频值 */
    int div = caculate_div(rate, parent->rate, id);

    /* 设置分频值 */
    int enable = cgu_is_enabled(xcdr);

    if (enable)
        cgu_set_div(&xcdr, div, id);
    else
        cgu_clks[id].save_div = div; /* 当前还没使能，就先把分频系数给保存下来 */

    clk->rate = parent->rate / div;

    spin_unlock_irqrestore(&cpm_cgu_lock, flags);

    return 0;
}

int cgu_enable(struct clk *clk, int on)
{
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);
    struct clk *parent = get_clk_from_id(clk->parent);

    int prev_on = cgu_is_enabled(xcdr);
    if (prev_on == on)
        return 0;

    /* 失能时钟输出 */
    if (!on) {
        cpm_cgu_disable(&xcdr, id);
        return 0;
    }

    /* 使能时钟输出， 并设置分频值 */
    int div = cgu_clks[id].save_div;
    cgu_set_div(&xcdr, div, id);
    cpm_cgu_enable(&xcdr, id);

    clk->rate = parent->rate / div;

    return 0;
}

struct clk * cgu_get_parent(struct clk *clk)
{
    int id  = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);
    return to_parent_clk(xcdr, id);
}

int cgu_set_parent(struct clk *clk, struct clk *parent)
{
    int i;
    unsigned long flags;
    int id = index(clk->id);

    for (i = 0; i < 4; i++) {
        if (cgu_clks[id].src[i] == parent->id)
            break;
    }
    if (i >= 4)
        return -EINVAL;

    spin_lock_irqsave(&cpm_cgu_lock, flags);

    /* 设置时钟源 */
    cpm_set_parent(id, i);
    clk->parent = parent->id;

    spin_unlock_irqrestore(&cpm_cgu_lock, flags);

    /* 更新时钟频率 */
    cgu_set_rate(clk, clk->rate);

    return 0;
}

void init_cgu_clk(struct clk *clk)
{
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);

    struct clk *parent = cgu_get_parent(clk);
    if (parent)
        clk->parent = parent->id;

    clk->rate = cgu_get_rate(clk);
    cgu_clks[id].save_div = cgu_get_div(xcdr, id);

    if (cgu_is_enabled(xcdr))
        clk->is_init_enabled = 1;
}
