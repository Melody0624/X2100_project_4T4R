#include <soc/cpm.h>
#include <soc/base.h>

#include "clk.h"
#define CONFIG_NO_SPINLOCK_CHECK
#include "spinlock.h"

#define CPM_TCU_GATE    18
#define CPM_OST_GATE    20
#define CPM_INTC_GATE   26

/* CPM_CLKGR0: uart [0/1/2] */
static void soc_uart_clk0_enable(int uart_id, int on)
{
    int bit = 14 + uart_id;
    unsigned int clkgr = CPM_CLKGR0;

    if(on)
        cpm_clear_bit(bit, clkgr);
    else
        cpm_set_bit(bit, clkgr);
}

/* CPM_CLKGR1: uart [3..9] */
static void soc_uart_clk1_enable(int uart_id, int on)
{
    int bit = 16 + (uart_id - 3);
    unsigned int clkgr = CPM_CLKGR1;

    if(on)
        cpm_clear_bit(bit, clkgr);
    else
        cpm_set_bit(bit, clkgr);
}

void soc_uart_clk_enable(int uart_id, int on)
{
    assert(uart_id >= 0 && uart_id <= 9);

    if (uart_id <= 2)
        soc_uart_clk0_enable(uart_id, on);
    else
        soc_uart_clk1_enable(uart_id, on);
}

void soc_intc_clk_enable(void)
{
    cpm_clear_bit(CPM_INTC_GATE, CPM_CLKGR1);
}

void soc_ost_clk_enable(int on)
{
    if(on)
        cpm_clear_bit(CPM_OST_GATE, CPM_CLKGR0);
    else
        cpm_set_bit(CPM_OST_GATE, CPM_CLKGR0);
}

void soc_tcu_clk_enable(int on)
{
    if(on)
        cpm_clear_bit(CPM_TCU_GATE, CPM_CLKGR0);
    else
        cpm_set_bit(CPM_TCU_GATE, CPM_CLKGR0);
}

int cpm_gate_enable(struct clk *clk, int on)
{
    int bit = clk->type_value;
    unsigned int clkgr = clk->type == CLK_TYPE_GATE0 ? CPM_CLKGR0 : CPM_CLKGR1;

    if(on)
        cpm_clear_bit(bit, clkgr);
    else
        cpm_set_bit(bit, clkgr);

    return 0;
}

void init_gate_clk(struct clk *clk)
{
    static unsigned long clkgr0, clkgr1;
    static int clkgr_init = 0;

    if(clkgr_init == 0){
        clkgr0 = cpm_inl(CPM_CLKGR0);
        clkgr1 = cpm_inl(CPM_CLKGR1);
        clkgr_init = 1;
    }

    int bit = clk->type_value;
    unsigned long clkgr = clk->type == CLK_TYPE_GATE0 ? clkgr0 : clkgr1;
    clk->is_init_enabled = !(clkgr & (1 << bit));
    clk->rate = clk_get_rate(get_clk_from_id(clk->parent));
}
