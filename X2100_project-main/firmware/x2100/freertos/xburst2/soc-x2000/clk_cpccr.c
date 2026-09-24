#include <spinlock.h>

#include <asm/cacheops.h>
#include <soc/cpm.h>
#include <driver/cache.h>
#include <soc/base.h>
#include <soc/extal.h>
#include "clk.h"

#include <common.h>

enum {
    SCLK_A = 1,
    APLL,
    MPLL,
    EXCLK,
    H2CLK,

    SELECT_TYPES
};

#define NO_USE 255

struct cpccr_reg {
    unsigned char sel_src[3];
    unsigned char sel;
    unsigned char ce;
    unsigned char div;
};

#define GATE_SCLKA 23

#define index(id) ((id) - CLK_ID_SCLK_A)

static const struct cpccr_reg regs[] = {
    [index(CLK_ID_CCLK)]   = {{CLK_ID_EXT, CLK_ID_SCLK_A, CLK_ID_MPLL}, 28, 22, 0},
    [index(CLK_ID_L2CLK)]  = {{CLK_ID_EXT, CLK_ID_SCLK_A, CLK_ID_MPLL}, 28, 22, 4},
    [index(CLK_ID_H0CLK)]  = {{CLK_ID_EXT, CLK_ID_SCLK_A, CLK_ID_MPLL}, 26, 21, 8},
    [index(CLK_ID_H2CLK)]  = {{CLK_ID_EXT, CLK_ID_SCLK_A, CLK_ID_MPLL}, 24, 20, 12},
    [index(CLK_ID_PCLK)]   = {{CLK_ID_EXT, CLK_ID_SCLK_A, CLK_ID_MPLL}, 24, 20, 16},
    [index(CLK_ID_SCLK_A)] = {{CLK_ID_EXT, CLK_ID_EXT1,   CLK_ID_APLL}, 30, NO_USE, NO_USE},
};

static inline struct clk *to_parent_clk(unsigned long cpccr, int id)
{
    int sel = regs[id].sel;
    int n = get_bit_field(&cpccr, sel, sel + 1);
    int parent = regs[id].sel_src[n];

    return get_clk_from_id(parent);
}

unsigned long cpccr_get_rate(struct clk *clk)
{
    unsigned long cpccr = cpm_inl(CPM_CPCCR);

    struct clk *parent = get_clk_from_id(clk->parent);
    unsigned int rate = clk_get_rate(parent);
    unsigned int div = regs[index(clk->id)].div;

    if (div == NO_USE)
        return rate;

    div = get_bit_field(&cpccr, div, div + 3);

    return rate / (div + 1);
}

void init_cpccr_clk(struct clk *clk)
{
    unsigned long cpccr = cpm_inl(CPM_CPCCR);
    struct clk *parent = to_parent_clk(cpccr, index(clk->id));

    clk->parent = parent->id;
    clk->rate = cpccr_get_rate(clk);
    clk->is_init_enabled = !!clk->rate;

    if (clk->id == CLK_ID_SCLK_A) {
        if (cpccr & BIT(GATE_SCLKA))
            clk->is_init_enabled = 0;
    }
}
