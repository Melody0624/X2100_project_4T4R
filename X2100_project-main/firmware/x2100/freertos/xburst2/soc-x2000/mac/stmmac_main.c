#include <os.h>
#include  <driver/irq.h>
#include <driver/cache.h>
#include <driver/clk.h>

#include <lwip/netifapi.h>

#include "stmmac.h"

#define ETH_FCS_LEN	4		/* Octets in the FCS		 */

/* Module parameters */
#define DMA_TX_SIZE 256
static int dma_txsize = DMA_TX_SIZE;

#define DMA_RX_SIZE 256
static int dma_rxsize = DMA_RX_SIZE;

static int flow_ctrl = FLOW_OFF;

static int pause_time = PAUSE_TIME;

#define TC_DEFAULT 64
static int tc = TC_DEFAULT;

#define	DEFAULT_BUFSIZE	1536
static int buf_sz = DEFAULT_BUFSIZE;

/* By default the driver will use the ring mode to manage tx and rx descriptors
 * but passing this value so user can force to use the chain instead of the ring
 */
static unsigned int chain_mode;

/**
 * stmmac_verify_args - verify the driver parameters.
 * Description: it verifies if some wrong parameter is passed to the driver.
 * Note that wrong parameters are replaced with the default values.
 */
static void stmmac_verify_args(void)
{
	if (unlikely(dma_rxsize < 0))
		dma_rxsize = DMA_RX_SIZE;
	if (unlikely(dma_txsize < 0))
		dma_txsize = DMA_TX_SIZE;
	if (unlikely((buf_sz < DEFAULT_BUFSIZE) || (buf_sz > BUF_SIZE_16KiB)))
		buf_sz = DEFAULT_BUFSIZE;
	if (unlikely(flow_ctrl > 1))
		flow_ctrl = FLOW_AUTO;
	else if (likely(flow_ctrl < 0))
		flow_ctrl = FLOW_OFF;
	if (unlikely((pause_time < 0) || (pause_time > 0xffff)))
		pause_time = PAUSE_TIME;
}

static inline u32 stmmac_tx_avail(struct stmmac_priv *priv)
{
	return priv->dirty_tx + priv->dma_tx_size - priv->cur_tx - 1;
}

/**
 * stmmac_init_ptp: init PTP
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PTPv1 or v2.
 * This is done by looking at the HW cap. register.
 * Also it registers the ptp driver.
 */
static void stmmac_init_ptp(struct stmmac_priv *priv)
{
	if (!(priv->dma_cap.time_stamp || priv->dma_cap.atime_stamp))
		return;

	priv->adv_ts = 0;
	if (priv->dma_cap.atime_stamp && priv->extend_desc)
		priv->adv_ts = 1;

	if (priv->dma_cap.time_stamp)
		printf("IEEE 1588-2002 Time Stamp supported\n");

	if (priv->adv_ts)
		printf("IEEE 1588-2008 Advanced Time Stamp supported\n");

	priv->hwts_tx_en = 0;
	priv->hwts_rx_en = 0;
}

/**
 * stmmac_check_pcs_mode: verify if RGMII/SGMII is supported
 * @priv: driver private structure
 * Description: this is to verify if the HW supports the PCS.
 * Physical Coding Sublayer (PCS) interface that can be used when the MAC is
 * configured for the TBI, RTBI, or SGMII PHY interface.
 */
static void stmmac_check_pcs_mode(struct stmmac_priv *priv)
{
	int interface = priv->plat->interface;

	if (priv->dma_cap.pcs) {
		if ((interface == PHY_INTERFACE_MODE_RGMII) ||
			(interface == PHY_INTERFACE_MODE_RGMII_ID) ||
			(interface == PHY_INTERFACE_MODE_RGMII_RXID) ||
			(interface == PHY_INTERFACE_MODE_RGMII_TXID)) {
			printf("STMMAC: PCS RGMII support enable\n");
			priv->pcs = STMMAC_PCS_RGMII;
		} else if (interface == PHY_INTERFACE_MODE_SGMII) {
			printf("STMMAC: PCS SGMII support enable\n");
			priv->pcs = STMMAC_PCS_SGMII;
		}
	}
}

static int stmmac_set_bfsize(int mtu, int bufsize)
{
	int ret = bufsize;

	if (mtu >= BUF_SIZE_4KiB)
		ret = BUF_SIZE_8KiB;
	else if (mtu >= BUF_SIZE_2KiB)
		ret = BUF_SIZE_4KiB;
	else if (mtu > DEFAULT_BUFSIZE)
		ret = BUF_SIZE_2KiB;
	else
		ret = DEFAULT_BUFSIZE;

	return ret;
}

/**
 * stmmac_clear_descriptors: clear descriptors
 * @priv: driver private structure
 * Description: this function is called to clear the tx and rx descriptors
 * in case of both basic and extended descriptors are used.
 */
static void stmmac_clear_descriptors(struct stmmac_priv *priv)
{
	int i;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int rxsize = priv->dma_rx_size;

	/* Clear the Rx/Tx descriptors */
	for (i = 0; i < rxsize; i++)
		if (priv->extend_desc)
			priv->hw.desc->init_rx_desc(&priv->dma_erx[i].basic,
							 priv->use_riwt, priv->mode,
							 (i == rxsize - 1), priv->dma_buf_sz);
		else
			priv->hw.desc->init_rx_desc(&priv->dma_rx[i],
							 priv->use_riwt, priv->mode,
							 (i == rxsize - 1), priv->dma_buf_sz);
	for (i = 0; i < txsize; i++)
		if (priv->extend_desc)
			priv->hw.desc->init_tx_desc(&priv->dma_etx[i].basic,
							 priv->mode,
							 (i == txsize - 1));
		else
			priv->hw.desc->init_tx_desc(&priv->dma_tx[i],
							 priv->mode,
							 (i == txsize - 1));
}

