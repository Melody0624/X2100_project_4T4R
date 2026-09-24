#include <soc/cpm.h>
#include "clk.h"
#include "spinlock.h"
#include "error-base.h"
#include "common.h"

static DEFINE_SPINLOCK(i2s_lock);

#define i2s_cs      30,30
#define i2s_ce      29,29
#define i2s_div_m   20,28
#define i2s_div_n   0,19

struct i2s_clk {
    unsigned char src[4];
    unsigned short reg_cdr;
    unsigned short is_init;
    int save_div_m;
    int save_div_n;
};

#define index(id) ((id) - CLK_ID_CGU_I2S0)

static struct i2s_clk i2s_clks[] = {
    [index(CLK_ID_CGU_I2S0)]    = {{CLK_ID_SCLK_A, CLK_ID_EPLL, CLK_ID_EXT, CLK_ID_EXT}, CPM_I2S0CDR},
    [index(CLK_ID_CGU_I2S1)]    = {{CLK_ID_SCLK_A, CLK_ID_EPLL, CLK_ID_EXT, CLK_ID_EXT}, CPM_I2S1CDR},
    [index(CLK_ID_CGU_I2S2)]    = {{CLK_ID_SCLK_A, CLK_ID_EPLL, CLK_ID_EXT, CLK_ID_EXT}, CPM_I2S2CDR},
    [index(CLK_ID_CGU_I2S3)]    = {{CLK_ID_SCLK_A, CLK_ID_EPLL, CLK_ID_EXT, CLK_ID_EXT}, CPM_I2S3CDR}
};

static inline void cpm_set_xcdr(unsigned long xcdr, int id)
{
    cpm_outl(xcdr, i2s_clks[id].reg_cdr);
}

static inline unsigned long cpm_get_xcdr(int id)
{
    return cpm_inl(i2s_clks[id].reg_cdr);
}

static inline void i2s_get_div(unsigned long xcdr, int *m, int *n)
{
    *m = get_bit_field(&xcdr, i2s_div_m);
    *n = get_bit_field(&xcdr, i2s_div_n);
}

static inline int i2s_is_enabled(unsigned long xcdr)
{
    return get_bit_field(&xcdr, i2s_ce);
}

static inline void caculate_div(unsigned int rate, unsigned long long parent_rate, int *div_m, int *div_n)
{
    int m, n;
    unsigned long long m_mul = 0;
    unsigned long long max_n = 0xfffffull;
    unsigned long long max_m = max_n * rate / parent_rate; // (div_m/div_n) = (rate/parent_rate)

    if (max_m > 0x1ff)
        max_m = 0x1ff;//511

    for (m = 1; m <= max_m; m++)
    {
        m_mul = m * parent_rate;
        if (!(m_mul % rate))
            break;
    }

    n = m * parent_rate / rate;
    if (m >max_m)
        m = max_m;

    *div_m = m;
    *div_n = n;
}

static inline struct clk* to_parent_clk(unsigned long xcdr, int id)
{
    int n = get_bit_field(&xcdr, i2s_cs);
    int parent = i2s_clks[id].src[n];

    return get_clk_from_id(parent);
}

static void check_init_i2s_cgu_clk(struct clk *clk);

int i2s_cgu_enable(struct clk *clk, int on)
{
    check_init_i2s_cgu_clk(clk);

    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);
    struct clk *parent = get_clk_from_id(clk->parent);

    int prev_on = i2s_is_enabled(xcdr);
    if (prev_on == on)
        return 0;

    /* 失能 i2s 时钟 */
    if (!on) {
        set_bit_field(&xcdr, i2s_ce, 0);
        cpm_set_xcdr(xcdr, id);
        return 0;
    }

    /* 使能时钟输出， 设置分频值 */
    int div_m = i2s_clks[id].save_div_m;
    int div_n = i2s_clks[id].save_div_n;

    set_bit_field(&xcdr, i2s_div_m, div_m);
    set_bit_field(&xcdr, i2s_div_n, div_n);
    set_bit_field(&xcdr, i2s_ce, 1);
    cpm_set_xcdr(xcdr, id);

    clk->rate = (parent->rate * div_m) / div_n;
    return 0;
}

