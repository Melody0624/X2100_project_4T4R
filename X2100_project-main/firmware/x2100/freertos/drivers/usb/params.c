#include "core.h"

static void dwc2_set_params(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *p = &hsotg->params;
	const struct user_config_info *config_info = &hsotg->usb_info->config_info;

	p->ahbcfg = GAHBCFG_HBSTLEN_INCR16 << GAHBCFG_HBSTLEN_SHIFT;

	p->otg_caps.otg_rev = 0x200;

	p->host_rx_fifo_size = 1528;
	p->host_nperio_tx_fifo_size = 1024;
	p->host_perio_tx_fifo_size = 1024;

	p->speed = config_info->speed;
	p->disable_vbus = config_info->disable_vbus;
}

static void dwc2_set_param_otg_cap(struct dwc2_hsotg *hsotg)
{
	switch (hsotg->hw_params.op_mode) {
	case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		hsotg->params.otg_caps.hnp_support = true;
		hsotg->params.otg_caps.srp_support = true;
		break;
	case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
		hsotg->params.otg_caps.hnp_support = false;
		hsotg->params.otg_caps.srp_support = true;
		break;
	default:
		hsotg->params.otg_caps.hnp_support = false;
		hsotg->params.otg_caps.srp_support = false;
		break;
	}
}

static void dwc2_set_param_phy_type(struct dwc2_hsotg *hsotg)
{
	int val;
	u32 hs_phy_type = hsotg->hw_params.hs_phy_type;

	val = DWC2_PHY_TYPE_PARAM_FS;
	if (hs_phy_type != GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED) {
		if (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
		    hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI)
			val = DWC2_PHY_TYPE_PARAM_UTMI;
		else
			val = DWC2_PHY_TYPE_PARAM_ULPI;
	}

	if (dwc2_is_fs_iot(hsotg))
		hsotg->params.phy_type = DWC2_PHY_TYPE_PARAM_FS;

	hsotg->params.phy_type = val;
}

static void dwc2_set_param_speed(struct dwc2_hsotg *hsotg)
{
	int val;

	val = hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS ?
		DWC2_SPEED_PARAM_FULL : DWC2_SPEED_PARAM_HIGH;

	if (dwc2_is_fs_iot(hsotg))
		val = DWC2_SPEED_PARAM_FULL;

	if (dwc2_is_hs_iot(hsotg))
		val = DWC2_SPEED_PARAM_HIGH;

	hsotg->params.speed = val;
}

static void dwc2_set_param_phy_utmi_width(struct dwc2_hsotg *hsotg)
{
	int val;

	val = (hsotg->hw_params.utmi_phy_data_width ==
	       GHWCFG4_UTMI_PHY_DATA_WIDTH_8) ? 8 : 16;

	hsotg->params.phy_utmi_width = val;
}

static void dwc2_set_param_tx_fifo_sizes(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *p = &hsotg->params;
	int fifo_count;

	fifo_count = dwc2_hsotg_tx_fifo_count(hsotg);
	memset(p->g_tx_fifo_size, 0, sizeof(p->g_tx_fifo_size));

#if 0
	p->g_rx_fifo_size = 1016;
	p->g_np_tx_fifo_size = 512;

	int depth_average = dwc2_hsotg_tx_fifo_average_depth(hsotg);
	for (int i = 1; i <= fifo_count; i++)
		p->g_tx_fifo_size[i] = depth_average;
#else
	p->g_rx_fifo_size = 1096;
	p->g_np_tx_fifo_size = 512;

	assert(fifo_count == 8);
	p->g_tx_fifo_size[1] = 16;
	p->g_tx_fifo_size[2] = 16;
	p->g_tx_fifo_size[3] = 16;
	p->g_tx_fifo_size[4] = 128;
	p->g_tx_fifo_size[5] = 256;
	p->g_tx_fifo_size[6] = 256;
	p->g_tx_fifo_size[7] = 512;
	p->g_tx_fifo_size[8] = 768;
#endif
}