static int stmmac_init_buffers(struct stmmac_priv *priv, struct dma_desc *p,
				  int i)
{
	priv->tx_skbuff[i] = cache_align_malloc(priv->dma_buf_sz);
	if (!priv->tx_skbuff[i]) {
		printf("%s: init fails; tx_skbuff is NULL\n", __func__);
		return -ENOMEM;
	}

	priv->rx_skbuff[i] = cache_align_malloc(priv->dma_buf_sz);
	if (!priv->rx_skbuff[i]) {
		free(priv->tx_skbuff[i]);
		priv->tx_skbuff[i] = NULL;
		printf("%s: init fails; rx_skbuff is NULL\n", __func__);
		return -ENOMEM;
	}

	flush_dcache((unsigned long)priv->rx_skbuff[i], priv->dma_buf_sz);
	priv->rx_skbuff_dma[i] = CPHYSADDR(priv->rx_skbuff[i]);
	p->des2 = priv->rx_skbuff_dma[i];

	if ((priv->hw.mode->init_desc3) &&
		(priv->dma_buf_sz == BUF_SIZE_16KiB))
		priv->hw.mode->init_desc3(p);

	return 0;
}

static void stmmac_free_buffers(struct stmmac_priv *priv, int i)
{
	if (priv->tx_skbuff[i]) {
		priv->tx_skbuff_dma[i] = 0;
		free(priv->tx_skbuff[i]);
		priv->tx_skbuff[i] = NULL;
	}

	if (priv->rx_skbuff[i]) {
		invalidate_dcache((unsigned long)priv->rx_skbuff[i], priv->dma_buf_sz);
		priv->rx_skbuff_dma[i] = 0;
		free(priv->rx_skbuff[i]);
		priv->rx_skbuff[i] = NULL;
	}
}

/**
 * init_dma_desc_rings - init the RX/TX descriptor rings
 * @dev: net device structure
 * Description:  this function initializes the DMA RX/TX descriptors
 * and allocates the socket buffers. It suppors the chained and ring
 * modes.
 */
static int init_dma_desc_rings(struct stmmac_priv *priv)
{
	int i;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int rxsize = priv->dma_rx_size;
	unsigned int bfsize = 0;
	int ret = -ENOMEM;

	if (priv->hw.mode->set_16kib_bfsize)
		bfsize = priv->hw.mode->set_16kib_bfsize(priv->plat->mtu);

	if (bfsize < BUF_SIZE_16KiB)
		bfsize = stmmac_set_bfsize(priv->plat->mtu, priv->dma_buf_sz);

	priv->dma_buf_sz = bfsize;

	for (i = 0; i < rxsize; i++) {
		struct dma_desc *p;
		if (priv->extend_desc)
			p = &((priv->dma_erx + i)->basic);
		else
			p = priv->dma_rx + i;

		ret = stmmac_init_buffers(priv, p, i);
		if (ret)
			goto err_init_rx_buffers;
	}
	priv->cur_rx = 0;
	priv->dirty_rx = 0;
	buf_sz = bfsize;

	/* Setup the chained descriptor addresses */
	if (priv->mode == STMMAC_CHAIN_MODE) {
		if (priv->extend_desc) {
			priv->hw.mode->init(priv->dma_erx, priv->dma_rx_phy,
						 rxsize, 1);
			priv->hw.mode->init(priv->dma_etx, priv->dma_tx_phy,
						 txsize, 1);
		} else {
			priv->hw.mode->init(priv->dma_rx, priv->dma_rx_phy,
						 rxsize, 0);
			priv->hw.mode->init(priv->dma_tx, priv->dma_tx_phy,
						 txsize, 0);
		}
	}

	/* TX INITIALIZATION */
	for (i = 0; i < txsize; i++) {
		struct dma_desc *p;
		if (priv->extend_desc)
			p = &((priv->dma_etx + i)->basic);
		else
			p = priv->dma_tx + i;
		p->des2 = 0;
		priv->tx_skbuff_dma[i] = 0;
	}

	priv->dirty_tx = 0;
	priv->cur_tx = 0;

	stmmac_clear_descriptors(priv);
	return 0;

err_init_rx_buffers:
	while (--i >= 0)
		stmmac_free_buffers(priv, i);
	return ret;
}

static void dma_free_skbufs(struct stmmac_priv *priv)
{
	int i;

	for (i = 0; i < priv->dma_rx_size; i++)
		stmmac_free_buffers(priv, i);
}

static void dma_init_tx_skbufs(struct stmmac_priv *priv)
{
	int i;

	for (i = 0; i < priv->dma_tx_size; i++) {
		struct dma_desc *p;

		if (priv->extend_desc)
			p = &((priv->dma_etx + i)->basic);
		else
			p = priv->dma_tx + i;

		p->des2 = 0;
		priv->tx_skbuff_dma[i] = 0;
	}
}

