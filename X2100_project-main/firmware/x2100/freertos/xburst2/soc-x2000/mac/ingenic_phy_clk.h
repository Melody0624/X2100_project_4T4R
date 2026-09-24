#ifndef __INGENIC_PHY_CLK_H__
#define __INGENIC_PHY_CLK_H__

void mac_enable_phy_clk(void);
void mac_disable_phy_clk(void);

#ifdef CONFIG_X2000_MAC0
void soc_mac0_init(void);
void soc_mac0_deinit(void);
#endif

#ifdef CONFIG_X2000_MAC1
void soc_mac1_init(void);
void soc_mac1_deinit(void);
#endif

#endif /* __INGENIC_PHY_CLK_H__ */
