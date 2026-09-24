#include <irqflags.h>
#include <driver/clk.h>
#include <soc/base.h>
#include <soc/cpm.h>
#include <soc/extal.h>
#include <common.h>
#include <bit_field.h>

#include "clk.h"

static const int pll_no_tab[] = {
    [1] = 2,
    [2] = 4,
    [3] = 8,
    [4] = 16,
    [5] = 32,
    [6] = 64,
};

unsigned long pll_get_rate(struct clk *clk)
{
    int cpxpcr_reg = clk->type_value;
    unsigned long value = cpm_inl(cpxpcr_reg);

    if (!(value & BIT(XPCR_PLLEN)))
        return 0;

    struct clk *parent = get_clk_from_id(clk->parent);

    unsigned int fd = get_bit_field(&value, XPCR_PLLFD);
    unsigned int rd = get_bit_field(&value, XPCR_PLLRD);
    unsigned int od = get_bit_field(&value, XPCR_PLLOD);

    unsigned int nf = fd + 1;
    unsigned int nr = rd + 1;
    unsigned int no = pll_no_tab[od];
    unsigned int rate_in = parent->rate / 1000;
    unsigned int rate = rate_in * 2 * nf / (nr * no);

    return rate * 1000;
}

void init_ext_pll(struct clk *clk)
{
    switch (clk->id) {
    case CLK_ID_EXT0:
        clk->is_init_enabled = 1;
        clk->rate = JZ_EXTAL_RTC;
        break;
    case CLK_ID_EXT1:
        clk->is_init_enabled = 1;
        clk->rate = JZ_EXTAL;
        break;
    case CLK_ID_OTGPHY:
        clk->is_init_enabled = 1;
        clk->rate = 48 * 1000 * 1000;
        break;
    case CLK_ID_EPLL:
    case CLK_ID_APLL:
    case CLK_ID_MPLL:
        clk->parent = CLK_ID_EXT1;
        clk->rate = pll_get_rate(clk);
        clk->is_init_enabled = !!clk->rate;
        break;
    }
}
