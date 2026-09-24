#ifndef _CLK_H_
#define _CLK_H_

#include <soc/clk.h>

struct clk;

void uart_clk_enable(int uart_id, int on);

void init_all_clk(void);

struct clk *clk_get(const char *id);
void clk_put(struct clk *clk);

int clk_enable(struct clk *clk);
void clk_disable(struct clk *clk);
int clk_is_enabled(struct clk *clk);

unsigned long clk_get_rate(struct clk *clk);
int clk_set_rate(struct clk *clk, unsigned long rate);

void clocks_show(void);

void clk_enable_nolock(struct clk *clk);
void clk_disable_nolock(struct clk *clk);

int clk_set_parent(struct clk *clk, struct clk *parent);
struct clk *clk_get_parent(struct clk *clk);

#endif /* _CLK_H_ */