static void dwc2_set_param_power_down(struct dwc2_hsotg *hsotg)
{
	int val;

	if (hsotg->hw_params.hibernation)
		val = DWC2_POWER_DOWN_PARAM_HIBERNATION;
	else if (hsotg->hw_params.power_optimized)
		val = DWC2_POWER_DOWN_PARAM_PARTIAL;
	else
		val = DWC2_POWER_DOWN_PARAM_NONE;

	hsotg->params.power_down = val;
}

/**
 * dwc2_set_default_params() - Set all core parameters to their
 * auto-detected default values.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 */
static void dwc2_set_default_params(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	struct dwc2_core_params *p = &hsotg->params;
	bool dma_capable = !(hw->arch == GHWCFG2_SLAVE_ONLY_ARCH);

	dwc2_set_param_otg_cap(hsotg);
	dwc2_set_param_phy_type(hsotg);
	dwc2_set_param_speed(hsotg);
	dwc2_set_param_phy_utmi_width(hsotg);
	dwc2_set_param_power_down(hsotg);
	p->phy_ulpi_ddr = false;
	p->phy_ulpi_ext_vbus = false;

	p->enable_dynamic_fifo = hw->enable_dynamic_fifo;
	p->en_multiple_tx_fifo = hw->en_multiple_tx_fifo;
	p->ulpi_fs_ls = false;
	p->ts_dline = false;
	p->reload_ctl = (hw->snpsid >= DWC2_CORE_REV_2_92a);
	p->uframe_sched = true;
	p->external_id_pin_ctl = false;
	p->disable_vbus = false;
	p->ipg_isoc_en = false;
	p->service_interval = false;
	p->max_packet_count = hw->max_packet_count;
	p->max_transfer_size = hw->max_transfer_size;
	p->ahbcfg = GAHBCFG_HBSTLEN_INCR << GAHBCFG_HBSTLEN_SHIFT;
	p->ref_clk_per = 33333;
	p->sof_cnt_wkup_alert = 100;

	if ((hsotg->dr_mode == USB_DR_MODE_HOST) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		p->host_dma = dma_capable;
		p->host_channels = hw->host_channels;
		p->host_rx_fifo_size = hw->rx_fifo_size;
		p->host_nperio_tx_fifo_size = hw->host_nperio_tx_fifo_size;
		p->host_perio_tx_fifo_size = hw->host_perio_tx_fifo_size;
	}

	if ((hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		p->g_dma = dma_capable;
		dwc2_set_param_tx_fifo_sizes(hsotg);
	}
}

static void dwc2_check_param_otg_cap(struct dwc2_hsotg *hsotg)
{
	int valid = 1;

	if (hsotg->params.otg_caps.hnp_support && hsotg->params.otg_caps.srp_support) {
		/* check HNP && SRP capable */
		if (hsotg->hw_params.op_mode != GHWCFG2_OP_MODE_HNP_SRP_CAPABLE)
			valid = 0;
	} else if (!hsotg->params.otg_caps.hnp_support) {
		/* check SRP only capable */
		if (hsotg->params.otg_caps.srp_support) {
			switch (hsotg->hw_params.op_mode) {
			case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
			case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
			case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
			case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
				break;
			default:
				valid = 0;
				break;
			}
		}
		/* else: NO HNP && NO SRP capable: always valid */
	} else {
		valid = 0;
	}

	if (!valid)
		dwc2_set_param_otg_cap(hsotg);
}

static void dwc2_check_param_phy_type(struct dwc2_hsotg *hsotg)
{
	int valid = 0;
	u32 hs_phy_type;
	u32 fs_phy_type;

	hs_phy_type = hsotg->hw_params.hs_phy_type;
	fs_phy_type = hsotg->hw_params.fs_phy_type;

	switch (hsotg->params.phy_type) {
	case DWC2_PHY_TYPE_PARAM_FS:
		if (fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
			valid = 1;
		break;
	case DWC2_PHY_TYPE_PARAM_UTMI:
		if ((hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI) ||
		    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
			valid = 1;
		break;
	case DWC2_PHY_TYPE_PARAM_ULPI:
		if ((hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI) ||
		    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
			valid = 1;
		break;
	default:
		break;
	}

	if (!valid)
		dwc2_set_param_phy_type(hsotg);
}

static void dwc2_check_param_speed(struct dwc2_hsotg *hsotg)
{
	int valid = 1;
	int phy_type = hsotg->params.phy_type;
	int speed = hsotg->params.speed;

	switch (speed) {
	case DWC2_SPEED_PARAM_HIGH:
		if ((hsotg->params.speed == DWC2_SPEED_PARAM_HIGH) &&
		    (phy_type == DWC2_PHY_TYPE_PARAM_FS))
			valid = 0;
		break;
	case DWC2_SPEED_PARAM_FULL:
	case DWC2_SPEED_PARAM_LOW:
		break;
	default:
		valid = 0;
		break;
	}

	if (!valid)
		dwc2_set_param_speed(hsotg);
}

static void dwc2_check_param_phy_utmi_width(struct dwc2_hsotg *hsotg)
{
	int valid = 0;
	int param = hsotg->params.phy_utmi_width;
	int width = hsotg->hw_params.utmi_phy_data_width;

	switch (width) {
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8:
		valid = (param == 8);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_16:
		valid = (param == 16);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8_OR_16:
		valid = (param == 8 || param == 16);
		break;
	}

	if (!valid)
		dwc2_set_param_phy_utmi_width(hsotg);
}

static void dwc2_check_param_power_down(struct dwc2_hsotg *hsotg)
{
	int param = hsotg->params.power_down;

	switch (param) {
	case DWC2_POWER_DOWN_PARAM_NONE:
		break;
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		if (hsotg->hw_params.power_optimized)
			break;
		printf("Partial power down isn't supported by HW\n");
		param = DWC2_POWER_DOWN_PARAM_NONE;
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		if (hsotg->hw_params.hibernation)
			break;
		printf("Hibernation isn't supported by HW\n");
		param = DWC2_POWER_DOWN_PARAM_NONE;
		break;
	default:
		printf("%s: Invalid parameter power_down=%d\n", __func__, param);
		param = DWC2_POWER_DOWN_PARAM_NONE;
		break;
	}

	hsotg->params.power_down = param;
}

static void dwc2_check_param_tx_fifo_sizes(struct dwc2_hsotg *hsotg)
{
	int fifo_count;
	int fifo;
	int min;
	u32 total = 0;
	u32 dptxfszn;

	fifo_count = dwc2_hsotg_tx_fifo_count(hsotg);
	min = hsotg->hw_params.en_multiple_tx_fifo ? 16 : 4;

	for (fifo = 1; fifo <= fifo_count; fifo++)
		total += hsotg->params.g_tx_fifo_size[fifo];

	if (total > dwc2_hsotg_tx_fifo_total_depth(hsotg) || !total) {
		printf("%s: Invalid parameter g-tx-fifo-size, setting to default average\n", __func__);
		dwc2_set_param_tx_fifo_sizes(hsotg);
	}

	for (fifo = 1; fifo <= fifo_count; fifo++) {
		dptxfszn = hsotg->hw_params.g_tx_fifo_size[fifo];

		if (hsotg->params.g_tx_fifo_size[fifo] < min ||
		    hsotg->params.g_tx_fifo_size[fifo] >  dptxfszn) {
			printf("%s: Invalid parameter g_tx_fifo_size[%d]=%d\n",
				 __func__, fifo, hsotg->params.g_tx_fifo_size[fifo]);
			hsotg->params.g_tx_fifo_size[fifo] = dptxfszn;
		}
	}
}

#define CHECK_RANGE(_param, _min, _max, _def) do {			\
		if ((int)(hsotg->params._param) < (_min) ||		\
		    (hsotg->params._param) > (_max)) {			\
			panic("%s: Invalid parameter %s=%d\n", \
				 __func__, #_param, hsotg->params._param); \
			hsotg->params._param = (_def);			\
		}							\
	} while (0)

#define CHECK_BOOL(_param, _check) do {					\
		if (hsotg->params._param && !(_check)) {		\
			panic("%s: Invalid parameter %s=%d\n", \
				 __func__, #_param, hsotg->params._param); \
			hsotg->params._param = false;			\
		}							\
	} while (0)

static void dwc2_check_params(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	bool dma_capable = !(hw->arch == GHWCFG2_SLAVE_ONLY_ARCH);

	dwc2_check_param_otg_cap(hsotg);
	dwc2_check_param_phy_type(hsotg);
	dwc2_check_param_speed(hsotg);
	dwc2_check_param_phy_utmi_width(hsotg);
	dwc2_check_param_power_down(hsotg);
	CHECK_BOOL(enable_dynamic_fifo, hw->enable_dynamic_fifo);
	CHECK_BOOL(en_multiple_tx_fifo, hw->en_multiple_tx_fifo);
	CHECK_BOOL(ipg_isoc_en, hw->ipg_isoc_en);
	CHECK_BOOL(reload_ctl, (hsotg->hw_params.snpsid > DWC2_CORE_REV_2_92a));
	CHECK_BOOL(service_interval, hw->service_interval_mode);
	CHECK_RANGE(max_packet_count,
		    15, hw->max_packet_count,
		    hw->max_packet_count);
	CHECK_RANGE(max_transfer_size,
		    2047, hw->max_transfer_size,
		    hw->max_transfer_size);

	if ((hsotg->dr_mode == USB_DR_MODE_HOST) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		CHECK_BOOL(host_dma, dma_capable);
		CHECK_RANGE(host_channels,
			    1, hw->host_channels,
			    hw->host_channels);
		CHECK_RANGE(host_rx_fifo_size,
			    16, hw->rx_fifo_size,
			    hw->rx_fifo_size);
		CHECK_RANGE(host_nperio_tx_fifo_size,
			    16, hw->host_nperio_tx_fifo_size,
			    hw->host_nperio_tx_fifo_size);
		CHECK_RANGE(host_perio_tx_fifo_size,
			    16, hw->host_perio_tx_fifo_size,
			    hw->host_perio_tx_fifo_size);
	}

	if ((hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		CHECK_BOOL(g_dma, dma_capable);
		CHECK_RANGE(g_rx_fifo_size,
			    16, hw->rx_fifo_size,
			    hw->rx_fifo_size);
		CHECK_RANGE(g_np_tx_fifo_size,
			    16, hw->dev_nperio_tx_fifo_size,
			    hw->dev_nperio_tx_fifo_size);
		dwc2_check_param_tx_fifo_sizes(hsotg);
	}
}

/*
 * Gets host hardware parameters. Forces host mode if not currently in
 * host mode. Should be called immediately after a core soft reset in
 * order to get the reset values.
 */
static void dwc2_get_host_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	u32 gnptxfsiz;
	u32 hptxfsiz;

	if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
		return;

	dwc2_force_mode(hsotg, true);

	gnptxfsiz = dwc2_readl(hsotg, GNPTXFSIZ);
	hptxfsiz = dwc2_readl(hsotg, HPTXFSIZ);

	hw->host_nperio_tx_fifo_size = (gnptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				       FIFOSIZE_DEPTH_SHIFT;
	hw->host_perio_tx_fifo_size = (hptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				      FIFOSIZE_DEPTH_SHIFT;
}

/*
 * Gets device hardware parameters. Forces device mode if not
 * currently in device mode. Should be called immediately after a core
 * soft reset in order to get the reset values.
 */
static void dwc2_get_dev_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	u32 gnptxfsiz;
	int fifo, fifo_count;

	if (hsotg->dr_mode == USB_DR_MODE_HOST)
		return;

	dwc2_force_mode(hsotg, false);

	gnptxfsiz = dwc2_readl(hsotg, GNPTXFSIZ);

	fifo_count = dwc2_hsotg_tx_fifo_count(hsotg);

	for (fifo = 1; fifo <= fifo_count; fifo++) {
		hw->g_tx_fifo_size[fifo] = (dwc2_readl(hsotg, DPTXFSIZN(fifo)) &
			 FIFOSIZE_DEPTH_MASK) >> FIFOSIZE_DEPTH_SHIFT;
	}

	hw->dev_nperio_tx_fifo_size = (gnptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				       FIFOSIZE_DEPTH_SHIFT;
}

/**
 * During device initialization, read various hardware configuration
 * registers and interpret the contents.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 */
void dwc2_get_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	unsigned int width;
	u32 hwcfg1, hwcfg2, hwcfg3, hwcfg4;
	u32 grxfsiz;

	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the GSNPSID register contents. The value should be
	 * 0x45f4xxxx, 0x5531xxxx or 0x5532xxxx
	 */

	hw->snpsid = dwc2_readl(hsotg, GSNPSID);
	if ((hw->snpsid & GSNPSID_ID_MASK) != DWC2_OTG_ID &&
	    (hw->snpsid & GSNPSID_ID_MASK) != DWC2_FS_IOT_ID &&
	    (hw->snpsid & GSNPSID_ID_MASK) != DWC2_HS_IOT_ID) {
		panic("Bad value for GSNPSID: 0x%08x\n", hw->snpsid);
	}

	printf("USB Core Release: %1x.%1x%1x%1x (snpsid=%x)\n",
		hw->snpsid >> 12 & 0xf, hw->snpsid >> 8 & 0xf,
		hw->snpsid >> 4 & 0xf, hw->snpsid & 0xf, hw->snpsid);

	hwcfg1 = dwc2_readl(hsotg, GHWCFG1);
	hwcfg2 = dwc2_readl(hsotg, GHWCFG2);
	hwcfg3 = dwc2_readl(hsotg, GHWCFG3);
	hwcfg4 = dwc2_readl(hsotg, GHWCFG4);
	grxfsiz = dwc2_readl(hsotg, GRXFSIZ);

	/* hwcfg1 */
	hw->dev_ep_dirs = hwcfg1;

	/* hwcfg2 */
	hw->op_mode = (hwcfg2 & GHWCFG2_OP_MODE_MASK) >>
		      GHWCFG2_OP_MODE_SHIFT;
	hw->arch = (hwcfg2 & GHWCFG2_ARCHITECTURE_MASK) >>
		   GHWCFG2_ARCHITECTURE_SHIFT;
	hw->enable_dynamic_fifo = !!(hwcfg2 & GHWCFG2_DYNAMIC_FIFO);
	hw->host_channels = 1 + ((hwcfg2 & GHWCFG2_NUM_HOST_CHAN_MASK) >>
				GHWCFG2_NUM_HOST_CHAN_SHIFT);
	hw->hs_phy_type = (hwcfg2 & GHWCFG2_HS_PHY_TYPE_MASK) >>
			  GHWCFG2_HS_PHY_TYPE_SHIFT;
	hw->fs_phy_type = (hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK) >>
			  GHWCFG2_FS_PHY_TYPE_SHIFT;
	hw->num_dev_ep = (hwcfg2 & GHWCFG2_NUM_DEV_EP_MASK) >>
			 GHWCFG2_NUM_DEV_EP_SHIFT;
	hw->nperio_tx_q_depth =
		(hwcfg2 & GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK) >>
		GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT << 1;
	hw->host_perio_tx_q_depth =
		(hwcfg2 & GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK) >>
		GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT << 1;
	hw->dev_token_q_depth =
		(hwcfg2 & GHWCFG2_DEV_TOKEN_Q_DEPTH_MASK) >>
		GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT;

	/* hwcfg3 */
	width = (hwcfg3 & GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK) >>
		GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT;
	hw->max_transfer_size = (1 << (width + 11)) - 1;
	width = (hwcfg3 & GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK) >>
		GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT;
	hw->max_packet_count = (1 << (width + 4)) - 1;
	hw->total_fifo_size = (hwcfg3 & GHWCFG3_DFIFO_DEPTH_MASK) >>
			      GHWCFG3_DFIFO_DEPTH_SHIFT;
	hw->lpm_mode = !!(hwcfg3 & GHWCFG3_OTG_LPM_EN);

	/* hwcfg4 */
	hw->en_multiple_tx_fifo = !!(hwcfg4 & GHWCFG4_DED_FIFO_EN);
	hw->num_dev_perio_in_ep = (hwcfg4 & GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK) >>
				  GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT;
	hw->num_dev_in_eps = (hwcfg4 & GHWCFG4_NUM_IN_EPS_MASK) >>
			     GHWCFG4_NUM_IN_EPS_SHIFT;
	hw->dma_desc_enable = !!(hwcfg4 & GHWCFG4_DESC_DMA);
	hw->power_optimized = !!(hwcfg4 & GHWCFG4_POWER_OPTIMIZ);
	hw->hibernation = !!(hwcfg4 & GHWCFG4_HIBER);
	hw->utmi_phy_data_width = (hwcfg4 & GHWCFG4_UTMI_PHY_DATA_WIDTH_MASK) >>
				  GHWCFG4_UTMI_PHY_DATA_WIDTH_SHIFT;
	hw->ipg_isoc_en = !!(hwcfg4 & GHWCFG4_IPG_ISOC_SUPPORTED);
	hw->service_interval_mode = !!(hwcfg4 & GHWCFG4_SERVICE_INTERVAL_SUPPORTED);

	/* fifo sizes */
	hw->rx_fifo_size = (grxfsiz & GRXFSIZ_DEPTH_MASK) >> GRXFSIZ_DEPTH_SHIFT;
	/*
	 * Host specific hardware parameters. Reading these parameters
	 * requires the controller to be in host mode. The mode will
	 * be forced, if necessary, to read these values.
	 */
	dwc2_get_host_hwparams(hsotg);
	dwc2_get_dev_hwparams(hsotg);
}

void dwc2_init_params(struct dwc2_hsotg *hsotg)
{
	dwc2_set_default_params(hsotg);

	dwc2_set_params(hsotg);

	dwc2_check_params(hsotg);

	if (hsotg->params.disable_vbus) {
		u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);
		gotgctl |= GOTGCTL_VBVALOEN | GOTGCTL_VBVALOVAL;

		switch (hsotg->usb_info->config_info.dr_mode) {
		case USB_DR_MODE_PERIPHERAL: {
			gotgctl |= GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;
			gotgctl |= GOTGCTL_AVALOEN;
			gotgctl &= ~GOTGCTL_AVALOVAL;
			break;
		}
		case USB_DR_MODE_HOST: {
			gotgctl |= GOTGCTL_AVALOEN | GOTGCTL_AVALOVAL;
			gotgctl |= GOTGCTL_BVALOEN;
			gotgctl &= ~GOTGCTL_BVALOVAL;
			break;
		}
		default: {
			gotgctl |= GOTGCTL_AVALOEN | GOTGCTL_AVALOVAL;
			gotgctl |= GOTGCTL_BVALOEN | GOTGCTL_BVALOVAL;
			break;
		}
		}

		dwc2_writel(hsotg, gotgctl, GOTGCTL);
	}

}
