#include <string.h>
#include "common.h"
#include "spinlock.h"
#include <soc/cpm.h>
#include <soc/base.h>

#include <soc/rtc.h>
#include <soc/extal.h>

#include "clk.h"

struct clk_ops clkops[CLK_TYPE_NUMS];

static DEFINE_SPINLOCK(clk_lock);

void soc_init_all_clk(void)
{
    int i;
    struct clk *clks = get_clk_from_id(0);
    int clks_size = get_clk_sources_size();

    for(i = 0; i < clks_size; i++) {
        if (clkops[clks[i].type].init)
            clkops[clks[i].type].init(&clks[i]);
    }

    init_rtc_internal_clk();

#ifdef DEBUG
    printf("CCLK:%luMHz L2CLK:%luMhz H0CLK:%luMHz H2CLK:%luMhz PCLK:%luMhz\n",
            clks[CLK_ID_CCLK].rate/1000/1000,
            clks[CLK_ID_L2CLK].rate/1000/1000,
            clks[CLK_ID_H0CLK].rate/1000/1000,
            clks[CLK_ID_H2CLK].rate/1000/1000,
            clks[CLK_ID_PCLK].rate/1000/1000);
#endif
}

struct clk *soc_clk_get(const char *id)
{
    int i;
    struct clk *clks = get_clk_from_id(0);
    int clks_size = get_clk_sources_size();

    assert(id);

    for(i = 0; i < clks_size; i++) {
        if (clks[i].name && !strcmp(id, clks[i].name))
            return &clks[i];
    }

    return NULL;
}

void soc_clk_enable_nolock(struct clk *clk)
{
    if (clk->count || clk->is_init_enabled) {
        clk->count++;
        return;
    }

    if (!clkops[clk->type].enable) {
        printf("clk: failed to enable [%s], no enable ops\n", clk->name);
        return;
    }

    if (clk->parent)
        soc_clk_enable_nolock(get_clk_from_id(clk->parent));

    clkops[clk->type].enable(clk, 1);

    clk->count++;
}

int soc_clk_enable(struct clk *clk)
{
    unsigned long flags;

    assert(clk);

    spin_lock_irqsave(&clk_lock, flags);
    soc_clk_enable_nolock(clk);
    spin_unlock_irqrestore(&clk_lock, flags);

    return 0;
}

int soc_clk_is_enabled(struct clk *clk)
{
    return clk->count || clk->is_init_enabled;
}

void soc_clk_disable_nolock(struct clk *clk)
{
    if (!clk->count && !clk->is_init_enabled)
        return;

    if (clk->count) {
        if (--clk->count)
            return;

        if (clk->is_init_enabled)
            return;
    }

    if (!clkops[clk->type].enable) {
        printf("clk: failed to disable [%s], no enable ops\n", clk->name);
        return;
    }

    clkops[clk->type].enable(clk, 0);

    if (clk->is_init_enabled) {
        clk->is_init_enabled = 0;
        return;
    }

    if (clk->parent)
        soc_clk_disable_nolock(get_clk_from_id(clk->parent));
}

void soc_clk_disable(struct clk *clk)
{
    unsigned long flags;

    assert(clk);

    spin_lock_irqsave(&clk_lock, flags);
    soc_clk_disable_nolock(clk);
    spin_unlock_irqrestore(&clk_lock, flags);
}

unsigned long soc_clk_get_rate(struct clk *clk)
{
    assert(clk);

    if (clkops[clk->type].get_rate)
        return clkops[clk->type].get_rate(clk);
    else
        return clk->rate;
}

void soc_clk_put(struct clk *clk)
{
    (void) clk;
}

int soc_clk_set_rate(struct clk *clk, unsigned long rate)
{
    int ret = 0;

    assert(clk);

    if (clk->rate != rate)
        ret = clkops[clk->type].set_rate(clk, rate);

    return ret;
}

int soc_clk_set_parent(struct clk *clk, struct clk *parent)
{
    int err = 0, flags;

    assert(clk);

    spin_lock_irqsave(&clk_lock, flags);

    if (clkops[clk->type].set_parent)
        err = clkops[clk->type].set_parent(clk, parent);

    if (!err) {
        clk->parent = parent->id;
        clk->rate = soc_clk_get_rate(clk);
    }

    spin_unlock_irqrestore(&clk_lock, flags);

    return err;
}

struct clk *soc_clk_get_parent(struct clk *clk)
{
    assert(clk);

    if (clk->parent)
        return get_clk_from_id(clk->parent);
    else
        return NULL;
}

void soc_clocks_show(void)
{
    int i;
    struct clk *clk_srcs = get_clk_from_id(0);

    printf("ID          NAME       FRE      stat     count   parent\n");
    for(i = 0; i < get_clk_sources_size(); i++) {
        struct clk *clk = &clk_srcs[i];
        unsigned int mhz = clk->rate / 1000;

        if (clk->name == NULL || clk->id == CLK_ID_EXT) {
            printf("--------------------------------------------------------\n");
            continue;
        }

        printf("%2d %15s  %4d.%02dMHz  %s  %2d    %s\n",
                i, clk_srcs[i].name
                , mhz/1000, mhz%1000
                , soc_clk_is_enabled(clk) ? " enable": "disable"
                , clk->count
                , clk_srcs[clk->parent].name);
    }
}
