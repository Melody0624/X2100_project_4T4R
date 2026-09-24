#ifndef __MMC_H__
#define __MMC_H__

/* MMC control register */
/* When set, all counter are reset */
#define MMC_CNTRL_COUNTER_RESET		0x1
/* When set, do not roll over zero after reaching the max value*/
#define MMC_CNTRL_COUNTER_STOP_ROLLOVER	0x2
#define MMC_CNTRL_RESET_ON_READ		0x4	/* Reset after reading */
#define MMC_CNTRL_COUNTER_FREEZER	0x8	/* Freeze counter values to the current value.*/
#define MMC_CNTRL_PRESET		0x10
#define MMC_CNTRL_FULL_HALF_PRESET	0x20

void dwmac_mmc_ctrl(unsigned long ioaddr, unsigned int mode);
void dwmac_mmc_intr_all_mask(unsigned long ioaddr);

#endif /* __MMC_H__ */
