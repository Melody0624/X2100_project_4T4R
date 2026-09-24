#include "stmmac.h"

static void stmmac_refill_desc3(void *priv_ptr, struct dma_desc *p)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)priv_ptr;

	/* Fill DES3 in case of RING mode */
	if (priv->dma_buf_sz >= BUF_SIZE_8KiB)
		p->des3 = p->des2 + BUF_SIZE_8KiB;
}

/* In ring mode we need to fill the desc3 because it is used as buffer */
static void stmmac_init_desc3(struct dma_desc *p)
{
	p->des3 = p->des2 + BUF_SIZE_8KiB;
}

static void stmmac_clean_desc3(void *priv_ptr, struct dma_desc *p)
{
	if (unlikely(p->des3))
		p->des3 = 0;
}

static int stmmac_set_16kib_bfsize(int mtu)
{
	int ret = 0;
	if (unlikely(mtu > BUF_SIZE_8KiB))
		ret = BUF_SIZE_16KiB;
	return ret;
}

const struct stmmac_mode_ops ring_mode_ops = {
	.refill_desc3 = stmmac_refill_desc3,
	.init_desc3 = stmmac_init_desc3,
	.clean_desc3 = stmmac_clean_desc3,
	.set_16kib_bfsize = stmmac_set_16kib_bfsize,
};
