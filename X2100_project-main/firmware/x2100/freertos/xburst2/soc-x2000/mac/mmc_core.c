#include <io.h>
#include "mmc.h"

/* MAC Management Counters register offset */

#define MMC_CNTRL		0x00000100	/* MMC Control */
#define MMC_RX_INTR		0x00000104	/* MMC RX Interrupt */
#define MMC_TX_INTR		0x00000108	/* MMC TX Interrupt */
#define MMC_RX_INTR_MASK	0x0000010c	/* MMC Interrupt Mask */
#define MMC_TX_INTR_MASK	0x00000110	/* MMC Interrupt Mask */
#define MMC_DEFAULT_MASK		0xffffffff

/* IPC*/
#define MMC_RX_IPC_INTR_MASK		0x00000200
#define MMC_RX_IPC_INTR			0x00000208

void dwmac_mmc_ctrl(unsigned long ioaddr, unsigned int mode)
{
	unsigned long value = readl(ioaddr + MMC_CNTRL);

	value |= (mode & 0x3F);

	writel(value, ioaddr + MMC_CNTRL);
}

/* To mask all all interrupts.*/
void dwmac_mmc_intr_all_mask(unsigned long ioaddr)
{
	writel(MMC_DEFAULT_MASK, ioaddr + MMC_RX_INTR_MASK);
	writel(MMC_DEFAULT_MASK, ioaddr + MMC_TX_INTR_MASK);
	writel(MMC_DEFAULT_MASK, ioaddr + MMC_RX_IPC_INTR_MASK);
}