static int alloc_dma_desc_resources(struct stmmac_priv *priv)
{
	unsigned int txsize = priv->dma_tx_size;
	unsigned int rxsize = priv->dma_rx_size;
	int ret = -ENOMEM;

	priv->rx_skbuff_dma = calloc(rxsize, sizeof(dma_addr_t));
	if (!priv->rx_skbuff_dma)
		return -ENOMEM;

	priv->rx_skbuff = calloc(rxsize, sizeof(u8 *));
	if (!priv->rx_skbuff)
		goto err_rx_skbuff;

	priv->tx_skbuff_dma = calloc(txsize, sizeof(dma_addr_t));
	if (!priv->tx_skbuff_dma)
		goto err_tx_skbuff_dma;

	priv->tx_skbuff = calloc(txsize, sizeof(u8 *));
	if (!priv->tx_skbuff)
		goto err_tx_skbuff;

	if (priv->extend_desc) {
		priv->dma_erx = cache_align_malloc(rxsize * sizeof(struct dma_extended_desc));
		priv->dma_erx = (void *)KSEG1ADDR(priv->dma_erx);
		priv->dma_rx_phy = CPHYSADDR(priv->dma_erx);
		if (!priv->dma_erx)
			goto err_dma;

		priv->dma_etx = cache_align_malloc(txsize * sizeof(struct dma_extended_desc));
		priv->dma_etx = (void *)KSEG1ADDR(priv->dma_etx);
		priv->dma_tx_phy = CPHYSADDR(priv->dma_etx);
		if (!priv->dma_etx) {
			priv->dma_erx = (void *)KSEG0ADDR(priv->dma_erx);
			free(priv->dma_erx);
			priv->dma_erx = NULL;
			priv->dma_rx_phy = 0;
			goto err_dma;
		}
	} else {
		priv->dma_rx = cache_align_malloc(rxsize * sizeof(struct dma_desc));
		priv->dma_rx = (void *)KSEG1ADDR(priv->dma_rx);
		priv->dma_rx_phy = CPHYSADDR(priv->dma_rx);
		if (!priv->dma_rx)
			goto err_dma;

		priv->dma_tx = cache_align_malloc(txsize * sizeof(struct dma_desc));
		priv->dma_tx = (void *)KSEG1ADDR(priv->dma_tx);
		priv->dma_tx_phy = CPHYSADDR(priv->dma_tx);
		if (!priv->dma_tx) {
			priv->dma_rx = (void *)KSEG0ADDR(priv->dma_rx);
			free(priv->dma_rx);
			priv->dma_rx = NULL;
			priv->dma_rx_phy = 0;
			goto err_dma;
		}
	}

	return 0;

err_dma:
	free(priv->tx_skbuff);
	priv->tx_skbuff = NULL;
err_tx_skbuff:
	free(priv->tx_skbuff_dma);
	priv->tx_skbuff_dma = NULL;
err_tx_skbuff_dma:
	free(priv->rx_skbuff);
	priv->rx_skbuff = NULL;
err_rx_skbuff:
	free(priv->rx_skbuff_dma);
	priv->rx_skbuff_dma = NULL;
	return ret;
}

static void free_dma_desc_resources(struct stmmac_priv *priv)
{
	/* Release the DMA TX/RX socket buffers */
	dma_free_skbufs(priv);

	/* Free DMA regions of consistent memory previously allocated */
	if (!priv->extend_desc) {
		priv->dma_tx = (void *)KSEG0ADDR(priv->dma_tx);
		free(priv->dma_tx);
		priv->dma_tx = NULL;
		priv->dma_tx_phy = 0;

		priv->dma_rx = (void *)KSEG0ADDR(priv->dma_rx);
		free(priv->dma_rx);
		priv->dma_rx = NULL;
		priv->dma_rx_phy = 0;
	} else {
		priv->dma_etx = (void *)KSEG0ADDR(priv->dma_etx);
		free(priv->dma_etx);
		priv->dma_etx = NULL;
		priv->dma_tx_phy = 0;

		priv->dma_erx = (void *)KSEG0ADDR(priv->dma_erx);
		free(priv->dma_erx);
		priv->dma_erx = NULL;
		priv->dma_rx_phy = 0;
	}

	free(priv->rx_skbuff_dma);
	priv->rx_skbuff_dma = NULL;
	free(priv->rx_skbuff);
	priv->rx_skbuff = NULL;
	free(priv->tx_skbuff_dma);
	priv->tx_skbuff_dma = NULL;
	free(priv->tx_skbuff);
	priv->tx_skbuff = NULL;
}

/**
 *  stmmac_dma_operation_mode - HW DMA operation mode
 *  @priv: driver private structure
 *  Description: it sets the DMA operation mode: tx/rx DMA thresholds
 *  or Store-And-Forward capability.
 */
static void stmmac_dma_operation_mode(struct stmmac_priv *priv)
{
	if (priv->plat->force_thresh_dma_mode)
		priv->hw.dma->dma_mode(priv->plat->ioaddr, tc, tc);
	else if (priv->plat->force_sf_dma_mode || priv->plat->tx_coe) {
		/*
		 * In case of GMAC, SF mode can be enabled
		 * to perform the TX COE in HW. This depends on:
		 * 1) TX COE if actually supported
		 * 2) There is no bugged Jumbo frame support
		 *	that needs to not insert csum in the TDES.
		 */
		priv->hw.dma->dma_mode(priv->plat->ioaddr, SF_DMA_MODE, SF_DMA_MODE);
		tc = SF_DMA_MODE;
	} else
		priv->hw.dma->dma_mode(priv->plat->ioaddr, tc, SF_DMA_MODE);
}

