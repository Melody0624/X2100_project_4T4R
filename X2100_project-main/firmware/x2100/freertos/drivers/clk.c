#include <driver/clk.h>

/*
 * soc 需要实现
 */
void soc_uart_clk_enable(int uart_id, int on);
void soc_init_all_clk(void);
struct clk *soc_clk_get(const char *id);
void soc_clk_put(struct clk *clk);
int soc_clk_enable(struct clk *clk);
void soc_clk_disable(struct clk *clk);
int soc_clk_is_enabled(struct clk *clk);
unsigned long soc_clk_get_rate(struct clk *clk);
int soc_clk_set_rate(struct clk *clk, unsigned long rate);
void soc_clocks_show(void);
void soc_clk_enable_nolock(struct clk *clk);
void soc_clk_disable_nolock(struct clk *clk);
int soc_clk_set_parent(struct clk *clk, struct clk *parent);
struct clk *soc_clk_get_parent(struct clk *clk);

void uart_clk_enable(int uart_id, int on)
{
    soc_uart_clk_enable(uart_id, on);
}

void init_all_clk(void)
{
    soc_init_all_clk();
}

struct clk *clk_get(const char *id)
{
    return soc_clk_get(id);
}

void clk_put(struct clk *clk)
{
    soc_clk_put(clk);
}

int clk_enable(struct clk *clk)
{
    return soc_clk_enable(clk);
}

void clk_disable(struct clk *clk)
{
    return soc_clk_disable(clk);
}

int clk_is_enabled(struct clk *clk)
{
    return soc_clk_is_enabled(clk);
}

unsigned long clk_get_rate(struct clk *clk)
{
    return soc_clk_get_rate(clk);
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
    return soc_clk_set_rate(clk, rate);
}

void clocks_show(void)
{
    return soc_clocks_show();
}

void clk_enable_nolock(struct clk *clk)
{
    soc_clk_enable_nolock(clk);
}

void clk_disable_nolock(struct clk *clk)
{
    soc_clk_disable_nolock(clk);
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
    return soc_clk_set_parent(clk, parent);
}

struct clk *clk_get_parent(struct clk *clk)
{
    return soc_clk_get_parent(clk);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(uart_clk_enable);
EXPORT_SYMBOL(clk_get);
EXPORT_SYMBOL(clk_put);
EXPORT_SYMBOL(clk_enable);
EXPORT_SYMBOL(clk_disable);
EXPORT_SYMBOL(clk_is_enabled);
EXPORT_SYMBOL(clk_get_rate);
EXPORT_SYMBOL(clk_set_rate);
EXPORT_SYMBOL(clocks_show);
EXPORT_SYMBOL(clk_enable_nolock);
EXPORT_SYMBOL(clk_disable_nolock);
EXPORT_SYMBOL(clk_set_parent);
EXPORT_SYMBOL(clk_get_parent);