unsigned long i2s_cgu_do_get_rate(struct clk *clk)
{
    int div_m, div_n;
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);
    struct clk *parent = get_clk_from_id(clk->parent);

    i2s_get_div(xcdr, &div_m, &div_n);

    return div_n ? ((uint64_t)parent->rate * div_m) / div_n : 0;
}

unsigned long i2s_cgu_get_rate(struct clk *clk)
{
    check_init_i2s_cgu_clk(clk);
    return i2s_cgu_do_get_rate(clk);
}

static int i2s_cgu_do_set_rate(struct clk *clk, unsigned long rate)
{
    int div_m, div_n;
    int id = index(clk->id);
    unsigned long xcdr;
    struct clk *parent = get_clk_from_id(clk->parent);

    if (rate == 0 || parent->rate == 0)
        return -2;

    caculate_div(rate, parent->rate, &div_m, &div_n);
    clk->rate = (parent->rate * div_m) / div_n;

    xcdr = cpm_get_xcdr(id);
    if (i2s_is_enabled(xcdr)) {

        set_bit_field(&xcdr, i2s_div_m, div_m);
        set_bit_field(&xcdr, i2s_div_n, div_n);
        cpm_set_xcdr(xcdr, id);
    } else {
        /* 当前还没使能，就先把分频系数给保存下来 */
        i2s_clks[id].save_div_m = div_m;
        i2s_clks[id].save_div_n = div_n;
    }

    return 0;
}

int i2s_cgu_set_rate(struct clk *clk, unsigned long rate)
{
    int ret;
    unsigned long flags;

    check_init_i2s_cgu_clk(clk);

    spin_lock_irqsave(&i2s_lock, flags);

    ret = i2s_cgu_do_set_rate(clk, rate);
 
    spin_unlock_irqrestore(&i2s_lock, flags);

    return ret;
}

struct clk * i2s_cgu_get_parent(struct clk *clk)
{
    check_init_i2s_cgu_clk(clk);

    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);
    return to_parent_clk(xcdr, id);
}

static int i2s_cgu_do_set_parent(struct clk *clk, struct clk *parent)
{
    int i;
    int id = index(clk->id);

    for (i = 0; i < 4; i++) {
        if (i2s_clks[id].src[i] == parent->id)
            break;
    }
    if (i >= 4)
        return -EINVAL;

    /* 设置时钟源 */
    unsigned long xcdr = cpm_get_xcdr(id);
    set_bit_field(&xcdr, i2s_cs, i);
    cpm_set_xcdr(xcdr, id);
    clk->parent = parent->id;
    i2s_cgu_do_set_rate(clk, clk->rate);

    return 0;
}

int i2s_cgu_set_parent(struct clk *clk, struct clk *parent)
{
    int ret;
    unsigned long flags;

    check_init_i2s_cgu_clk(clk);

    spin_lock_irqsave(&i2s_lock, flags);

    ret = i2s_cgu_do_set_parent(clk, parent);

    spin_unlock_irqrestore(&i2s_lock, flags);

    return ret;
}

/* 当此cgu被使用时才 set_parent("sclk_a")
 * 否则双系统时会导致模块驱动没有声音
 */
static void check_init_i2s_cgu_clk(struct clk *clk)
{
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);

    if (i2s_clks[id].is_init)
        return;

    i2s_clks[id].is_init = 1;

    struct clk *parent = clk_get("sclk_a");
    i2s_cgu_do_set_parent(clk, parent);
    clk->parent = parent->id;

    clk->rate = i2s_cgu_do_get_rate(clk);
    i2s_get_div(xcdr, &i2s_clks[id].save_div_m, &i2s_clks[id].save_div_n);
}

void init_i2s_cgu_clk(struct clk *clk)
{
    int id = index(clk->id);
    unsigned long xcdr = cpm_get_xcdr(id);

    if (i2s_is_enabled(xcdr))
        clk->is_init_enabled = 1;
}