/**
 * stmmac_tx_clean:
 * @priv: driver private structure
 * Description: it reclaims resources after transmission completes.
 */
static void stmmac_tx_clean(struct stmmac_priv *priv)
{
	unsigned int txsize = priv->dma_tx_size;

	spin_lock(&priv->tx_lock);

	priv->xstats.tx_clean++;

	while (priv->dirty_tx != priv->cur_tx) {
		int last;
		unsigned int entry = priv->dirty_tx % txsize;
		struct dma_desc *p;

		if (priv->extend_desc)
			p = (struct dma_desc *)(priv->dma_etx + entry);
		else
			p = priv->dma_tx + entry;

		/* Check if the descriptor is owned by the DMA. */
		if (priv->hw.desc->get_tx_owner(p))
			break;

		/* Verify tx error by looking at the last segment. */
		last = priv->hw.desc->get_tx_ls(p);
		if (likely(last)) {
			int tx_error =
				priv->hw.desc->tx_status(&priv->stats,
							  &priv->xstats, p,
							  priv->plat->ioaddr);
			if (likely(tx_error == 0)) {
				priv->stats.tx_packets++;
				priv->xstats.tx_pkt_n++;
			} else
				priv->stats.tx_errors++;

		}

		priv->tx_skbuff_dma[entry] = 0;

		priv->hw.mode->clean_desc3(priv, p);

		priv->hw.desc->release_tx_desc(p, priv->mode);

		priv->dirty_tx++;
	}

	spin_unlock(&priv->tx_lock);
}

static inline void stmmac_enable_dma_irq(struct stmmac_priv *priv)
{
	priv->hw.dma->enable_dma_irq(priv->plat->ioaddr);
}

static inline void stmmac_disable_dma_irq(struct stmmac_priv *priv)
{
	priv->hw.dma->disable_dma_irq(priv->plat->ioaddr);
}

/**
 * stmmac_tx_err: irq tx error mng function
 * @priv: driver private structure
 * Description: it cleans the descriptors and restarts the transmission
 * in case of errors.
 */
static void stmmac_tx_err(struct stmmac_priv *priv)
{
	int i;
	int txsize = priv->dma_tx_size;

	priv->hw.dma->stop_tx(priv->plat->ioaddr);

	dma_init_tx_skbufs(priv);
	for (i = 0; i < txsize; i++)
		if (priv->extend_desc)
			priv->hw.desc->init_tx_desc(&priv->dma_etx[i].basic,
							 priv->mode,
							 (i == txsize - 1));
		else
			priv->hw.desc->init_tx_desc(&priv->dma_tx[i],
							 priv->mode,
							 (i == txsize - 1));
	priv->dirty_tx = 0;
	priv->cur_tx = 0;
	priv->hw.dma->start_tx(priv->plat->ioaddr);

	priv->stats.tx_errors++;
}

/**
 * stmmac_mmc_setup: setup the Mac Management Counters (MMC)
 * @priv: driver private structure
 * Description: this masks the MMC irq, in fact, the counters are managed in SW.
 */
static void stmmac_mmc_setup(struct stmmac_priv *priv)
{
	unsigned int mode = MMC_CNTRL_RESET_ON_READ | MMC_CNTRL_COUNTER_RESET |
		MMC_CNTRL_PRESET | MMC_CNTRL_FULL_HALF_PRESET;

	dwmac_mmc_intr_all_mask(priv->plat->ioaddr);

	if (priv->dma_cap.rmon)
		dwmac_mmc_ctrl(priv->plat->ioaddr, mode);
	else
		printf(" No MAC Management Counters available\n");
}

static u32 stmmac_get_synopsys_id(struct stmmac_priv *priv)
{
	u32 hwid = priv->hw.synopsys_uid;

	/* Check Synopsys Id (not available on old chips) */
	if (likely(hwid)) {
		u32 uid = ((hwid & 0x0000ff00) >> 8);
		u32 synid = (hwid & 0x000000ff);

		printf("stmmac - user ID: 0x%x, Synopsys ID: 0x%x\n", uid, synid);

		return synid;
	}
	return 0;
}

/**
 * stmmac_selec_desc_mode: to select among: normal/alternate/extend descriptors
 * @priv: driver private structure
 * Description: select the Enhanced/Alternate or Normal descriptors.
 * In case of Enhanced/Alternate, it looks at the extended descriptors are
 * supported by the HW cap. register.
 */
static void stmmac_selec_desc_mode(struct stmmac_priv *priv)
{
	if (priv->plat->enh_desc) {
		printf(" Enhanced/Alternate descriptors\n");

		/* GMAC older than 3.50 has no extended descriptors */
		if (priv->synopsys_id >= DWMAC_CORE_3_50) {
			printf("\tEnabled extended descriptors\n");
			priv->extend_desc = 1;
		} else
			printf("Extended descriptors not supported\n");

		priv->hw.desc = &enh_desc_ops;
	} else {
		printf(" Normal descriptors\n");
		priv->hw.desc = &ndesc_ops;
	}
}

/**
 * stmmac_get_hw_features: get MAC capabilities from the HW cap. register.
 * @priv: driver private structure
 * Description:
 *  new GMAC chip generations have a new register to indicate the
 *  presence of the optional feature/functions.
 *  This can be also used to override the value passed through the
 *  platform and necessary for old MAC10/100 and GMAC chips.
 */
