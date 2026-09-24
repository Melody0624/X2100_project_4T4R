#include <io.h>

#include "stmmac.h"
#include "dwmac1000.h"

static void dwmac1000_core_init(unsigned long ioaddr, int mtu)
{
	u32 value = readl(ioaddr + GMAC_CONTROL);
	value |= GMAC_CORE_INIT;
	if (mtu > 1500)
		value |= GMAC_CONTROL_2K;
	if (mtu > 2000)
		value |= GMAC_CONTROL_JE;

	writel(value, ioaddr + GMAC_CONTROL);

	/* Mask GMAC interrupts */
	writel(0x207, ioaddr + GMAC_INT_MASK);

}

static int dwmac1000_rx_ipc_enable(unsigned long ioaddr)
{
	u32 value = readl(ioaddr + GMAC_CONTROL);

	value |= GMAC_CONTROL_IPC;
	writel(value, ioaddr + GMAC_CONTROL);

	value = readl(ioaddr + GMAC_CONTROL);

	return !!(value & GMAC_CONTROL_IPC);
}

static void dwmac1000_set_umac_addr(unsigned long ioaddr, unsigned char *addr,
					unsigned int reg_n)
{
	stmmac_set_mac_addr(ioaddr, addr, GMAC_ADDR_HIGH(reg_n),
				GMAC_ADDR_LOW(reg_n));
}

static void dwmac1000_get_umac_addr(unsigned long ioaddr, unsigned char *addr,
					unsigned int reg_n)
{
	stmmac_get_mac_addr(ioaddr, addr, GMAC_ADDR_HIGH(reg_n),
				GMAC_ADDR_LOW(reg_n));
}

static void dwmac1000_flow_ctrl(unsigned long ioaddr, unsigned int duplex,
				unsigned int fc, unsigned int pause_time)
{
	unsigned int flow = 0;

	if (fc & FLOW_RX)
		flow |= GMAC_FLOW_CTRL_RFE;

	if (fc & FLOW_TX)
		flow |= GMAC_FLOW_CTRL_TFE;

	if (duplex)
		flow |= (pause_time << GMAC_FLOW_CTRL_PT_SHIFT);

	writel(flow, ioaddr + GMAC_FLOW_CTRL);
}

static int dwmac1000_irq_status(unsigned long ioaddr,
				struct stmmac_extra_stats *x)
{
	u32 intr_status = readl(ioaddr + GMAC_INT_STATUS);
	int ret = 0;

	/* Not used events (e.g. MMC interrupts) are not handled. */
	if ((intr_status & mmc_tx_irq))
		x->mmc_tx_irq_n++;
	if (unlikely(intr_status & mmc_rx_irq))
		x->mmc_rx_irq_n++;
	if (unlikely(intr_status & mmc_rx_csum_offload_irq))
		x->mmc_rx_csum_offload_irq_n++;
	if (unlikely(intr_status & pmt_irq)) {
		/* clear the PMT bits 5 and 6 by reading the PMT status reg */
		readl(ioaddr + GMAC_PMT);
		x->irq_receive_pmt_irq_n++;
	}
	/* MAC trx/rx EEE LPI entry/exit interrupts */
	if (intr_status & lpiis_irq) {
		/* Clean LPI interrupt by reading the Reg 12 */
		ret = readl(ioaddr + LPI_CTRL_STATUS);

		if (ret & LPI_CTRL_STATUS_TLPIEN)
			x->irq_tx_path_in_lpi_mode_n++;
		if (ret & LPI_CTRL_STATUS_TLPIEX)
			x->irq_tx_path_exit_lpi_mode_n++;
		if (ret & LPI_CTRL_STATUS_RLPIEN)
			x->irq_rx_path_in_lpi_mode_n++;
		if (ret & LPI_CTRL_STATUS_RLPIEX)
			x->irq_rx_path_exit_lpi_mode_n++;
	}

	if ((intr_status & pcs_ane_irq) || (intr_status & pcs_link_irq)) {
		readl(ioaddr + GMAC_AN_STATUS);
		x->irq_pcs_ane_n++;
	}
	if (intr_status & rgmii_irq) {
		u32 status = readl(ioaddr + GMAC_S_R_GMII);
		x->irq_rgmii_n++;

		/* Save and dump the link status. */
		if (status & GMAC_S_R_GMII_LINK) {
			int speed_value = (status & GMAC_S_R_GMII_SPEED) >>
				GMAC_S_R_GMII_SPEED_SHIFT;
			x->pcs_duplex = (status & GMAC_S_R_GMII_MODE);

			if (speed_value == GMAC_S_R_GMII_SPEED_125)
				x->pcs_speed = 1000;
			else if (speed_value == GMAC_S_R_GMII_SPEED_25)
				x->pcs_speed = 100;
			else
				x->pcs_speed = 10;

			x->pcs_link = 1;
			printf("%s: Link is Up\n", __func__);
		} else {
			x->pcs_link = 0;
			printf("%s: Link is Down\n", __func__);
		}
	}

	return ret;
}

static void dwmac1000_ctrl_ane(unsigned long ioaddr, int restart)
{
	/* auto negotiation enable and External Loopback enable */
	u32 value = GMAC_AN_CTRL_ANE | GMAC_AN_CTRL_ELE;

	if (restart)
		value |= GMAC_AN_CTRL_RAN;

	writel(value, ioaddr + GMAC_AN_CTRL);
}

static const struct stmmac_ops dwmac1000_ops = {
	.core_init = dwmac1000_core_init,
	.rx_ipc = dwmac1000_rx_ipc_enable,
	.host_irq_status = dwmac1000_irq_status,
	.flow_ctrl = dwmac1000_flow_ctrl,
	.set_umac_addr = dwmac1000_set_umac_addr,
	.get_umac_addr = dwmac1000_get_umac_addr,
	.ctrl_ane = dwmac1000_ctrl_ane,
};

void dwmac1000_setup(struct stmmac_priv *priv)
{
	u32 hwid = readl(priv->plat->ioaddr + GMAC_VERSION);

	priv->hw.mac = &dwmac1000_ops;
	priv->hw.dma = &dwmac1000_dma_ops;

	priv->hw.link.port = GMAC_CONTROL_PS;
	priv->hw.link.duplex = GMAC_CONTROL_DM;
	priv->hw.link.speed = GMAC_CONTROL_FES;
	priv->hw.mii.addr = GMAC_MII_ADDR;
	priv->hw.mii.data = GMAC_MII_DATA;
	priv->hw.synopsys_uid = hwid;
}
