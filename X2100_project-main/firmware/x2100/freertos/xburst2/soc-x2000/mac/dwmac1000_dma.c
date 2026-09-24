#include <io.h>
#include "dwmac1000.h"
#include "dwmac_dma.h"

static int dwmac1000_dma_init(unsigned long ioaddr, int pbl, int fb, int mb,
				  int burst_len, u32 dma_tx, u32 dma_rx, int atds)
{
	u32 value = readl(ioaddr + DMA_BUS_MODE);
	int limit;

	/* DMA SW reset */
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);
	limit = 1000;
	while (limit--) {
		if (!(readl(ioaddr + DMA_BUS_MODE) & DMA_BUS_MODE_SFT_RESET))
			break;
		mdelay(10);
	}
	if (limit < 0)
		return -EBUSY;

	/*
	 * Set the DMA PBL (Programmable Burst Length) mode
	 * Before stmmac core 3.50 this mode bit was 4xPBL, and
	 * post 3.5 mode bit acts as 8*PBL.
	 * For core rev < 3.5, when the core is set for 4xPBL mode, the
	 * DMA transfers the data in 4, 8, 16, 32, 64 & 128 beats
	 * depending on pbl value.
	 * For core rev > 3.5, when the core is set for 8xPBL mode, the
	 * DMA transfers the data in 8, 16, 32, 64, 128 & 256 beats
	 * depending on pbl value.
	 */
	value = DMA_BUS_MODE_PBL | ((pbl << DMA_BUS_MODE_PBL_SHIFT) |
					(pbl << DMA_BUS_MODE_RPBL_SHIFT));

	/* Set the Fixed burst mode */
	if (fb)
		value |= DMA_BUS_MODE_FB;

	/* Mixed Burst has no effect when fb is set */
	if (mb)
		value |= DMA_BUS_MODE_MB;

	if (atds)
		value |= DMA_BUS_MODE_ATDS;

	writel(value, ioaddr + DMA_BUS_MODE);

	/* In case of GMAC AXI configuration, program the DMA_AXI_BUS_MODE
	 * for supported bursts.
	 *
	 * Note: This is applicable only for revision GMACv3.61a. For
	 * older version this register is reserved and shall have no
	 * effect.
	 *
	 * Note:
	 *  For Fixed Burst Mode: if we directly write 0xFF to this
	 *  register using the configurations pass from platform code,
	 *  this would ensure that all bursts supported by core are set
	 *  and those which are not supported would remain ineffective.
	 *
	 *  For Non Fixed Burst Mode: provide the maximum value of the
	 *  burst length. Any burst equal or below the provided burst
	 *  length would be allowed to perform.
	 */
	writel(burst_len, ioaddr + DMA_AXI_BUS_MODE);

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_INTR_DEFAULT_MASK, ioaddr + DMA_INTR_ENA);

	/* RX/TX descriptor base address lists must be written into
	 * DMA CSR3 and CSR4, respectively
	 */
	writel(dma_tx, ioaddr + DMA_TX_BASE_ADDR);
	writel(dma_rx, ioaddr + DMA_RCV_BASE_ADDR);

	return 0;
}

static void dwmac1000_dma_operation_mode(unsigned long ioaddr, int txmode,
					 int rxmode)
{
	u32 csr6 = readl(ioaddr + DMA_CONTROL);

	if (txmode == SF_DMA_MODE) {
		/* Transmit COE type 2 cannot be done in cut-through mode. */
		csr6 |= DMA_CONTROL_TSF;
		/* Operating on second frame increase the performance
		 * especially when transmit store-and-forward is used.
		 */
		csr6 |= DMA_CONTROL_OSF;
	} else {
		csr6 &= ~DMA_CONTROL_TSF;
		csr6 &= DMA_CONTROL_TC_TX_MASK;
		/* Set the transmit threshold */
		if (txmode <= 32)
			csr6 |= DMA_CONTROL_TTC_32;
		else if (txmode <= 64)
			csr6 |= DMA_CONTROL_TTC_64;
		else if (txmode <= 128)
			csr6 |= DMA_CONTROL_TTC_128;
		else if (txmode <= 192)
			csr6 |= DMA_CONTROL_TTC_192;
		else
			csr6 |= DMA_CONTROL_TTC_256;
	}

	if (rxmode == SF_DMA_MODE) {
		csr6 |= DMA_CONTROL_RSF;
	} else {
		csr6 &= ~DMA_CONTROL_RSF;
		csr6 &= DMA_CONTROL_TC_RX_MASK;
		if (rxmode <= 32)
			csr6 |= DMA_CONTROL_RTC_32;
		else if (rxmode <= 64)
			csr6 |= DMA_CONTROL_RTC_64;
		else if (rxmode <= 96)
			csr6 |= DMA_CONTROL_RTC_96;
		else
			csr6 |= DMA_CONTROL_RTC_128;
	}

	writel(csr6, ioaddr + DMA_CONTROL);
}

static unsigned int dwmac1000_get_hw_feature(unsigned long ioaddr)
{
	return readl(ioaddr + DMA_HW_FEATURE);
}

static void dwmac1000_rx_watchdog(unsigned long ioaddr, u32 riwt)
{
	writel(riwt, ioaddr + DMA_RX_WATCHDOG);
}

const struct stmmac_dma_ops dwmac1000_dma_ops = {
	.init = dwmac1000_dma_init,
	.dma_mode = dwmac1000_dma_operation_mode,
	.enable_dma_transmission = dwmac_enable_dma_transmission,
	.enable_dma_irq = dwmac_enable_dma_irq,
	.disable_dma_irq = dwmac_disable_dma_irq,
	.start_tx = dwmac_dma_start_tx,
	.stop_tx = dwmac_dma_stop_tx,
	.start_rx = dwmac_dma_start_rx,
	.stop_rx = dwmac_dma_stop_rx,
	.dma_interrupt = dwmac_dma_interrupt,
	.get_hw_feature = dwmac1000_get_hw_feature,
	.rx_watchdog = dwmac1000_rx_watchdog,
};