static int stmmac_get_hw_features(struct stmmac_priv *priv)
{
	u32 hw_cap = 0;

	if (priv->hw.dma->get_hw_feature) {
		hw_cap = priv->hw.dma->get_hw_feature(priv->plat->ioaddr);

		priv->dma_cap.mbps_10_100 = (hw_cap & DMA_HW_FEAT_MIISEL);
		priv->dma_cap.mbps_1000 = (hw_cap & DMA_HW_FEAT_GMIISEL) >> 1;
		priv->dma_cap.half_duplex = (hw_cap & DMA_HW_FEAT_HDSEL) >> 2;
		priv->dma_cap.hash_filter = (hw_cap & DMA_HW_FEAT_HASHSEL) >> 4;
		priv->dma_cap.multi_addr = (hw_cap & DMA_HW_FEAT_ADDMAC) >> 5;
		priv->dma_cap.pcs = (hw_cap & DMA_HW_FEAT_PCSSEL) >> 6;
		priv->dma_cap.sma_mdio = (hw_cap & DMA_HW_FEAT_SMASEL) >> 8;
		priv->dma_cap.pmt_remote_wake_up =
			(hw_cap & DMA_HW_FEAT_RWKSEL) >> 9;
		priv->dma_cap.pmt_magic_frame =
			(hw_cap & DMA_HW_FEAT_MGKSEL) >> 10;
		/* MMC */
		priv->dma_cap.rmon = (hw_cap & DMA_HW_FEAT_MMCSEL) >> 11;
		/* IEEE 1588-2002 */
		priv->dma_cap.time_stamp =
			(hw_cap & DMA_HW_FEAT_TSVER1SEL) >> 12;
		/* IEEE 1588-2008 */
		priv->dma_cap.atime_stamp =
			(hw_cap & DMA_HW_FEAT_TSVER2SEL) >> 13;
		/* 802.3az - Energy-Efficient Ethernet (EEE) */
		priv->dma_cap.eee = (hw_cap & DMA_HW_FEAT_EEESEL) >> 14;
		priv->dma_cap.av = (hw_cap & DMA_HW_FEAT_AVSEL) >> 15;
		/* TX and RX csum */
		priv->dma_cap.tx_coe = (hw_cap & DMA_HW_FEAT_TXCOESEL) >> 16;
		priv->dma_cap.rx_coe_type1 =
			(hw_cap & DMA_HW_FEAT_RXTYP1COE) >> 17;
		priv->dma_cap.rx_coe_type2 =
			(hw_cap & DMA_HW_FEAT_RXTYP2COE) >> 18;
		priv->dma_cap.rxfifo_over_2048 =
			(hw_cap & DMA_HW_FEAT_RXFIFOSIZE) >> 19;
		/* TX and RX number of channels */
		priv->dma_cap.number_rx_channel =
			(hw_cap & DMA_HW_FEAT_RXCHCNT) >> 20;
		priv->dma_cap.number_tx_channel =
			(hw_cap & DMA_HW_FEAT_TXCHCNT) >> 22;
		/* Alternate (enhanced) DESC mode */
		priv->dma_cap.enh_desc = (hw_cap & DMA_HW_FEAT_ENHDESSEL) >> 24;
	}

	return hw_cap;
}

/**
 * stmmac_init_dma_engine: DMA init.
 * @priv: driver private structure
 * Description:
 * It inits the DMA invoking the specific MAC/GMAC callback.
 * Some DMA parameters can be passed from the platform;
 * in case of these are not passed a default is kept for the MAC or GMAC.
 */
static int stmmac_init_dma_engine(struct stmmac_priv *priv)
{
	int pbl = DEFAULT_DMA_PBL, fixed_burst = 0, burst_len = 0;
	int mixed_burst = 0;
	int atds = 0;

	if (priv->plat->dma_cfg) {
		pbl = priv->plat->dma_cfg->pbl;
		fixed_burst = priv->plat->dma_cfg->fixed_burst;
		mixed_burst = priv->plat->dma_cfg->mixed_burst;
		burst_len = priv->plat->dma_cfg->burst_len;
	}

	if (priv->extend_desc && (priv->mode == STMMAC_RING_MODE))
		atds = 1;

	return priv->hw.dma->init(priv->plat->ioaddr, pbl, fixed_burst, mixed_burst,
				   burst_len, priv->dma_tx_phy,
				   priv->dma_rx_phy, atds);
}

/**
 * stmmac_hw_setup: setup mac in a usable state.
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function sets up the ip in a usable state.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
static int stmmac_hw_setup(struct stmmac_priv *priv)
{
	int ret;

	ret = init_dma_desc_rings(priv);
	if (ret < 0) {
		printf("%s: DMA descriptors initialization failed\n", __func__);
		return ret;
	}
	/* DMA initialization and SW reset */
	ret = stmmac_init_dma_engine(priv);
	if (ret < 0) {
		printf("%s: DMA engine initialization failed\n", __func__);
		return ret;
	}

	/* Copy the MAC addr into the HW  */
	priv->hw.mac->set_umac_addr(priv->plat->ioaddr, priv->plat->hw_addr, 0);

	/* If required, perform hw setup of the bus. */
	if (priv->plat->bus_setup)
		priv->plat->bus_setup(priv->plat->ioaddr);

	/* Initialize the MAC Core */
	priv->hw.mac->core_init(priv->plat->ioaddr, priv->plat->mtu);

	/* Enable the MAC Rx/Tx */
	stmmac_set_mac(priv->plat->ioaddr, 1);

	/* Set the HW DMA mode and the COE */
	stmmac_dma_operation_mode(priv);

	stmmac_mmc_setup(priv);

	stmmac_init_ptp(priv);

	/* Start the ball rolling... */
	priv->hw.dma->start_tx(priv->plat->ioaddr);
	priv->hw.dma->start_rx(priv->plat->ioaddr);

	if ((priv->use_riwt) && (priv->hw.dma->rx_watchdog)) {
		priv->rx_riwt = MAX_DMA_RIWT;
		priv->hw.dma->rx_watchdog(priv->plat->ioaddr, MAX_DMA_RIWT);
	}

	if (priv->pcs && priv->hw.mac->ctrl_ane)
		priv->hw.mac->ctrl_ane(priv->plat->ioaddr, 0);

	return 0;
}

