#include <common.h>
#include <driver/clk.h>

#include "ingenic_phy_clk.h"

static unsigned int mac_init_flag;
static unsigned int phy_clk_rate = CONFIG_X2000_MAC_PHY_CLK_RATE;
static struct clk *phy_clk;

void mac_enable_phy_clk(void)
{
    if (phy_clk_rate) {
        if (!mac_init_flag) {
            phy_clk = clk_get("cgu_macphy");
            assert(phy_clk);

            clk_set_rate(phy_clk, phy_clk_rate);
            clk_enable(phy_clk);
        }

        mac_init_flag++;
    }
}

void mac_disable_phy_clk(void)
{
    if (phy_clk_rate) {
        mac_init_flag--;

        if (!mac_init_flag) {
            clk_disable(phy_clk);
            clk_put(phy_clk);
            phy_clk = NULL;
        }
    }

}

void soc_mac_init(void)
{
#ifdef CONFIG_X2000_MAC0
    soc_mac0_init();
#endif

#ifdef CONFIG_X2000_MAC1
    soc_mac1_init();
#endif
}

void soc_mac_deinit(void)
{
#ifdef CONFIG_X2000_MAC1
    soc_mac1_deinit();
#endif

#ifdef CONFIG_X2000_MAC0
    soc_mac0_deinit();
#endif
}