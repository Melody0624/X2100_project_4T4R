#include "stmmac.h"

static void stmmac_init_dma_chain(void *des, dma_addr_t phy_addr,
				  unsigned int size, unsigned int extend_desc)
{
	/*
	 * In chained mode the des3 points to the next element in the ring.
	 * The latest element has to point to the head.
	 */
	int i;
	dma_addr_t dma_phy = phy_addr;

	if (extend_desc) {
		struct dma_extended_desc *p = (struct dma_extended_desc *)des;
		for (i = 0; i < (size - 1); i++) {
			dma_phy += sizeof(struct dma_extended_desc);
			p->basic.des3 = (unsigned int)dma_phy;
			p++;
		}
		p->basic.des3 = (unsigned int)phy_addr;

	} else {
		struct dma_desc *p = (struct dma_desc *)des;
		for (i = 0; i < (size - 1); i++) {
			dma_phy += sizeof(struct dma_desc);
			p->des3 = (unsigned int)dma_phy;
			p++;
		}
		p->des3 = (unsigned int)phy_addr;
	}
}

static void stmmac_refill_desc3(void *priv_ptr, struct dma_desc *p)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)priv_ptr;

	if (priv->hwts_rx_en && !priv->extend_desc)
		/* NOTE: Device will overwrite des3 with timestamp value if
		 * 1588-2002 time stamping is enabled, hence reinitialize it
		 * to keep explicit chaining in the descriptor.
		 */
		p->des3 = (unsigned int)(priv->dma_rx_phy +
					 (((priv->dirty_rx) + 1) %
					  priv->dma_rx_size) *
					 sizeof(struct dma_desc));
}

static void stmmac_clean_desc3(void *priv_ptr, struct dma_desc *p)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)priv_ptr;

	if (priv->hw.desc->get_tx_ls(p) && !priv->extend_desc)
		/* NOTE: Device will overwrite des3 with timestamp value if
		 * 1588-2002 time stamping is enabled, hence reinitialize it
		 * to keep explicit chaining in the descriptor.
		 */
		p->des3 = (unsigned int)(priv->dma_tx_phy +
					 (((priv->dirty_tx + 1) %
					   priv->dma_tx_size) *
					  sizeof(struct dma_desc)));
}

const struct stmmac_mode_ops chain_mode_ops = {
	.init = stmmac_init_dma_chain,
	.refill_desc3 = stmmac_refill_desc3,
	.clean_desc3 = stmmac_clean_desc3,
};