/**
 *  stmmac_xmit: Tx entry point of the driver
 *  @skb : the socket buffer
 *  @dev : device pointer
 *  Description : this is the tx entry point of the driver.
 *  It programs the chain or the ring and supports oversized frames
 *  and SG feature.
 */
err_t stmmac_xmit(struct stmmac_priv *priv, struct pbuf *p)
{
	unsigned long flags;
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry;
	struct dma_desc *desc, *first;
	int framelen = 0;
	struct pbuf *q = NULL;

	spin_lock_irqsave(&priv->tx_lock, flags);

	if (unlikely(stmmac_tx_avail(priv) < 1)) {
		spin_unlock_irqrestore(&priv->tx_lock, flags);
		printf("%s: No more free tx buf\n", __func__);
		return ERR_USE;
	}

	entry = priv->cur_tx % txsize;

	if (priv->extend_desc)
		desc = (struct dma_desc *)(priv->dma_etx + entry);
	else
		desc = priv->dma_tx + entry;

	first = desc;

	/* Copy user data to the transmit buffer */
	for (q = p; q != NULL; q = q->next)
	{
		memcpy(priv->tx_skbuff[entry] + framelen, q->payload, q->len);
		framelen += q->len;

		/* Check the frame length */
		if (framelen > priv->dma_buf_sz - 1)
		{
			spin_unlock_irqrestore(&priv->tx_lock, flags);
			printf("%s: tx buffer frame length over : %d", __func__, framelen);
			return ERR_USE;
		}
	}

	flush_dcache((unsigned long)priv->tx_skbuff[entry], ALIGN(framelen, cache_line_size()));
	priv->tx_skbuff_dma[entry] = CPHYSADDR(priv->tx_skbuff[entry]);
	desc->des2 = priv->tx_skbuff_dma[entry];
	priv->hw.desc->prepare_tx_desc(desc, 1, framelen, 0, priv->mode);

	/* Finalize the latest segment. */
	priv->hw.desc->close_tx_desc(desc);

	/* To avoid raise condition */
	priv->hw.desc->set_tx_owner(first);

	priv->cur_tx++;

	priv->stats.tx_bytes += framelen;

	priv->hw.dma->enable_dma_transmission(priv->plat->ioaddr);

	spin_unlock_irqrestore(&priv->tx_lock, flags);

	return ERR_OK;
}

/**
 * stmmac_rx_refill: refill used skb preallocated buffers
 * @priv: driver private structure
 * Description : this is to reallocate the skb for the reception process
 */
static inline void stmmac_rx_refill(struct stmmac_priv *priv)
{
	unsigned int rxsize = priv->dma_rx_size;

	for (; priv->cur_rx - priv->dirty_rx > 0; priv->dirty_rx++) {
		unsigned int entry = priv->dirty_rx % rxsize;
		struct dma_desc *p;

		if (priv->extend_desc)
			p = (struct dma_desc *)(priv->dma_erx + entry);
		else
			p = priv->dma_rx + entry;
		if (priv->rx_skbuff_dma[entry] == 0) {
			flush_dcache((unsigned long)priv->rx_skbuff[entry], priv->dma_buf_sz);
			priv->rx_skbuff_dma[entry] = CPHYSADDR(priv->rx_skbuff[entry]);
			p->des2 = priv->rx_skbuff_dma[entry];
			priv->hw.mode->refill_desc3(priv, p);
		}
		priv->hw.desc->set_rx_owner(p);
	}
}

/**
 * stmmac_rx_refill: refill used skb preallocated buffers
 * @priv: driver private structure
 * Description :  this the function called by the napi poll method.
 * It gets all the frames inside the ring.
 */
static int stmmac_rx(struct stmmac_priv *priv)
{
	unsigned int rxsize = priv->dma_rx_size;
	unsigned int next_entry = priv->cur_rx % rxsize;
	unsigned int count = 0;
	int coe = priv->plat->rx_coe;

	while (count < 64) {
		int status, entry;
		struct dma_desc *p;

		entry = next_entry;

		if (priv->extend_desc)
			p = (struct dma_desc *)(priv->dma_erx + entry);
		else
			p = priv->dma_rx + entry;

		if (priv->hw.desc->get_rx_owner(p))
			break;

		count++;

		next_entry = (++priv->cur_rx) % rxsize;

		/* read the status of the incoming frame */
		status = priv->hw.desc->rx_status(&priv->stats,
						   &priv->xstats, p);
		if ((priv->extend_desc) && (priv->hw.desc->rx_extended_status))
			priv->hw.desc->rx_extended_status(&priv->stats,
							   &priv->xstats,
							   priv->dma_erx +
							   entry);
		if (unlikely(status == discard_frame)) {
			priv->stats.rx_errors++;
			if (priv->hwts_rx_en && !priv->extend_desc)
				priv->rx_skbuff_dma[entry] = 0;
		} else {
			int frame_len;
			int buf_len = 0;
			struct pbuf *p_buf = NULL, *q_buf = NULL;

			frame_len = priv->hw.desc->get_rx_frame_len(p, coe);

			/*  check if frame_len fits the preallocated memory */
			if (frame_len > priv->dma_buf_sz) {
				priv->stats.rx_length_errors++;
				continue;
			}

			/* ACS is set; GMAC core strips PAD/FCS for IEEE 802.3
			 * Type frames (LLC/LLC-SNAP)
			 */
			if (unlikely(status != llc_snap))
				frame_len -= ETH_FCS_LEN;

			priv->rx_skbuff_dma[entry] = 0;
			invalidate_dcache((unsigned long)priv->rx_skbuff[entry], priv->dma_buf_sz);

			p_buf = pbuf_alloc(PBUF_RAW, frame_len, PBUF_RAM);
			if (!p_buf) {
				priv->stats.rx_dropped++;
				printf("%s: pbuf_alloc fail\n", __func__);
				continue;
			}

			for (q_buf = p_buf; q_buf != NULL; q_buf = q_buf->next)
			{
				memcpy(q_buf->payload, priv->rx_skbuff[entry] + buf_len, q_buf->len);
				buf_len += q_buf->len;

				if (buf_len > frame_len)
				{
					printf("frame len is too long!");
					break;
				}
			}

			if(priv->netif.input(p_buf, &priv->netif) != ERR_OK) {
				printf("ethernetif_input: Input error\n");
				pbuf_free(p_buf);
			}

			priv->stats.rx_packets++;
			priv->stats.rx_bytes += frame_len;
		}
	}

	stmmac_rx_refill(priv);

	priv->xstats.rx_pkt_n += count;
	return count;
}

/**
 * stmmac_dma_interrupt: DMA ISR
 * @priv: driver private structure
 * Description: this is the DMA ISR. It is called by the main ISR.
 * It calls the dwmac dma routine to understand which type of interrupt
 * happened. In case of there is a Normal interrupt and either TX or RX
 * interrupt happened so the NAPI is scheduled.
 */
static void stmmac_dma_interrupt(struct stmmac_priv *priv)
{
	int status;

	status = priv->hw.dma->dma_interrupt(priv->plat->ioaddr, &priv->xstats);
	if (status & handle_rx) {
		stmmac_disable_dma_irq(priv);
		thread_waiter_wakeup(&priv->rx_waiter);
	}

	if (status & handle_tx)
		stmmac_tx_clean(priv);

	if (unlikely(status & tx_hard_error_bump_tc)) {
		/* Try to bump up the dma threshold on this failure */
		if (unlikely(tc != SF_DMA_MODE) && (tc <= 256)) {
			tc += 64;
			priv->hw.dma->dma_mode(priv->plat->ioaddr, tc, SF_DMA_MODE);
			priv->xstats.threshold = tc;
		}
	} else if (unlikely(status == tx_hard_error))
		stmmac_tx_err(priv);
}

/**
 *  stmmac_interrupt - main ISR
 *  @irq: interrupt number.
 *  @dev_id: to pass the net device pointer.
 *  Description: this is the main driver interrupt service routine.
 *  It calls the DMA ISR and also the core ISR to manage PMT, MMC, LPI
 *  interrupts.
 */
static void stmmac_interrupt(int irq, void *dev_id)
{
	struct stmmac_priv *priv = (struct stmmac_priv *)dev_id;

	/* To handle GMAC own interrupts */
	priv->hw.mac->host_irq_status(priv->plat->ioaddr, &priv->xstats);

	/* To handle DMA interrupts */
	stmmac_dma_interrupt(priv);
}

void mac_rx_thread(void *data)
{
	struct stmmac_priv *priv = data;

	while (priv->rx_running) {
		thread_waiter_wait(&priv->rx_waiter);
		if (priv->rx_running) {
			stmmac_rx(priv);
			stmmac_enable_dma_irq(priv);
		}
	}

	thread_waiter_wakeup(&priv->rx_exit_waiter);
}

/**
 *  stmmac_open - open entry point of the driver
 *  @dev : pointer to the device structure.
 *  Description:
 *  This function is the open entry point of the driver.
 *  Return value:
 *  0 on success and an appropriate (-)ve integer as defined in errno.h
 *  file on failure.
 */
int stmmac_open(struct stmmac_priv *priv)
{
	int ret;

	/* Extra statistics */
	memset(&priv->xstats, 0, sizeof(struct stmmac_extra_stats));
	priv->xstats.threshold = tc;

	/* Create and initialize the TX/RX descriptors chains. */
	priv->dma_tx_size = ALIGN(dma_txsize, cache_line_size());
	priv->dma_rx_size = ALIGN(dma_rxsize, cache_line_size());
	priv->dma_buf_sz = ALIGN(buf_sz, cache_line_size());

	ret = alloc_dma_desc_resources(priv);
	if (ret < 0) {
		printf("%s: DMA descriptors allocation failed\n", __func__);
		return ret;
	}

	ret = stmmac_hw_setup(priv);
	if (ret < 0) {
		printf("%s: Hw setup failed\n", __func__);
		goto init_error;
	}

	priv->rx_running = 1;
    thread_waiter_init(&priv->rx_waiter);
	thread_waiter_init(&priv->rx_exit_waiter);
    priv->rx_thread = thread_create("rx_thread", 4096, mac_rx_thread, priv);

	/* Request the IRQ lines */
	request_irq(priv->plat->irq, 0, stmmac_interrupt, "mac_irq", priv);

	return 0;

init_error:
	free_dma_desc_resources(priv);
	return ret;
}

void stmmac_release(struct stmmac_priv *priv)
{
	release_irq(priv->plat->irq);

	priv->rx_running = 0;
	thread_waiter_wakeup(&priv->rx_waiter);
	thread_waiter_wait(&priv->rx_exit_waiter);

	priv->hw.dma->stop_rx(priv->plat->ioaddr);
	priv->hw.dma->stop_tx(priv->plat->ioaddr);

	free_dma_desc_resources(priv);

	stmmac_set_mac(priv->plat->ioaddr, 0);
}


/**
 *  stmmac_hw_init - Init the MAC device
 *  @priv: driver private structure
 *  Description: this function detects which MAC device
 *  (GMAC/MAC10-100) has to attached, checks the HW capability
 *  (if supported) and sets the driver's features (for example
 *  to use the ring or chaine mode or support the normal/enh
 *  descriptor structure).
 */
static void stmmac_hw_init(struct stmmac_priv *priv)
{
	int ret;

	/* Identify the MAC HW device */
	dwmac1000_setup(priv);

	/* Get and dump the chip ID */
	priv->synopsys_id = stmmac_get_synopsys_id(priv);

	/* To use the chained or ring mode */
	if (chain_mode) {
		priv->hw.mode = &chain_mode_ops;
		printf(" Chain mode enabled\n");
		priv->mode = STMMAC_CHAIN_MODE;
	} else {
		priv->hw.mode = &ring_mode_ops;
		printf(" Ring mode enabled\n");
		priv->mode = STMMAC_RING_MODE;
	}

	/* Get the HW capability (new GMAC newer than 3.50a) */
	priv->hw_cap_support = stmmac_get_hw_features(priv);
	if (priv->hw_cap_support) {
		printf(" DMA HW capability register supported");

		/* We can override some gmac/dma configuration fields: e.g.
		 * enh_desc, tx_coe (e.g. that are passed through the
		 * platform) with the values from the HW capability
		 * register (if supported).
		 */
		priv->plat->enh_desc = priv->dma_cap.enh_desc;
		priv->plat->pmt = priv->dma_cap.pmt_remote_wake_up;

		priv->plat->tx_coe = priv->dma_cap.tx_coe;

		if (priv->dma_cap.rx_coe_type2)
			priv->plat->rx_coe = STMMAC_RX_COE_TYPE2;
		else if (priv->dma_cap.rx_coe_type1)
			priv->plat->rx_coe = STMMAC_RX_COE_TYPE1;

	} else
		printf(" No HW DMA feature register supported");

	/* To use alternate (extended) or normal descriptor structures */
	stmmac_selec_desc_mode(priv);

	ret = priv->hw.mac->rx_ipc(priv->plat->ioaddr);
	if (!ret) {
		printf(" RX IPC Checksum Offload not configured.\n");
		priv->plat->rx_coe = STMMAC_RX_COE_NONE;
	}

	if (priv->plat->rx_coe)
		printf(" RX Checksum Offload Engine supported (type %d)\n", priv->plat->rx_coe);
	if (priv->plat->tx_coe)
		printf(" TX Checksum insertion supported\n");
}

/**
 * stmmac_dvr_probe
 * @device: device pointer
 * @plat_dat: platform data pointer
 * @addr: iobase memory address
 * Description: this is the main probe function used to
 * call the alloc_etherdev, allocate the priv structure.
 */
struct stmmac_priv *stmmac_dvr_probe(struct plat_stmmacenet_data *plat_dat)
{
	int ret = 0;
	struct stmmac_priv *priv;

	priv = malloc(sizeof(struct stmmac_priv));
	if (!priv)
		return NULL;

	memset(priv, 0, sizeof(struct stmmac_priv));

	priv->pause = pause_time;
	priv->plat = plat_dat;

	/* Verify driver arguments */
	stmmac_verify_args();

	/* Init MAC and get the capabilities */
	stmmac_hw_init(priv);

	if (flow_ctrl)
		priv->flow_ctrl = FLOW_AUTO;	/* RX/TX pause on */

	/* Rx Watchdog is available in the COREs newer than the 3.40.
	 * In some case, for example on bugged HW this feature
	 * has to be disable and this can be done by passing the
	 * riwt_off field from the platform.
	 */
	if ((priv->synopsys_id >= DWMAC_CORE_3_50) && (!priv->plat->riwt_off)) {
		priv->use_riwt = 1;
		printf(" Enable RX Mitigation via HW Watchdog Timer\n");
	}

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->tx_lock);

	stmmac_check_pcs_mode(priv);

	/* scan mac phy */
	ret = stmmac_mdio_scan(priv);
	if (ret)
		goto error_mdio_scan;

	return priv;

error_mdio_scan:
	free(priv);
	return NULL;
}

/**
 * stmmac_dvr_remove
 * @ndev: net device pointer
 * Description: this function resets the TX/RX processes, disables the MAC RX/TX
 * changes the link status, releases the DMA descriptor rings.
 */
void stmmac_dvr_remove(struct stmmac_priv *priv)
{
	free(priv);
}
