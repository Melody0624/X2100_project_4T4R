#include "usb.h"

#include "host/hcd.h"

#include "core.h"
#include "hcd.h"
#include <errno.h>

#include <driver/gpio_pin.h>

void dwc2_vbus_supply_init(struct dwc2_hsotg *hsotg)
{
	if (!hsotg->vbus_supply_enabled) {
		hsotg->vbus_supply_enabled = 1;

		gpio_pin_output_enable(hsotg->usb_info->config_info.vbus_power);
	}
}

void dwc2_vbus_supply_exit(struct dwc2_hsotg *hsotg)
{
	if (hsotg->vbus_supply_enabled) {
		hsotg->vbus_supply_enabled = 0;

		gpio_pin_output_disable(hsotg->usb_info->config_info.vbus_power);
	}
}

/*
 * =========================================================================
 *  Host Core Layer Functions
 * =========================================================================
 */

/**
 * dwc2_enable_common_interrupts() - Initializes the commmon interrupts,
 * used in both device and host modes
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
static void dwc2_enable_common_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk;

	/* Clear any pending OTG Interrupts */
	dwc2_writel(hsotg, 0xffffffff, GOTGINT);

	/* Clear any pending interrupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Enable the interrupts in the GINTMSK */
	intmsk = GINTSTS_MODEMIS | GINTSTS_OTGINT;

	if (!hsotg->params.host_dma)
		intmsk |= GINTSTS_RXFLVL;
	if (!hsotg->params.external_id_pin_ctl)
		intmsk |= GINTSTS_CONIDSTSCHNG;

	intmsk |= GINTSTS_WKUPINT | GINTSTS_USBSUSP |
		  GINTSTS_SESSREQINT;

	dwc2_writel(hsotg, intmsk, GINTMSK);
}

static int dwc2_gahbcfg_init(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg, GAHBCFG);

	switch (hsotg->hw_params.arch) {
	case GHWCFG2_EXT_DMA_ARCH:
		printf("External DMA Mode not supported\n");
		return -EINVAL;

	case GHWCFG2_INT_DMA_ARCH:
		if (hsotg->params.ahbcfg != -1) {
			ahbcfg &= GAHBCFG_CTRL_MASK;
			ahbcfg |= hsotg->params.ahbcfg &
				  ~GAHBCFG_CTRL_MASK;
		}
		break;

	case GHWCFG2_SLAVE_ONLY_ARCH:
	default:
		break;
	}

	if (hsotg->params.host_dma)
		ahbcfg |= GAHBCFG_DMA_EN;

	dwc2_writel(hsotg, ahbcfg, GAHBCFG);

	return 0;
}

static void dwc2_gusbcfg_init(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;

	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	usbcfg &= ~(GUSBCFG_HNPCAP | GUSBCFG_SRPCAP);

	switch (hsotg->hw_params.op_mode) {
	case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		if (hsotg->params.otg_caps.hnp_support &&
		    hsotg->params.otg_caps.srp_support)
			usbcfg |= GUSBCFG_HNPCAP;
		//fallthrough;

	case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
		if (hsotg->params.otg_caps.srp_support)
			usbcfg |= GUSBCFG_SRPCAP;
		break;

	case GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE:
	case GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE:
	case GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST:
	default:
		break;
	}

	dwc2_writel(hsotg, usbcfg, GUSBCFG);
}

/**
 * dwc2_enable_host_interrupts() - Enables the Host mode interrupts
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_enable_host_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk;

	/* Disable all interrupts */
	dwc2_writel(hsotg, 0, GINTMSK);
	dwc2_writel(hsotg, 0, HAINTMSK);

	/* Enable the common interrupts */
	dwc2_enable_common_interrupts(hsotg);

	/* Enable host mode interrupts without disturbing common interrupts */
	intmsk = dwc2_readl(hsotg, GINTMSK);
	intmsk |= GINTSTS_DISCONNINT | GINTSTS_PRTINT | GINTSTS_HCHINT;
	dwc2_writel(hsotg, intmsk, GINTMSK);
}

/**
 * dwc2_disable_host_interrupts() - Disables the Host Mode interrupts
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_disable_host_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 intmsk = dwc2_readl(hsotg, GINTMSK);

	/* Disable host mode interrupts without disturbing common interrupts */
	intmsk &= ~(GINTSTS_SOF | GINTSTS_PRTINT | GINTSTS_HCHINT |
		    GINTSTS_PTXFEMP | GINTSTS_NPTXFEMP | GINTSTS_DISCONNINT);
	dwc2_writel(hsotg, intmsk, GINTMSK);
}

/*
 * dwc2_calculate_dynamic_fifo() - Calculates the default fifo size
 * For system that have a total fifo depth that is smaller than the default
 * RX + TX fifo size.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_calculate_dynamic_fifo(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *params = &hsotg->params;
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	u32 rxfsiz, nptxfsiz, ptxfsiz, total_fifo_size;

	total_fifo_size = hw->total_fifo_size;
	rxfsiz = params->host_rx_fifo_size;
	nptxfsiz = params->host_nperio_tx_fifo_size;
	ptxfsiz = params->host_perio_tx_fifo_size;

	/*
	 * Will use Method 2 defined in the DWC2 spec: minimum FIFO depth
	 * allocation with support for high bandwidth endpoints. Synopsys
	 * defines MPS(Max Packet size) for a periodic EP=1024, and for
	 * non-periodic as 512.
	 */
	if (total_fifo_size < (rxfsiz + nptxfsiz + ptxfsiz)) {
		/*
		 * For Buffer DMA mode/Scatter Gather DMA mode
		 * 2 * ((Largest Packet size / 4) + 1 + 1) + n
		 * with n = number of host channel.
		 * 2 * ((1024/4) + 2) = 516
		 */
		rxfsiz = 516 + hw->host_channels;

		/*
		 * min non-periodic tx fifo depth
		 * 2 * (largest non-periodic USB packet used / 4)
		 * 2 * (512/4) = 256
		 */
		nptxfsiz = 256;

		/*
		 * min periodic tx fifo depth
		 * (largest packet size*MC)/4
		 * (1024 * 3)/4 = 768
		 */
		ptxfsiz = 768;

		params->host_rx_fifo_size = rxfsiz;
		params->host_nperio_tx_fifo_size = nptxfsiz;
		params->host_perio_tx_fifo_size = ptxfsiz;
	}

	/*
	 * If the summation of RX, NPTX and PTX fifo sizes is still
	 * bigger than the total_fifo_size, then we have a problem.
	 *
	 * We won't be able to allocate as many endpoints. Right now,
	 * we're just printing an error message, but ideally this FIFO
	 * allocation algorithm would be improved in the future.
	 *
	 * FIXME improve this FIFO allocation algorithm.
	 */
	if (unlikely(total_fifo_size < (rxfsiz + nptxfsiz + ptxfsiz)))
		panic("ERROR: usb host invalid fifo sizes\n");
}

static void dwc2_config_fifos(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *params = &hsotg->params;
	u32 nptxfsiz, hptxfsiz, dfifocfg, grxfsiz;

	if (!params->enable_dynamic_fifo)
		return;

	dwc2_calculate_dynamic_fifo(hsotg);

	/* Rx FIFO */
	grxfsiz = dwc2_readl(hsotg, GRXFSIZ);
	grxfsiz &= ~GRXFSIZ_DEPTH_MASK;
	grxfsiz |= params->host_rx_fifo_size <<
		   GRXFSIZ_DEPTH_SHIFT & GRXFSIZ_DEPTH_MASK;
	dwc2_writel(hsotg, grxfsiz, GRXFSIZ);

	/* Non-periodic Tx FIFO */
	nptxfsiz = params->host_nperio_tx_fifo_size <<
		   FIFOSIZE_DEPTH_SHIFT & FIFOSIZE_DEPTH_MASK;
	nptxfsiz |= params->host_rx_fifo_size <<
		    FIFOSIZE_STARTADDR_SHIFT & FIFOSIZE_STARTADDR_MASK;
	dwc2_writel(hsotg, nptxfsiz, GNPTXFSIZ);

	/* Periodic Tx FIFO */
	hptxfsiz = params->host_perio_tx_fifo_size <<
		   FIFOSIZE_DEPTH_SHIFT & FIFOSIZE_DEPTH_MASK;
	hptxfsiz |= (params->host_rx_fifo_size +
		     params->host_nperio_tx_fifo_size) <<
		    FIFOSIZE_STARTADDR_SHIFT & FIFOSIZE_STARTADDR_MASK;
	dwc2_writel(hsotg, hptxfsiz, HPTXFSIZ);

	if (hsotg->params.en_multiple_tx_fifo &&
	    hsotg->hw_params.snpsid >= DWC2_CORE_REV_2_91a) {
		/*
		 * This feature was implemented in 2.91a version
		 * Global DFIFOCFG calculation for Host mode -
		 * include RxFIFO, NPTXFIFO and HPTXFIFO
		 */
		dfifocfg = dwc2_readl(hsotg, GDFIFOCFG);
		dfifocfg &= ~GDFIFOCFG_EPINFOBASE_MASK;
		dfifocfg |= (params->host_rx_fifo_size +
			     params->host_nperio_tx_fifo_size +
			     params->host_perio_tx_fifo_size) <<
			    GDFIFOCFG_EPINFOBASE_SHIFT &
			    GDFIFOCFG_EPINFOBASE_MASK;
		dwc2_writel(hsotg, dfifocfg, GDFIFOCFG);
	}
}

/**
 * dwc2_calc_frame_interval() - Calculates the correct frame Interval value for
 * the HFIR register according to PHY type and speed
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: The caller can modify the value of the HFIR register only after the
 * Port Enable bit of the Host Port Control and Status register (HPRT.EnaPort)
 * has been set
 */
u32 dwc2_calc_frame_interval(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;
	u32 hprt0;
	int clock = 60;	/* default value */

	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	hprt0 = dwc2_readl(hsotg, HPRT0);

	if (!(usbcfg & GUSBCFG_PHYSEL) && (usbcfg & GUSBCFG_ULPI_UTMI_SEL) &&
	    !(usbcfg & GUSBCFG_PHYIF16))
		clock = 60;
	if ((usbcfg & GUSBCFG_PHYSEL) && hsotg->hw_params.fs_phy_type ==
	    GHWCFG2_FS_PHY_TYPE_SHARED_ULPI)
		clock = 48;
	if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && (usbcfg & GUSBCFG_PHYIF16))
		clock = 30;
	if (!(usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && !(usbcfg & GUSBCFG_PHYIF16))
		clock = 60;
	if ((usbcfg & GUSBCFG_PHY_LP_CLK_SEL) && !(usbcfg & GUSBCFG_PHYSEL) &&
	    !(usbcfg & GUSBCFG_ULPI_UTMI_SEL) && (usbcfg & GUSBCFG_PHYIF16))
		clock = 48;
	if ((usbcfg & GUSBCFG_PHYSEL) && !(usbcfg & GUSBCFG_PHYIF16) &&
	    hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_SHARED_UTMI)
		clock = 48;
	if ((usbcfg & GUSBCFG_PHYSEL) &&
	    hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
		clock = 48;

	if ((hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT == HPRT0_SPD_HIGH_SPEED)
		/* High speed case */
		return 125 * clock - 1;

	/* FS/LS case */
	return 1000 * clock - 1;
}

/**
 * dwc2_read_packet() - Reads a packet from the Rx FIFO into the destination
 * buffer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @dest:    Destination buffer for the packet
 * @bytes:   Number of bytes to copy to the destination
 */
void dwc2_read_packet(struct dwc2_hsotg *hsotg, u8 *dest, u16 bytes)
{
	u32 *data_buf = (u32 *)dest;
	int word_count = (bytes + 3) / 4;
	int i;

	/*
	 * Todo: Account for the case where dest is not dword aligned. This
	 * requires reading data from the FIFO into a u32 temp buffer, then
	 * moving it into the data buffer.
	 */

	for (i = 0; i < word_count; i++, data_buf++)
		*data_buf = dwc2_readl(hsotg, HCFIFO(0));
}

static int _dwc2_hcd_start(struct usb_hcd *hcd);

static void dwc2_host_start(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);

	_dwc2_hcd_start(hcd);
}

static void dwc2_host_disconnect(struct dwc2_hsotg *hsotg)
{
}

static void dwc2_host_hub_info(struct dwc2_hsotg *hsotg, void *context,
			       int *hub_addr, int *hub_port)
{
	struct urb *urb = context;

	if (urb->dev->tt)
		*hub_addr = urb->dev->tt->hub->devnum;
	else
		*hub_addr = 0;
	*hub_port = urb->dev->ttport;
}

/*
 * =========================================================================
 *  Low Level Host Channel Access Functions
 * =========================================================================
 */

static void dwc2_hc_enable_slave_ints(struct dwc2_hsotg *hsotg,
				      struct dwc2_host_chan *chan)
{
	u32 hcintmsk = HCINTMSK_CHHLTD;

	switch (chan->ep_type) {
	case USB_ENDPOINT_XFER_CONTROL:
	case USB_ENDPOINT_XFER_BULK:
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_STALL;
		hcintmsk |= HCINTMSK_XACTERR;
		hcintmsk |= HCINTMSK_DATATGLERR;
		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_BBLERR;
		} else {
			hcintmsk |= HCINTMSK_NAK;
			hcintmsk |= HCINTMSK_NYET;
			if (chan->do_ping)
				hcintmsk |= HCINTMSK_ACK;
		}

		if (chan->do_split) {
			hcintmsk |= HCINTMSK_NAK;
			if (chan->complete_split)
				hcintmsk |= HCINTMSK_NYET;
			else
				hcintmsk |= HCINTMSK_ACK;
		}

		if (chan->error_state)
			hcintmsk |= HCINTMSK_ACK;
		break;

	case USB_ENDPOINT_XFER_INT:
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_NAK;
		hcintmsk |= HCINTMSK_STALL;
		hcintmsk |= HCINTMSK_XACTERR;
		hcintmsk |= HCINTMSK_DATATGLERR;
		hcintmsk |= HCINTMSK_FRMOVRUN;

		if (chan->ep_is_in)
			hcintmsk |= HCINTMSK_BBLERR;
		if (chan->error_state)
			hcintmsk |= HCINTMSK_ACK;
		if (chan->do_split) {
			if (chan->complete_split)
				hcintmsk |= HCINTMSK_NYET;
			else
				hcintmsk |= HCINTMSK_ACK;
		}
		break;

	case USB_ENDPOINT_XFER_ISOC:
		hcintmsk |= HCINTMSK_XFERCOMPL;
		hcintmsk |= HCINTMSK_FRMOVRUN;
		hcintmsk |= HCINTMSK_ACK;

		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_XACTERR;
			hcintmsk |= HCINTMSK_BBLERR;
		}
		break;
	default:
		printf("## Unknown EP type ##\n");
		break;
	}

	dwc2_writel(hsotg, hcintmsk, HCINTMSK(chan->hc_num));
}

static void dwc2_hc_enable_dma_ints(struct dwc2_hsotg *hsotg,
				    struct dwc2_host_chan *chan)
{
	u32 hcintmsk = HCINTMSK_CHHLTD | HCINTMSK_AHBERR;

	if (chan->error_state && !chan->do_split &&
	    chan->ep_type != USB_ENDPOINT_XFER_ISOC) {
		hcintmsk |= HCINTMSK_ACK;
		if (chan->ep_is_in) {
			hcintmsk |= HCINTMSK_DATATGLERR;
			if (chan->ep_type != USB_ENDPOINT_XFER_INT)
				hcintmsk |= HCINTMSK_NAK;
		}
	}

	dwc2_writel(hsotg, hcintmsk, HCINTMSK(chan->hc_num));
}

static void dwc2_hc_enable_ints(struct dwc2_hsotg *hsotg,
				struct dwc2_host_chan *chan)
{
	u32 intmsk;

	if (hsotg->params.host_dma) {
		dwc2_hc_enable_dma_ints(hsotg, chan);
	} else {
		dwc2_hc_enable_slave_ints(hsotg, chan);
	}

	/* Enable the top level host channel interrupt */
	intmsk = dwc2_readl(hsotg, HAINTMSK);
	intmsk |= 1 << chan->hc_num;
	dwc2_writel(hsotg, intmsk, HAINTMSK);

	/* Make sure host channel interrupts are enabled */
	intmsk = dwc2_readl(hsotg, GINTMSK);
	intmsk |= GINTSTS_HCHINT;
	dwc2_writel(hsotg, intmsk, GINTMSK);
}

/**
 * dwc2_hc_init() - Prepares a host channel for transferring packets to/from
 * a specific endpoint
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * The HCCHARn register is set up with the characteristics specified in chan.
 * Host channel interrupts that may need to be serviced while this transfer is
 * in progress are enabled.
 */
static void dwc2_hc_init(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u8 hc_num = chan->hc_num;
	u32 hcintmsk;
	u32 hcchar;
	u32 hcsplt = 0;

	/* Clear old interrupt conditions for this host channel */
	hcintmsk = 0xffffffff;
	hcintmsk &= ~HCINTMSK_RESERVED14_31;
	dwc2_writel(hsotg, hcintmsk, HCINT(hc_num));

	/* Enable channel interrupts required for this transfer */
	dwc2_hc_enable_ints(hsotg, chan);

	/*
	 * Program the HCCHARn register with the endpoint characteristics for
	 * the current transfer
	 */
	hcchar = chan->dev_addr << HCCHAR_DEVADDR_SHIFT & HCCHAR_DEVADDR_MASK;
	hcchar |= chan->ep_num << HCCHAR_EPNUM_SHIFT & HCCHAR_EPNUM_MASK;
	if (chan->ep_is_in)
		hcchar |= HCCHAR_EPDIR;
	if (chan->speed == USB_SPEED_LOW)
		hcchar |= HCCHAR_LSPDDEV;
	hcchar |= chan->ep_type << HCCHAR_EPTYPE_SHIFT & HCCHAR_EPTYPE_MASK;
	hcchar |= chan->max_packet << HCCHAR_MPS_SHIFT & HCCHAR_MPS_MASK;
	dwc2_writel(hsotg, hcchar, HCCHAR(hc_num));

	/* Program the HCSPLT register for SPLITs */
	if (chan->do_split) {
		if (chan->complete_split)
			hcsplt |= HCSPLT_COMPSPLT;
		hcsplt |= chan->xact_pos << HCSPLT_XACTPOS_SHIFT &
			  HCSPLT_XACTPOS_MASK;
		hcsplt |= chan->hub_addr << HCSPLT_HUBADDR_SHIFT &
			  HCSPLT_HUBADDR_MASK;
		hcsplt |= chan->hub_port << HCSPLT_PRTADDR_SHIFT &
			  HCSPLT_PRTADDR_MASK;
	}

	dwc2_writel(hsotg, hcsplt, HCSPLT(hc_num));
}

/**
 * dwc2_hc_halt() - Attempts to halt a host channel
 *
 * @hsotg:       Controller register interface
 * @chan:        Host channel to halt
 * @halt_status: Reason for halting the channel
 *
 * This function should only be called in Slave mode or to abort a transfer in
 * either Slave mode or DMA mode. Under normal circumstances in DMA mode, the
 * controller halts the channel when the transfer is complete or a condition
 * occurs that requires application intervention.
 *
 * In slave mode, checks for a free request queue entry, then sets the Channel
 * Enable and Channel Disable bits of the Host Channel Characteristics
 * register of the specified channel to intiate the halt. If there is no free
 * request queue entry, sets only the Channel Disable bit of the HCCHARn
 * register to flush requests for this channel. In the latter case, sets a
 * flag to indicate that the host channel needs to be halted when a request
 * queue slot is open.
 *
 * In DMA mode, always sets the Channel Enable and Channel Disable bits of the
 * HCCHARn register. The controller ensures there is space in the request
 * queue before submitting the halt request.
 *
 * Some time may elapse before the core flushes any posted requests for this
 * host channel and halts. The Channel Halted interrupt handler completes the
 * deactivation of the host channel.
 */
void dwc2_hc_halt(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan,
		  enum dwc2_halt_status halt_status)
{
	u32 nptxsts, hptxsts, hcchar;

#if 0
	/*
	 * In buffer DMA or external DMA mode channel can't be halted
	 * for non-split periodic channels. At the end of the next
	 * uframe/frame (in the worst case), the core generates a channel
	 * halted and disables the channel automatically.
	 */
	if (hsotg->params.host_dma || hsotg->hw_params.arch == GHWCFG2_EXT_DMA_ARCH) {
		if (!chan->do_split &&
		    (chan->ep_type == USB_ENDPOINT_XFER_ISOC ||
		     chan->ep_type == USB_ENDPOINT_XFER_INT)) {
			printf("%s() Channel can't be halted\n", __func__);
			return;
		}
	}
#endif

	if (halt_status == DWC2_HC_XFER_NO_HALT_STATUS)
		printf("!!! halt_status = %d !!!\n", halt_status);

	if (halt_status == DWC2_HC_XFER_URB_DEQUEUE ||
	    halt_status == DWC2_HC_XFER_AHB_ERR) {
		/*
		 * Disable all channel interrupts except Ch Halted. The QTD
		 * and QH state associated with this transfer has been cleared
		 * (in the case of URB_DEQUEUE), so the channel needs to be
		 * shut down carefully to prevent crashes.
		 */
		u32 hcintmsk = HCINTMSK_CHHLTD;

		printf("usb host dequeue/error\n");
		dwc2_writel(hsotg, hcintmsk, HCINTMSK(chan->hc_num));
		udelay(100);
		/*
		 * Make sure no other interrupts besides halt are currently
		 * pending. Handling another interrupt could cause a crash due
		 * to the QTD and QH state.
		 */
		dwc2_writel(hsotg, ~hcintmsk, HCINT(chan->hc_num));
		udelay(100);
		/*
		 * Make sure the halt status is set to URB_DEQUEUE or AHB_ERR
		 * even if the channel was already halted for some other
		 * reason
		 */
		chan->halt_status = halt_status;

		hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
		if (!(hcchar & HCCHAR_CHENA)) {
			/*
			 * The channel is either already halted or it hasn't
			 * started yet. In DMA mode, the transfer may halt if
			 * it finishes normally or a condition occurs that
			 * requires driver intervention. Don't want to halt
			 * the channel again. In either Slave or DMA mode,
			 * it's possible that the transfer has been assigned
			 * to a channel, but not started yet when an URB is
			 * dequeued. Don't want to halt a channel that hasn't
			 * started yet.
			 */
			return;
		}
	}
	if (chan->halt_pending) {
		/*
		 * A halt has already been issued for this channel. This might
		 * happen when a transfer is aborted by a higher level in
		 * the stack.
		 */
		printf("*** %s: Channel %d, chan->halt_pending already set ***\n",
			 __func__, chan->hc_num);
		return;
	}

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));

	hcchar |= HCCHAR_CHENA;
	hcchar |= HCCHAR_CHDIS;

	if (!hsotg->params.host_dma) {
		/* Check for space in the request queue to issue the halt */
		if (chan->ep_type == USB_ENDPOINT_XFER_CONTROL ||
		    chan->ep_type == USB_ENDPOINT_XFER_BULK) {
			nptxsts = dwc2_readl(hsotg, GNPTXSTS);
			if ((nptxsts & TXSTS_QSPCAVAIL_MASK) == 0) {
				hcchar &= ~HCCHAR_CHENA;
			}
		} else {
			hptxsts = dwc2_readl(hsotg, HPTXSTS);
			if ((hptxsts & TXSTS_QSPCAVAIL_MASK) == 0 ||
			    hsotg->queuing_high_bandwidth) {
				hcchar &= ~HCCHAR_CHENA;
			}
		}
	}

	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
	chan->halt_status = halt_status;

	if (hcchar & HCCHAR_CHENA) {
		chan->halt_pending = 1;
		chan->halt_on_queue = 0;
	} else {
		chan->halt_on_queue = 1;
	}
}

/**
 * dwc2_hc_cleanup() - Clears the transfer state for a host channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Identifies the host channel to clean up
 *
 * This function is normally called after a transfer is done and the host
 * channel is being released
 */
void dwc2_hc_cleanup(struct dwc2_hsotg *hsotg, struct dwc2_host_chan *chan)
{
	u32 hcintmsk;

	chan->xfer_started = 0;

	list_del_init(&chan->split_order_list_entry);

	/*
	 * Clear channel interrupt enables and any unhandled channel interrupt
	 * conditions
	 */
	dwc2_writel(hsotg, 0, HCINTMSK(chan->hc_num));
	hcintmsk = 0xffffffff;
	hcintmsk &= ~HCINTMSK_RESERVED14_31;
	dwc2_writel(hsotg, hcintmsk, HCINT(chan->hc_num));
}

/**
 * dwc2_hc_set_even_odd_frame() - Sets the channel property that indicates in
 * which frame a periodic transfer should occur
 *
 * @hsotg:  Programming view of DWC_otg controller
 * @chan:   Identifies the host channel to set up and its properties
 * @hcchar: Current value of the HCCHAR register for the specified host channel
 *
 * This function has no effect on non-periodic transfers
 */
static void dwc2_hc_set_even_odd_frame(struct dwc2_hsotg *hsotg,
				       struct dwc2_host_chan *chan, u32 *hcchar)
{
	if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
	    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
		int host_speed;
		int xfer_ns;
		int xfer_us;
		int bytes_in_fifo;
		u16 fifo_space;
		u16 frame_number;
		u16 wire_frame;

		/*
		 * Try to figure out if we're an even or odd frame. If we set
		 * even and the current frame number is even the the transfer
		 * will happen immediately.  Similar if both are odd. If one is
		 * even and the other is odd then the transfer will happen when
		 * the frame number ticks.
		 *
		 * There's a bit of a balancing act to get this right.
		 * Sometimes we may want to send data in the current frame (AK
		 * right away).  We might want to do this if the frame number
		 * _just_ ticked, but we might also want to do this in order
		 * to continue a split transaction that happened late in a
		 * microframe (so we didn't know to queue the next transfer
		 * until the frame number had ticked).  The problem is that we
		 * need a lot of knowledge to know if there's actually still
		 * time to send things or if it would be better to wait until
		 * the next frame.
		 *
		 * We can look at how much time is left in the current frame
		 * and make a guess about whether we'll have time to transfer.
		 * We'll do that.
		 */

		/* Get speed host is running at */
		host_speed = (chan->speed != USB_SPEED_HIGH &&
			      !chan->do_split) ? chan->speed : USB_SPEED_HIGH;

		/* See how many bytes are in the periodic FIFO right now */
		fifo_space = (dwc2_readl(hsotg, HPTXSTS) &
			      TXSTS_FSPCAVAIL_MASK) >> TXSTS_FSPCAVAIL_SHIFT;
		bytes_in_fifo = sizeof(u32) *
				(hsotg->params.host_perio_tx_fifo_size -
				 fifo_space);

		/*
		 * Roughly estimate bus time for everything in the periodic
		 * queue + our new transfer.  This is "rough" because we're
		 * using a function that makes takes into account IN/OUT
		 * and INT/ISO and we're just slamming in one value for all
		 * transfers.  This should be an over-estimate and that should
		 * be OK, but we can probably tighten it.
		 */
		xfer_ns = usb_calc_bus_time(host_speed, false, false,
					    chan->xfer_len + bytes_in_fifo);
		xfer_us = NS_TO_US(xfer_ns);

		/* See what frame number we'll be at by the time we finish */
		frame_number = dwc2_hcd_get_future_frame_number(hsotg, xfer_us);

		/* This is when we were scheduled to be on the wire */
		wire_frame = dwc2_frame_num_inc(chan->qh->next_active_frame, 1);

		/*
		 * If we'd finish _after_ the frame we're scheduled in then
		 * it's hopeless.  Just schedule right away and hope for the
		 * best.  Note that it _might_ be wise to call back into the
		 * scheduler to pick a better frame, but this is better than
		 * nothing.
		 */
		if (dwc2_frame_num_gt(frame_number, wire_frame)) {
			wire_frame = frame_number;

			/*
			 * We picked a different frame number; communicate this
			 * back to the scheduler so it doesn't try to schedule
			 * another in the same frame.
			 *
			 * Remember that next_active_frame is 1 before the wire
			 * frame.
			 */
			chan->qh->next_active_frame =
				dwc2_frame_num_dec(frame_number, 1);
		}

		if (wire_frame & 1)
			*hcchar |= HCCHAR_ODDFRM;
		else
			*hcchar &= ~HCCHAR_ODDFRM;
	}
}

static void dwc2_set_pid_isoc(struct dwc2_host_chan *chan)
{
	/* Set up the initial PID for the transfer */
	if (chan->speed == USB_SPEED_HIGH) {
		if (chan->ep_is_in) {
			if (chan->multi_count == 1)
				chan->data_pid_start = DWC2_HC_PID_DATA0;
			else if (chan->multi_count == 2)
				chan->data_pid_start = DWC2_HC_PID_DATA1;
			else
				chan->data_pid_start = DWC2_HC_PID_DATA2;
		} else {
			if (chan->multi_count == 1)
				chan->data_pid_start = DWC2_HC_PID_DATA0;
			else
				chan->data_pid_start = DWC2_HC_PID_MDATA;
		}
	} else {
		chan->data_pid_start = DWC2_HC_PID_DATA0;
	}
}

/**
 * dwc2_hc_write_packet() - Writes a packet into the Tx FIFO associated with
 * the Host Channel
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * This function should only be called in Slave mode. For a channel associated
 * with a non-periodic EP, the non-periodic Tx FIFO is written. For a channel
 * associated with a periodic EP, the periodic Tx FIFO is written.
 *
 * Upon return the xfer_buf and xfer_count fields in chan are incremented by
 * the number of bytes written to the Tx FIFO.
 */
static void dwc2_hc_write_packet(struct dwc2_hsotg *hsotg,
				 struct dwc2_host_chan *chan)
{
	u32 i;
	u32 remaining_count;
	u32 byte_count;
	u32 dword_count;
	u32 *data_buf = (u32 *)chan->xfer_buf;

	remaining_count = chan->xfer_len - chan->xfer_count;
	if (remaining_count > chan->max_packet)
		byte_count = chan->max_packet;
	else
		byte_count = remaining_count;

	dword_count = (byte_count + 3) / 4;

	if (((unsigned long)data_buf & 0x3) == 0) {
		/* xfer_buf is DWORD aligned */
		for (i = 0; i < dword_count; i++, data_buf++)
			dwc2_writel(hsotg, *data_buf, HCFIFO(chan->hc_num));
	} else {
		/* xfer_buf is not DWORD aligned */
		for (i = 0; i < dword_count; i++, data_buf++) {
			u32 data = data_buf[0] | data_buf[1] << 8 |
				   data_buf[2] << 16 | data_buf[3] << 24;
			dwc2_writel(hsotg, data, HCFIFO(chan->hc_num));
		}
	}

	chan->xfer_count += byte_count;
	chan->xfer_buf += byte_count;
}

/**
 * dwc2_hc_do_ping() - Starts a PING transfer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * This function should only be called in Slave mode. The Do Ping bit is set in
 * the HCTSIZ register, then the channel is enabled.
 */
static void dwc2_hc_do_ping(struct dwc2_hsotg *hsotg,
			    struct dwc2_host_chan *chan)
{
	u32 hcchar;
	u32 hctsiz;

	hctsiz = TSIZ_DOPNG;
	hctsiz |= 1 << TSIZ_PKTCNT_SHIFT;
	dwc2_writel(hsotg, hctsiz, HCTSIZ(chan->hc_num));

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;
	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
}

/**
 * dwc2_hc_start_transfer() - Does the setup for a data transfer for a host
 * channel and starts the transfer
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel. The xfer_len value
 *         may be reduced to accommodate the max widths of the XferSize and
 *         PktCnt fields in the HCTSIZn register. The multi_count value may be
 *         changed to reflect the final xfer_len value.
 *
 * This function may be called in either Slave mode or DMA mode. In Slave mode,
 * the caller must ensure that there is sufficient space in the request queue
 * and Tx Data FIFO.
 *
 * For an OUT transfer in Slave mode, it loads a data packet into the
 * appropriate FIFO. If necessary, additional data packets are loaded in the
 * Host ISR.
 *
 * For an IN transfer in Slave mode, a data packet is requested. The data
 * packets are unloaded from the Rx FIFO in the Host ISR. If necessary,
 * additional data packets are requested in the Host ISR.
 *
 * For a PING transfer in Slave mode, the Do Ping bit is set in the HCTSIZ
 * register along with a packet count of 1 and the channel is enabled. This
 * causes a single PING transaction to occur. Other fields in HCTSIZ are
 * simply set to 0 since no data transfer occurs in this case.
 *
 * For a PING transfer in DMA mode, the HCTSIZ register is initialized with
 * all the information required to perform the subsequent data transfer. In
 * addition, the Do Ping bit is set in the HCTSIZ register. In this case, the
 * controller performs the entire PING protocol, then starts the data
 * transfer.
 */
static void dwc2_hc_start_transfer(struct dwc2_hsotg *hsotg,
				   struct dwc2_host_chan *chan)
{
	u32 max_hc_xfer_size = hsotg->params.max_transfer_size;
	u16 max_hc_pkt_count = hsotg->params.max_packet_count;
	u32 hcchar;
	u32 hctsiz = 0;
	u16 num_packets;
	u32 ec_mc;

	if (chan->do_ping) {
		if (!hsotg->params.host_dma) {
			dwc2_hc_do_ping(hsotg, chan);
			chan->xfer_started = 1;
			return;
		}

		hctsiz |= TSIZ_DOPNG;
	}

	if (chan->do_split) {
		num_packets = 1;

		if (chan->complete_split && !chan->ep_is_in)
			/*
			 * For CSPLIT OUT Transfer, set the size to 0 so the
			 * core doesn't expect any data written to the FIFO
			 */
			chan->xfer_len = 0;
		else if (chan->ep_is_in || chan->xfer_len > chan->max_packet)
			chan->xfer_len = chan->max_packet;
		else if (!chan->ep_is_in && chan->xfer_len > 188)
			chan->xfer_len = 188;

		hctsiz |= chan->xfer_len << TSIZ_XFERSIZE_SHIFT &
			  TSIZ_XFERSIZE_MASK;

		/* For split set ec_mc for immediate retries */
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			ec_mc = 3;
		else
			ec_mc = 1;
	} else {
		/*
		 * Ensure that the transfer length and packet count will fit
		 * in the widths allocated for them in the HCTSIZn register
		 */
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			/*
			 * Make sure the transfer size is no larger than one
			 * (micro)frame's worth of data. (A check was done
			 * when the periodic transfer was accepted to ensure
			 * that a (micro)frame's worth of data can be
			 * programmed into a channel.)
			 */
			u32 max_periodic_len =
				chan->multi_count * chan->max_packet;

			if (chan->xfer_len > max_periodic_len)
				chan->xfer_len = max_periodic_len;
		} else if (chan->xfer_len > max_hc_xfer_size) {
			/*
			 * Make sure that xfer_len is a multiple of max packet
			 * size
			 */
			chan->xfer_len =
				max_hc_xfer_size - chan->max_packet + 1;
		}

		if (chan->xfer_len > 0) {
			num_packets = (chan->xfer_len + chan->max_packet - 1) /
					chan->max_packet;
			if (num_packets > max_hc_pkt_count) {
				num_packets = max_hc_pkt_count;
				chan->xfer_len = num_packets * chan->max_packet;
			} else if (chan->ep_is_in) {
				/*
				 * Always program an integral # of max packets
				 * for IN transfers.
				 * Note: This assumes that the input buffer is
				 * aligned and sized accordingly.
				 */
				chan->xfer_len = num_packets * chan->max_packet;
			}
		} else {
			/* Need 1 packet for transfer length of 0 */
			num_packets = 1;
		}

		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			/*
			 * Make sure that the multi_count field matches the
			 * actual transfer length
			 */
			chan->multi_count = num_packets;

		if (chan->ep_type == USB_ENDPOINT_XFER_ISOC)
			dwc2_set_pid_isoc(chan);

		hctsiz |= chan->xfer_len << TSIZ_XFERSIZE_SHIFT &
			  TSIZ_XFERSIZE_MASK;

		/* The ec_mc gets the multi_count for non-split */
		ec_mc = chan->multi_count;
	}

	chan->start_pkt_count = num_packets;
	hctsiz |= num_packets << TSIZ_PKTCNT_SHIFT & TSIZ_PKTCNT_MASK;
	hctsiz |= chan->data_pid_start << TSIZ_SC_MC_PID_SHIFT &
		  TSIZ_SC_MC_PID_MASK;
	dwc2_writel(hsotg, hctsiz, HCTSIZ(chan->hc_num));

	if (hsotg->params.host_dma) {
		dma_addr_t dma_addr;

		if (chan->align_buf) {
			dma_addr = chan->align_buf;
		} else {
			dma_addr = chan->xfer_dma;
		}
		dwc2_writel(hsotg, (u32)dma_addr, HCDMA(chan->hc_num));
	}

	/* Start the split */
	if (chan->do_split) {
		u32 hcsplt = dwc2_readl(hsotg, HCSPLT(chan->hc_num));

		hcsplt |= HCSPLT_SPLTENA;
		dwc2_writel(hsotg, hcsplt, HCSPLT(chan->hc_num));
	}

	hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));
	hcchar &= ~HCCHAR_MULTICNT_MASK;
	hcchar |= (ec_mc << HCCHAR_MULTICNT_SHIFT) & HCCHAR_MULTICNT_MASK;
	dwc2_hc_set_even_odd_frame(hsotg, chan, &hcchar);

	if (hcchar & HCCHAR_CHDIS)
		printf("%s: chdis set, channel %d, hcchar 0x%08x\n",
			 __func__, chan->hc_num, hcchar);

	/* Set host channel enable after all other setup is complete */
	hcchar |= HCCHAR_CHENA;
	hcchar &= ~HCCHAR_CHDIS;
	dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));

	chan->xfer_started = 1;
	chan->requests++;

	if (!hsotg->params.host_dma &&
	    !chan->ep_is_in && chan->xfer_len > 0)
		/* Load OUT packet into the appropriate Tx FIFO */
		dwc2_hc_write_packet(hsotg, chan);
}

/**
 * dwc2_hc_continue_transfer() - Continues a data transfer that was started by
 * a previous call to dwc2_hc_start_transfer()
 *
 * @hsotg: Programming view of DWC_otg controller
 * @chan:  Information needed to initialize the host channel
 *
 * The caller must ensure there is sufficient space in the request queue and Tx
 * Data FIFO. This function should only be called in Slave mode. In DMA mode,
 * the controller acts autonomously to complete transfers programmed to a host
 * channel.
 *
 * For an OUT transfer, a new data packet is loaded into the appropriate FIFO
 * if there is any data remaining to be queued. For an IN transfer, another
 * data packet is always requested. For the SETUP phase of a control transfer,
 * this function does nothing.
 *
 * Return: 1 if a new request is queued, 0 if no more requests are required
 * for this transfer
 */
static int dwc2_hc_continue_transfer(struct dwc2_hsotg *hsotg,
				     struct dwc2_host_chan *chan)
{
	if (chan->do_split)
		/* SPLITs always queue just once per channel */
		return 0;

	if (chan->data_pid_start == DWC2_HC_PID_SETUP)
		/* SETUPs are queued only once since they can't be NAK'd */
		return 0;

	if (chan->ep_is_in) {
		/*
		 * Always queue another request for other IN transfers. If
		 * back-to-back INs are issued and NAKs are received for both,
		 * the driver may still be processing the first NAK when the
		 * second NAK is received. When the interrupt handler clears
		 * the NAK interrupt for the first NAK, the second NAK will
		 * not be seen. So we can't depend on the NAK interrupt
		 * handler to requeue a NAK'd request. Instead, IN requests
		 * are issued each time this function is called. When the
		 * transfer completes, the extra requests for the channel will
		 * be flushed.
		 */
		u32 hcchar = dwc2_readl(hsotg, HCCHAR(chan->hc_num));

		dwc2_hc_set_even_odd_frame(hsotg, chan, &hcchar);
		hcchar |= HCCHAR_CHENA;
		hcchar &= ~HCCHAR_CHDIS;
		dwc2_writel(hsotg, hcchar, HCCHAR(chan->hc_num));
		chan->requests++;
		return 1;
	}

	/* OUT transfers */

	if (chan->xfer_count < chan->xfer_len) {
		if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
		    chan->ep_type == USB_ENDPOINT_XFER_ISOC) {
			u32 hcchar = dwc2_readl(hsotg,
						HCCHAR(chan->hc_num));

			dwc2_hc_set_even_odd_frame(hsotg, chan,
						   &hcchar);
		}

		/* Load OUT packet into the appropriate Tx FIFO */
		dwc2_hc_write_packet(hsotg, chan);
		chan->requests++;
		return 1;
	}

	return 0;
}

/*
 * =========================================================================
 *  HCD
 * =========================================================================
 */

/*
 * Processes all the URBs in a single list of QHs. Completes them with
 * -ETIMEDOUT and frees the QTD.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_kill_urbs_in_qh_list(struct dwc2_hsotg *hsotg,
				      struct list_head *qh_list)
{
	struct dwc2_qh *qh, *qh_tmp;
	struct dwc2_qtd *qtd, *qtd_tmp;

	list_for_each_entry_safe(qh, qh_tmp, qh_list, qh_list_entry) {
		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry) {
			dwc2_host_complete(hsotg, qtd, -ECONNRESET);
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);
		}
	}
}

static void dwc2_qh_list_free(struct dwc2_hsotg *hsotg,
			      struct list_head *qh_list)
{
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh, *qh_tmp;
	unsigned long flags;

	if (!qh_list->next)
		/* The list hasn't been initialized yet */
		return;

	usb_spin_lock_irqsave(&hsotg->lock, flags);

	/* Ensure there are no QTDs or URBs left */
	dwc2_kill_urbs_in_qh_list(hsotg, qh_list);

	list_for_each_entry_safe(qh, qh_tmp, qh_list, qh_list_entry) {
		dwc2_hcd_qh_unlink(hsotg, qh);

		/* Free each QTD in the QH's QTD list */
		list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list,
					 qtd_list_entry)
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

		if (qh->channel && qh->channel->qh == qh)
			qh->channel->qh = NULL;

		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_hcd_qh_free(hsotg, qh);
		usb_spin_lock_irqsave(&hsotg->lock, flags);
	}

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Responds with an error status of -ETIMEDOUT to all URBs in the non-periodic
 * and periodic schedules. The QTD associated with each URB is removed from
 * the schedule and freed. This function may be called when a disconnect is
 * detected or when the HCD is being stopped.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_kill_all_urbs(struct dwc2_hsotg *hsotg)
{
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_inactive);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_waiting);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->non_periodic_sched_active);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_inactive);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_ready);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_assigned);
	dwc2_kill_urbs_in_qh_list(hsotg, &hsotg->periodic_sched_queued);
}

/**
 * dwc2_hcd_start() - Starts the HCD when switching to Host mode
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 */
void dwc2_hcd_start(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;

	if (hsotg->op_state == OTG_STATE_B_HOST) {
		/*
		 * Reset the port. During a HNP mode switch the reset
		 * needs to occur within 1ms and have a duration of at
		 * least 50ms.
		 */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_RST;
		dwc2_writel(hsotg, hprt0, HPRT0);
	}

	queue_delayed_work(&hsotg->wq_otg, &hsotg->start_work, 50*1000);
}

/* Must be called with interrupt disabled and spinlock held */
static void dwc2_hcd_cleanup_channels(struct dwc2_hsotg *hsotg)
{
	int num_channels = hsotg->params.host_channels;
	struct dwc2_host_chan *channel;
	u32 hcchar;
	int i;

	if (!hsotg->params.host_dma) {
		/* Flush out any channel requests in slave mode */
		for (i = 0; i < num_channels; i++) {
			channel = hsotg->hc_ptr_array[i];
			if (!list_empty(&channel->hc_list_entry))
				continue;
			hcchar = dwc2_readl(hsotg, HCCHAR(i));
			if (hcchar & HCCHAR_CHENA) {
				hcchar &= ~(HCCHAR_CHENA | HCCHAR_EPDIR);
				hcchar |= HCCHAR_CHDIS;
				dwc2_writel(hsotg, hcchar, HCCHAR(i));
			}
		}
	}

	for (i = 0; i < num_channels; i++) {
		channel = hsotg->hc_ptr_array[i];
		if (!list_empty(&channel->hc_list_entry))
			continue;
		hcchar = dwc2_readl(hsotg, HCCHAR(i));
		if (hcchar & HCCHAR_CHENA) {
			/* Halt the channel */
			hcchar |= HCCHAR_CHDIS;
			dwc2_writel(hsotg, hcchar, HCCHAR(i));
		}

		dwc2_hc_cleanup(hsotg, channel);
		list_add_tail(&channel->hc_list_entry, &hsotg->free_hc_list);
		/*
		 * Added for Descriptor DMA to prevent channel double cleanup in
		 * release_channel_ddma(), which is called from ep_disable when
		 * device disconnects
		 */
		channel->qh = NULL;
	}
	/* All channels have been freed, mark them available */
	if (hsotg->params.uframe_sched) {
		hsotg->available_host_channels =
			hsotg->params.host_channels;
	} else {
		hsotg->non_periodic_channels = 0;
		hsotg->periodic_channels = 0;
	}
}

/**
 * dwc2_hcd_connect() - Handles connect of the HCD
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_connect(struct dwc2_hsotg *hsotg)
{
	hsotg->flags.b.port_connect_status_change = 1;
	hsotg->flags.b.port_connect_status = 1;
}

/**
 * dwc2_hcd_disconnect() - Handles disconnect of the HCD
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 * @force: If true, we won't try to reconnect even if we see device connected.
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_disconnect(struct dwc2_hsotg *hsotg, bool force)
{
	u32 intr;
	u32 hprt0;

	/* Set status flags for the hub driver */
	hsotg->flags.b.port_connect_status_change = 1;
	hsotg->flags.b.port_connect_status = 0;

	/*
	 * Shutdown any transfers in process by clearing the Tx FIFO Empty
	 * interrupt mask and status bits and disabling subsequent host
	 * channel interrupts.
	 */
	intr = dwc2_readl(hsotg, GINTMSK);
	intr &= ~(GINTSTS_NPTXFEMP | GINTSTS_PTXFEMP | GINTSTS_HCHINT);
	dwc2_writel(hsotg, intr, GINTMSK);
	intr = GINTSTS_NPTXFEMP | GINTSTS_PTXFEMP | GINTSTS_HCHINT;
	dwc2_writel(hsotg, intr, GINTSTS);

	/*
	 * Turn off the vbus power only if the core has transitioned to device
	 * mode. If still in host mode, need to keep power on to detect a
	 * reconnection.
	 */
	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->op_state != OTG_STATE_A_SUSPEND)
			dwc2_writel(hsotg, 0, HPRT0);

		dwc2_disable_host_interrupts(hsotg);
	}

	/* Respond with an error status to all URBs in the schedule */
	dwc2_kill_all_urbs(hsotg);

	if (dwc2_is_host_mode(hsotg))
		/* Clean up any host channels that were in use */
		dwc2_hcd_cleanup_channels(hsotg);

	dwc2_host_disconnect(hsotg);

	/*
	 * Add an extra check here to see if we're actually connected but
	 * we don't have a detection interrupt pending.  This can happen if:
	 *   1. hardware sees connect
	 *   2. hardware sees disconnect
	 *   3. hardware sees connect
	 *   4. dwc2_port_intr() - clears connect interrupt
	 *   5. dwc2_handle_common_intr() - calls here
	 *
	 * Without the extra check here we will end calling disconnect
	 * and won't get any future interrupts to handle the connect.
	 */
	if (!force) {
		hprt0 = dwc2_readl(hsotg, HPRT0);
		if (!(hprt0 & HPRT0_CONNDET) && (hprt0 & HPRT0_CONNSTS))
			dwc2_hcd_connect(hsotg);
	}
}

/**
 * dwc2_hcd_rem_wakeup() - Handles Remote Wakeup
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 */
static void dwc2_hcd_rem_wakeup(struct dwc2_hsotg *hsotg)
{
	if (hsotg->bus_suspended) {
		hsotg->flags.b.port_suspend_change = 1;
	}
}

/**
 * dwc2_hcd_stop() - Halts the DWC_otg host mode operations in a clean manner
 *
 * @hsotg: Pointer to struct dwc2_hsotg
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_stop(struct dwc2_hsotg *hsotg)
{
	/*
	 * The root hub should be disconnected before this function is called.
	 * The disconnect will clear the QTD lists (via ..._hcd_urb_dequeue)
	 * and the QH lists (via ..._hcd_endpoint_disable).
	 */

	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	/* Turn off the vbus power */
	dwc2_writel(hsotg, 0, HPRT0);
}

/* Caller must hold driver lock */
static int dwc2_hcd_urb_enqueue(struct dwc2_hsotg *hsotg,
				struct dwc2_hcd_urb *urb, struct dwc2_qh *qh,
				struct dwc2_qtd *qtd)
{
	u32 intr_mask;
	int retval;
	int dev_speed;

	if (!hsotg->flags.b.port_connect_status) {
		/* No longer connected */
		printf("%s: Not connected\n", __func__);
		return -ENODEV;
	}

	dev_speed = dwc2_host_get_speed(hsotg, urb->priv);

	/* Some configurations cannot support LS traffic on a FS root port */
	if ((dev_speed == USB_SPEED_LOW) &&
	    (hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED) &&
	    (hsotg->hw_params.hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI)) {
		u32 hprt0 = dwc2_readl(hsotg, HPRT0);
		u32 prtspd = (hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;

		if (prtspd == HPRT0_SPD_FULL_SPEED)
			return -ENODEV;
	}

	if (!qtd)
		return -EINVAL;

	dwc2_hcd_qtd_init(qtd, urb);
	retval = dwc2_hcd_qtd_add(hsotg, qtd, qh);
	if (retval) {
		printf("DWC OTG HCD URB Enqueue failed adding QTD. Error status %d\n", retval);
		return retval;
	}

	intr_mask = dwc2_readl(hsotg, GINTMSK);
	if (!(intr_mask & GINTSTS_SOF)) {
		enum dwc2_transaction_type tr_type;

		if (qtd->qh->ep_type == USB_ENDPOINT_XFER_BULK &&
		    !(qtd->urb->flags & URB_GIVEBACK_ASAP))
			/*
			 * Do not schedule SG transactions until qtd has
			 * URB_GIVEBACK_ASAP set
			 */
			return 0;

		tr_type = dwc2_hcd_select_transactions(hsotg);
		if (tr_type != DWC2_TRANSACTION_NONE)
			dwc2_hcd_queue_transactions(hsotg, tr_type);
	}

	return 0;
}

/* Must be called with interrupt disabled and spinlock held */
static int dwc2_hcd_urb_dequeue(struct dwc2_hsotg *hsotg,
				struct dwc2_hcd_urb *urb)
{
	struct dwc2_qh *qh;
	struct dwc2_qtd *urb_qtd;

	urb_qtd = urb->qtd;
	if (!urb_qtd) {
		printf("## Urb QTD is NULL ##\n");
		return -EINVAL;
	}

	qh = urb_qtd->qh;
	if (!qh) {
		printf("## Urb QTD QH is NULL ##\n");
		return -EINVAL;
	}

	urb->priv = NULL;

	if (urb_qtd->in_process && qh->channel) {
		/* The QTD is in process (it has been assigned to a channel) */
		if (hsotg->flags.b.port_connect_status)
			/*
			 * If still connected (i.e. in host mode), halt the
			 * channel so it can be used for other transfers. If
			 * no longer connected, the host registers can't be
			 * written to halt the channel since the core is in
			 * device mode.
			 */
			dwc2_hc_halt(hsotg, qh->channel, DWC2_HC_XFER_URB_DEQUEUE);
	}

	/*
	 * Free the QTD and clean up the associated QH. Leave the QH in the
	 * schedule if it has any remaining QTDs.
	 */
	u8 in_process = urb_qtd->in_process;

	dwc2_hcd_qtd_unlink_and_free(hsotg, urb_qtd, qh);
	if (in_process) {
		dwc2_hcd_qh_deactivate(hsotg, qh, 0);
		qh->channel = NULL;
	} else if (list_empty(&qh->qtd_list)) {
		dwc2_hcd_qh_unlink(hsotg, qh);
	}

	return 0;
}

/* Must NOT be called with interrupt disabled or spinlock held */
static int dwc2_hcd_endpoint_disable(struct dwc2_hsotg *hsotg,
				     struct usb_host_endpoint *ep, int retry)
{
	struct dwc2_qtd *qtd, *qtd_tmp;
	struct dwc2_qh *qh;
	unsigned long flags;
	int rc;

	usb_spin_lock_irqsave(&hsotg->lock, flags);

	qh = (struct dwc2_qh *)ep->hcpriv;
	if (!qh) {
		rc = -EINVAL;
		goto err;
	}

	while (!list_empty(&qh->qtd_list) && retry--) {
		if (retry == 0) {
			printf("## timeout in dwc2_hcd_endpoint_disable() ##\n");
			rc = -EBUSY;
			goto err;
		}

		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
		msleep(20);
		usb_spin_lock_irqsave(&hsotg->lock, flags);
		qh = (struct dwc2_qh *)ep->hcpriv;
		if (!qh) {
			rc = -EINVAL;
			goto err;
		}
	}

	dwc2_hcd_qh_unlink(hsotg, qh);

	/* Free each QTD in the QH's QTD list */
	list_for_each_entry_safe(qtd, qtd_tmp, &qh->qtd_list, qtd_list_entry)
		dwc2_hcd_qtd_unlink_and_free(hsotg, qtd, qh);

	ep->hcpriv = NULL;

	if (qh->channel && qh->channel->qh == qh)
		qh->channel->qh = NULL;

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	dwc2_hcd_qh_free(hsotg, qh);

	return 0;

err:
	ep->hcpriv = NULL;
	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	return rc;
}

/* Must be called with interrupt disabled and spinlock held */
static int dwc2_hcd_endpoint_reset(struct dwc2_hsotg *hsotg,
				   struct usb_host_endpoint *ep)
{
	struct dwc2_qh *qh = (struct dwc2_qh *)ep->hcpriv;

	if (!qh)
		return -EINVAL;

	qh->data_toggle = DWC2_HC_PID_DATA0;

	return 0;
}

/**
 * dwc2_core_init() - Initializes the DWC_otg controller registers and
 * prepares the core for device mode or host mode operation
 *
 * @hsotg:         Programming view of the DWC_otg controller
 * @initial_setup: If true then this is the first init for this instance.
 */
int dwc2_core_init(struct dwc2_hsotg *hsotg, bool initial_setup)
{
	u32 usbcfg, otgctl;
	int retval;

	usbcfg = dwc2_readl(hsotg, GUSBCFG);

	/* Set ULPI External VBUS bit if needed */
	usbcfg &= ~GUSBCFG_ULPI_EXT_VBUS_DRV;
	if (hsotg->params.phy_ulpi_ext_vbus)
		usbcfg |= GUSBCFG_ULPI_EXT_VBUS_DRV;

	/* Set external TS Dline pulsing bit if needed */
	usbcfg &= ~GUSBCFG_TERMSELDLPULSE;
	if (hsotg->params.ts_dline)
		usbcfg |= GUSBCFG_TERMSELDLPULSE;

	dwc2_writel(hsotg, usbcfg, GUSBCFG);

	/*
	 * Reset the Controller
	 *
	 * We only need to reset the controller if this is a re-init.
	 * For the first init we know for sure that earlier code reset us (it
	 * needed to in order to properly detect various parameters).
	 */
	if (!initial_setup)
		dwc2_core_reset(hsotg, false);

	/*
	 * This needs to happen in FS mode before any other programming occurs
	 */
	retval = dwc2_phy_init(hsotg, initial_setup);
	if (retval)
		return retval;

	/* Program the GAHBCFG Register */
	retval = dwc2_gahbcfg_init(hsotg);
	if (retval)
		return retval;

	/* Program the GUSBCFG register */
	dwc2_gusbcfg_init(hsotg);

	/* Program the GOTGCTL register */
	otgctl = dwc2_readl(hsotg, GOTGCTL);
	otgctl &= ~GOTGCTL_OTGVER;
	dwc2_writel(hsotg, otgctl, GOTGCTL);

	/* Enable common interrupts */
	dwc2_enable_common_interrupts(hsotg);

	/*
	 * Do device or host initialization based on mode during PCD and
	 * HCD initialization
	 */
	if (dwc2_is_host_mode(hsotg)) {
		printf("Host Mode\n");
		hsotg->op_state = OTG_STATE_A_HOST;
	} else {
		printf("Device Mode\n");
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
	}

	return 0;
}

/**
 * dwc2_core_host_init() - Initializes the DWC_otg controller registers for
 * Host mode
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * This function flushes the Tx and Rx FIFOs and flushes any entries in the
 * request queues. Host channels are reset to ensure that they are ready for
 * performing transfers.
 */
static void dwc2_core_host_init(struct dwc2_hsotg *hsotg)
{
	u32 hcfg, hfir, otgctl, usbcfg;

	/* Set HS/FS Timeout Calibration to 7 (max available value).
	 * The number of PHY clocks that the application programs in
	 * this field is added to the high/full speed interpacket timeout
	 * duration in the core to account for any additional delays
	 * introduced by the PHY. This can be required, because the delay
	 * introduced by the PHY in generating the linestate condition
	 * can vary from one PHY to another.
	 */
	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	usbcfg |= GUSBCFG_TOUTCAL(7);
	dwc2_writel(hsotg, usbcfg, GUSBCFG);


	/* Initialize Host Configuration Register */
	dwc2_init_fs_ls_pclk_sel(hsotg);
	if (hsotg->params.speed == DWC2_SPEED_PARAM_FULL ||
	    hsotg->params.speed == DWC2_SPEED_PARAM_LOW) {
		hcfg = dwc2_readl(hsotg, HCFG);
		hcfg |= HCFG_FSLSSUPP;
		dwc2_writel(hsotg, hcfg, HCFG);
	}

	/*
	 * This bit allows dynamic reloading of the HFIR register during
	 * runtime. This bit needs to be programmed during initial configuration
	 * and its value must not be changed during runtime.
	 */
	if (hsotg->params.reload_ctl) {
		hfir = dwc2_readl(hsotg, HFIR);
		hfir |= HFIR_RLDCTRL;
		dwc2_writel(hsotg, hfir, HFIR);
	}

	/* Configure data FIFO sizes */
	dwc2_config_fifos(hsotg);

	/* TODO - check this */
	/* Clear Host Set HNP Enable in the OTG Control Register */
	otgctl = dwc2_readl(hsotg, GOTGCTL);
	otgctl &= ~GOTGCTL_HSTSETHNPEN;
	dwc2_writel(hsotg, otgctl, GOTGCTL);

	/* Make sure the FIFOs are flushed */
	dwc2_flush_tx_fifo(hsotg, 0x10 /* all TX FIFOs */);
	dwc2_flush_rx_fifo(hsotg);

	/* Clear Host Set HNP Enable in the OTG Control Register */
	otgctl = dwc2_readl(hsotg, GOTGCTL);
	otgctl &= ~GOTGCTL_HSTSETHNPEN;
	dwc2_writel(hsotg, otgctl, GOTGCTL);

	int num_channels, i;
	u32 hcchar;

	/* Flush out any leftover queued requests */
	num_channels = hsotg->params.host_channels;
	for (i = 0; i < num_channels; i++) {
		hcchar = dwc2_readl(hsotg, HCCHAR(i));
		if (hcchar & HCCHAR_CHENA) {
			hcchar &= ~HCCHAR_CHENA;
			hcchar |= HCCHAR_CHDIS;
			hcchar &= ~HCCHAR_EPDIR;
			dwc2_writel(hsotg, hcchar, HCCHAR(i));
		}
	}

	/* Halt all channels to put them into a known state */
	for (i = 0; i < num_channels; i++) {
		hcchar = dwc2_readl(hsotg, HCCHAR(i));
		if (hcchar & HCCHAR_CHENA) {
			hcchar |= HCCHAR_CHENA | HCCHAR_CHDIS;
			hcchar &= ~HCCHAR_EPDIR;
			dwc2_writel(hsotg, hcchar, HCCHAR(i));

			if (dwc2_hsotg_wait_bit_clear(hsotg, HCCHAR(i),
								HCCHAR_CHENA, 1000)) {
				printf("Unable to clear enable on channel %d\n", i);
			}
		}
	}

	/* Turn on the vbus power */
	if (hsotg->op_state == OTG_STATE_A_HOST) {
		u32 hprt0 = dwc2_read_hprt0(hsotg);
		if (!(hprt0 & HPRT0_PWR)) {
			hprt0 |= HPRT0_PWR;
			dwc2_writel(hsotg, hprt0, HPRT0);
		}
	}

	dwc2_enable_host_interrupts(hsotg);
}

/*
 * Initializes dynamic portions of the DWC_otg HCD state
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_hcd_reinit(struct dwc2_hsotg *hsotg)
{
	struct dwc2_host_chan *chan, *chan_tmp;
	int num_channels;
	int i;

	hsotg->flags.d32 = 0;
	hsotg->non_periodic_qh_ptr = &hsotg->non_periodic_sched_active;

	if (hsotg->params.uframe_sched) {
		hsotg->available_host_channels =
			hsotg->params.host_channels;
	} else {
		hsotg->non_periodic_channels = 0;
		hsotg->periodic_channels = 0;
	}

	/*
	 * Put all channels in the free channel list and clean up channel
	 * states
	 */
	list_for_each_entry_safe(chan, chan_tmp, &hsotg->free_hc_list,
				 hc_list_entry)
		list_del_init(&chan->hc_list_entry);

	num_channels = hsotg->params.host_channels;
	for (i = 0; i < num_channels; i++) {
		chan = hsotg->hc_ptr_array[i];
		list_add_tail(&chan->hc_list_entry, &hsotg->free_hc_list);
		dwc2_hc_cleanup(hsotg, chan);
	}

	/* Initialize the DWC core for host mode operation */
	dwc2_core_host_init(hsotg);
}

static void dwc2_hc_init_split(struct dwc2_hsotg *hsotg,
			       struct dwc2_host_chan *chan,
			       struct dwc2_qtd *qtd, struct dwc2_hcd_urb *urb)
{
	int hub_addr, hub_port;

	chan->do_split = 1;
	chan->xact_pos = qtd->isoc_split_pos;
	chan->complete_split = qtd->complete_split;
	dwc2_host_hub_info(hsotg, urb->priv, &hub_addr, &hub_port);
	chan->hub_addr = (u8)hub_addr;
	chan->hub_port = (u8)hub_port;
}

static void dwc2_hc_init_xfer(struct dwc2_hsotg *hsotg,
			      struct dwc2_host_chan *chan,
			      struct dwc2_qtd *qtd)
{
	struct dwc2_hcd_urb *urb = qtd->urb;
	struct dwc2_hcd_iso_packet_desc *frame_desc;

	switch (dwc2_hcd_get_pipe_type(&urb->pipe_info)) {
	case USB_ENDPOINT_XFER_CONTROL:
		chan->ep_type = USB_ENDPOINT_XFER_CONTROL;

		switch (qtd->control_phase) {
		case DWC2_CONTROL_SETUP:
			chan->do_ping = 0;
			chan->ep_is_in = 0;
			chan->data_pid_start = DWC2_HC_PID_SETUP;
			if (hsotg->params.host_dma)
				chan->xfer_dma = urb->setup_dma;
			else
				chan->xfer_buf = urb->setup_packet;
			chan->xfer_len = 8;
			break;

		case DWC2_CONTROL_DATA:
			chan->data_pid_start = qtd->data_toggle;
			break;

		case DWC2_CONTROL_STATUS:
			/*
			 * Direction is opposite of data direction or IN if no
			 * data
			 */
			if (urb->length == 0)
				chan->ep_is_in = 1;
			else
				chan->ep_is_in =
					dwc2_hcd_is_pipe_out(&urb->pipe_info);
			if (chan->ep_is_in)
				chan->do_ping = 0;
			chan->data_pid_start = DWC2_HC_PID_DATA1;
			chan->xfer_len = 0;
			if (hsotg->params.host_dma)
				chan->xfer_dma = hsotg->status_buf_dma;
			else
				chan->xfer_buf = hsotg->status_buf;
			break;
		}
		break;

	case USB_ENDPOINT_XFER_BULK:
		chan->ep_type = USB_ENDPOINT_XFER_BULK;
		break;

	case USB_ENDPOINT_XFER_INT:
		chan->ep_type = USB_ENDPOINT_XFER_INT;
		break;

	case USB_ENDPOINT_XFER_ISOC:
		chan->ep_type = USB_ENDPOINT_XFER_ISOC;

		frame_desc = &urb->iso_descs[qtd->isoc_frame_index];
		frame_desc->status = 0;

		chan->xfer_dma = urb->dma;
		chan->xfer_dma += frame_desc->offset + qtd->isoc_split_offset;
		chan->xfer_buf = urb->buf;
		chan->xfer_buf += frame_desc->offset + qtd->isoc_split_offset;

		chan->xfer_len = frame_desc->length - qtd->isoc_split_offset;

		if (chan->xact_pos == DWC2_HCSPLT_XACTPOS_ALL) {
			if (chan->xfer_len <= 188)
				chan->xact_pos = DWC2_HCSPLT_XACTPOS_ALL;
			else
				chan->xact_pos = DWC2_HCSPLT_XACTPOS_BEGIN;
		}
		break;
	}
}

static int dwc2_alloc_split_dma_aligned_buf(struct dwc2_hsotg *hsotg,
					    struct dwc2_qh *qh,
					    struct dwc2_host_chan *chan)
{
	if (chan->max_packet > DWC2_KMEM_UNALIGNED_BUF_SIZE)
		return -ENOMEM;

	if (!qh->dw_align_buf) {
		qh->dw_align_buf = cache_align_malloc(DWC2_KMEM_UNALIGNED_BUF_SIZE);
		if (!qh->dw_align_buf)
			return -ENOMEM;
	}

	if (!chan->ep_is_in)
		memcpy(qh->dw_align_buf, chan->xfer_buf, chan->xfer_len);

	flush_dcache((unsigned long)qh->dw_align_buf, DWC2_KMEM_UNALIGNED_BUF_SIZE);
	qh->dw_align_buf_dma = virt_to_phys(qh->dw_align_buf);
	chan->align_buf = qh->dw_align_buf_dma;
	return 0;
}

/**
 * dwc2_assign_and_init_hc() - Assigns transactions from a QTD to a free host
 * channel and initializes the host channel to perform the transactions. The
 * host channel is removed from the free list.
 *
 * @hsotg: The HCD state structure
 * @qh:    Transactions from the first QTD for this QH are selected and assigned
 *         to a free host channel
 */
static int dwc2_assign_and_init_hc(struct dwc2_hsotg *hsotg, struct dwc2_qh *qh)
{
	struct dwc2_host_chan *chan;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qtd *qtd;

	if (list_empty(&qh->qtd_list)) {
		printf("No QTDs in QH list\n");
		return -ENOMEM;
	}

	if (list_empty(&hsotg->free_hc_list)) {
		printf("No free channel to assign\n");
		return -ENOMEM;
	}

	chan = list_first_entry(&hsotg->free_hc_list, struct dwc2_host_chan,
				hc_list_entry);

	/* Remove host channel from free list */
	list_del_init(&chan->hc_list_entry);

	qtd = list_first_entry(&qh->qtd_list, struct dwc2_qtd, qtd_list_entry);
	urb = qtd->urb;
	qh->channel = chan;
	qtd->in_process = 1;

	/*
	 * Use usb_pipedevice to determine device address. This address is
	 * 0 before the SET_ADDRESS command and the correct address afterward.
	 */
	chan->dev_addr = dwc2_hcd_get_dev_addr(&urb->pipe_info);
	chan->ep_num = dwc2_hcd_get_ep_num(&urb->pipe_info);
	chan->speed = qh->dev_speed;
	chan->max_packet = qh->maxp;

	chan->xfer_started = 0;
	chan->halt_status = DWC2_HC_XFER_NO_HALT_STATUS;
	chan->error_state = (qtd->error_count > 0);
	chan->halt_on_queue = 0;
	chan->halt_pending = 0;
	chan->requests = 0;

	/*
	 * The following values may be modified in the transfer type section
	 * below. The xfer_len value may be reduced when the transfer is
	 * started to accommodate the max widths of the XferSize and PktCnt
	 * fields in the HCTSIZn register.
	 */

	chan->ep_is_in = (dwc2_hcd_is_pipe_in(&urb->pipe_info) != 0);
	if (chan->ep_is_in)
		chan->do_ping = 0;
	else
		chan->do_ping = qh->ping_state;

	chan->data_pid_start = qh->data_toggle;
	chan->multi_count = 1;

	if (urb->actual_length > urb->length &&
	    !dwc2_hcd_is_pipe_in(&urb->pipe_info))
		urb->actual_length = urb->length;

	chan->xfer_dma = urb->dma + urb->actual_length;
	chan->xfer_buf = (u8 *)urb->buf + urb->actual_length;
	chan->xfer_len = urb->length - urb->actual_length;
	chan->xfer_count = 0;

	/* Set the split attributes if required */
	if (qh->do_split)
		dwc2_hc_init_split(hsotg, chan, qtd, urb);
	else
		chan->do_split = 0;

	/* Set the transfer attributes */
	dwc2_hc_init_xfer(hsotg, chan, qtd);

	/* For non-dword aligned buffers */
	if (hsotg->params.host_dma && (chan->xfer_dma & 0x3)  &&
	    ((qh->do_split && chan->ep_is_in) || (chan->ep_type == USB_ENDPOINT_XFER_ISOC))) {
		if (dwc2_alloc_split_dma_aligned_buf(hsotg, qh, chan)) {
			printf("Failed to allocate memory to handle non-aligned buffer\n");
			/* Add channel back to free list */
			chan->multi_count = 0;
			list_add_tail(&chan->hc_list_entry,
						&hsotg->free_hc_list);
			qtd->in_process = 0;
			qh->channel = NULL;
			return -ENOMEM;
		}
	} else {
		/*
		 * We assume that DMA is always aligned in non-split
		 * case or split out case. Warn if not.
		 */
		if(hsotg->params.host_dma && (chan->xfer_dma & 0x3))
			printf("We assume that DMA is always aligned in non-split case or split out case. Warn if not\n");
	}

	if (chan->ep_type == USB_ENDPOINT_XFER_INT ||
	    chan->ep_type == USB_ENDPOINT_XFER_ISOC)
		/*
		 * This value may be modified when the transfer is started
		 * to reflect the actual transfer length
		 */
		chan->multi_count = qh->maxp_mult;

	dwc2_hc_init(hsotg, chan);
	chan->qh = qh;

	return 0;
}

/**
 * dwc2_hcd_select_transactions() - Selects transactions from the HCD transfer
 * schedule and assigns them to available host channels. Called from the HCD
 * interrupt handler functions.
 *
 * @hsotg: The HCD state structure
 *
 * Return: The types of new transactions that were assigned to host channels
 */
enum dwc2_transaction_type dwc2_hcd_select_transactions(
		struct dwc2_hsotg *hsotg)
{
	enum dwc2_transaction_type ret_val = DWC2_TRANSACTION_NONE;
	struct list_head *qh_ptr;
	struct dwc2_qh *qh;
	int num_channels;

	/* Process entries in the periodic ready list */
	qh_ptr = hsotg->periodic_sched_ready.next;
	while (qh_ptr != &hsotg->periodic_sched_ready) {
		if (list_empty(&hsotg->free_hc_list))
			break;
		if (hsotg->params.uframe_sched) {
			if (hsotg->available_host_channels <= 1)
				break;
			hsotg->available_host_channels--;
		}
		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (dwc2_assign_and_init_hc(hsotg, qh)) {
			if (hsotg->params.uframe_sched)
				hsotg->available_host_channels++;
			break;
		}

		/*
		 * Move the QH from the periodic ready schedule to the
		 * periodic assigned schedule
		 */
		qh_ptr = qh_ptr->next;
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->periodic_sched_assigned);
		ret_val = DWC2_TRANSACTION_PERIODIC;
	}

	/*
	 * Process entries in the inactive portion of the non-periodic
	 * schedule. Some free host channels may not be used if they are
	 * reserved for periodic transfers.
	 */
	num_channels = hsotg->params.host_channels;
	qh_ptr = hsotg->non_periodic_sched_inactive.next;
	while (qh_ptr != &hsotg->non_periodic_sched_inactive) {
		if (!hsotg->params.uframe_sched &&
		    hsotg->non_periodic_channels >= num_channels -
						hsotg->periodic_channels)
			break;
		if (list_empty(&hsotg->free_hc_list))
			break;
		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (hsotg->params.uframe_sched) {
			if (hsotg->available_host_channels < 1)
				break;
			hsotg->available_host_channels--;
		}

		if (dwc2_assign_and_init_hc(hsotg, qh)) {
			if (hsotg->params.uframe_sched)
				hsotg->available_host_channels++;
			break;
		}

		/*
		 * Move the QH from the non-periodic inactive schedule to the
		 * non-periodic active schedule
		 */
		qh_ptr = qh_ptr->next;
		list_move_tail(&qh->qh_list_entry,
			       &hsotg->non_periodic_sched_active);

		if (ret_val == DWC2_TRANSACTION_NONE)
			ret_val = DWC2_TRANSACTION_NON_PERIODIC;
		else
			ret_val = DWC2_TRANSACTION_ALL;

		if (!hsotg->params.uframe_sched)
			hsotg->non_periodic_channels++;
	}

	return ret_val;
}

/**
 * dwc2_queue_transaction() - Attempts to queue a single transaction request for
 * a host channel associated with either a periodic or non-periodic transfer
 *
 * @hsotg: The HCD state structure
 * @chan:  Host channel descriptor associated with either a periodic or
 *         non-periodic transfer
 * @fifo_dwords_avail: Number of DWORDs available in the periodic Tx FIFO
 *                     for periodic transfers or the non-periodic Tx FIFO
 *                     for non-periodic transfers
 *
 * Return: 1 if a request is queued and more requests may be needed to
 * complete the transfer, 0 if no more requests are required for this
 * transfer, -1 if there is insufficient space in the Tx FIFO
 *
 * This function assumes that there is space available in the appropriate
 * request queue. For an OUT transfer or SETUP transaction in Slave mode,
 * it checks whether space is available in the appropriate Tx FIFO.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static int dwc2_queue_transaction(struct dwc2_hsotg *hsotg,
				  struct dwc2_host_chan *chan,
				  u16 fifo_dwords_avail)
{
	int retval = 0;

	if (chan->do_split)
		/* Put ourselves on the list to keep order straight */
		list_move_tail(&chan->split_order_list_entry,
			       &hsotg->split_order);

	if (hsotg->params.host_dma && chan->qh) {
		if (!chan->xfer_started) {
			dwc2_hc_start_transfer(hsotg, chan);
			chan->qh->ping_state = 0;
		}
	} else if (chan->halt_pending) {
		/* Don't queue a request if the channel has been halted */
	} else if (chan->halt_on_queue) {
		dwc2_hc_halt(hsotg, chan, chan->halt_status);
	} else if (chan->do_ping) {
		if (!chan->xfer_started)
			dwc2_hc_start_transfer(hsotg, chan);
	} else if (!chan->ep_is_in ||
		   chan->data_pid_start == DWC2_HC_PID_SETUP) {
		if ((fifo_dwords_avail * 4) >= chan->max_packet) {
			if (!chan->xfer_started) {
				dwc2_hc_start_transfer(hsotg, chan);
				retval = 1;
			} else {
				retval = dwc2_hc_continue_transfer(hsotg, chan);
			}
		} else {
			retval = -1;
		}
	} else {
		if (!chan->xfer_started) {
			dwc2_hc_start_transfer(hsotg, chan);
			retval = 1;
		} else {
			retval = dwc2_hc_continue_transfer(hsotg, chan);
		}
	}

	return retval;
}

/*
 * Processes periodic channels for the next frame and queues transactions for
 * these channels to the DWC_otg controller. After queueing transactions, the
 * Periodic Tx FIFO Empty interrupt is enabled if there are more transactions
 * to queue as Periodic Tx FIFO or request queue space becomes available.
 * Otherwise, the Periodic Tx FIFO Empty interrupt is disabled.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_process_periodic_channels(struct dwc2_hsotg *hsotg)
{
	struct list_head *qh_ptr;
	struct dwc2_qh *qh;
	u32 tx_status;
	u32 fspcavail;
	u32 gintmsk;
	int status;
	bool no_queue_space = false;
	bool no_fifo_space = false;
	u32 qspcavail;

	/* If empty list then just adjust interrupt enables */
	if (list_empty(&hsotg->periodic_sched_assigned))
		goto exit;

	tx_status = dwc2_readl(hsotg, HPTXSTS);
	qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
		    TXSTS_QSPCAVAIL_SHIFT;
	fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
		    TXSTS_FSPCAVAIL_SHIFT;

	qh_ptr = hsotg->periodic_sched_assigned.next;
	while (qh_ptr != &hsotg->periodic_sched_assigned) {
		tx_status = dwc2_readl(hsotg, HPTXSTS);
		qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
			    TXSTS_QSPCAVAIL_SHIFT;
		if (qspcavail == 0) {
			no_queue_space = true;
			break;
		}

		qh = list_entry(qh_ptr, struct dwc2_qh, qh_list_entry);
		if (!qh->channel) {
			qh_ptr = qh_ptr->next;
			continue;
		}

		/* Make sure EP's TT buffer is clean before queueing qtds */
		if (qh->tt_buffer_dirty) {
			qh_ptr = qh_ptr->next;
			continue;
		}

		/*
		 * Set a flag if we're queuing high-bandwidth in slave mode.
		 * The flag prevents any halts to get into the request queue in
		 * the middle of multiple high-bandwidth packets getting queued.
		 */
		if (!hsotg->params.host_dma &&
		    qh->channel->multi_count > 1)
			hsotg->queuing_high_bandwidth = 1;

		fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
			    TXSTS_FSPCAVAIL_SHIFT;
		status = dwc2_queue_transaction(hsotg, qh->channel, fspcavail);
		if (status < 0) {
			no_fifo_space = true;
			break;
		}

		/*
		 * In Slave mode, stay on the current transfer until there is
		 * nothing more to do or the high-bandwidth request count is
		 * reached. In DMA mode, only need to queue one request. The
		 * controller automatically handles multiple packets for
		 * high-bandwidth transfers.
		 */
		if (hsotg->params.host_dma || status == 0 ||
		    qh->channel->requests == qh->channel->multi_count) {
			qh_ptr = qh_ptr->next;
			/*
			 * Move the QH from the periodic assigned schedule to
			 * the periodic queued schedule
			 */
			list_move_tail(&qh->qh_list_entry,
				       &hsotg->periodic_sched_queued);

			/* done queuing high bandwidth */
			hsotg->queuing_high_bandwidth = 0;
		}
	}

exit:
	if (no_queue_space || no_fifo_space ||
	    (!hsotg->params.host_dma &&
	     !list_empty(&hsotg->periodic_sched_assigned))) {
		/*
		 * May need to queue more transactions as the request
		 * queue or Tx FIFO empties. Enable the periodic Tx
		 * FIFO empty interrupt. (Always use the half-empty
		 * level to ensure that new requests are loaded as
		 * soon as possible.)
		 */
		gintmsk = dwc2_readl(hsotg, GINTMSK);
		if (!(gintmsk & GINTSTS_PTXFEMP)) {
			gintmsk |= GINTSTS_PTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	} else {
		/*
		 * Disable the Tx FIFO empty interrupt since there are
		 * no more transactions that need to be queued right
		 * now. This function is called from interrupt
		 * handlers to queue more transactions as transfer
		 * states change.
		 */
		gintmsk = dwc2_readl(hsotg, GINTMSK);
		if (gintmsk & GINTSTS_PTXFEMP) {
			gintmsk &= ~GINTSTS_PTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	}
}

/*
 * Processes active non-periodic channels and queues transactions for these
 * channels to the DWC_otg controller. After queueing transactions, the NP Tx
 * FIFO Empty interrupt is enabled if there are more transactions to queue as
 * NP Tx FIFO or request queue space becomes available. Otherwise, the NP Tx
 * FIFO Empty interrupt is disabled.
 *
 * Must be called with interrupt disabled and spinlock held
 */
static void dwc2_process_non_periodic_channels(struct dwc2_hsotg *hsotg)
{
	struct list_head *orig_qh_ptr;
	struct dwc2_qh *qh;
	u32 tx_status;
	u32 qspcavail;
	u32 fspcavail;
	u32 gintmsk;
	int status;
	int no_queue_space = 0;
	int no_fifo_space = 0;
	int more_to_do = 0;

	tx_status = dwc2_readl(hsotg, GNPTXSTS);
	qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
		    TXSTS_QSPCAVAIL_SHIFT;
	fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
		    TXSTS_FSPCAVAIL_SHIFT;

	/*
	 * Keep track of the starting point. Skip over the start-of-list
	 * entry.
	 */
	if (hsotg->non_periodic_qh_ptr == &hsotg->non_periodic_sched_active)
		hsotg->non_periodic_qh_ptr = hsotg->non_periodic_qh_ptr->next;
	orig_qh_ptr = hsotg->non_periodic_qh_ptr;

	/*
	 * Process once through the active list or until no more space is
	 * available in the request queue or the Tx FIFO
	 */
	do {
		tx_status = dwc2_readl(hsotg, GNPTXSTS);
		qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
			    TXSTS_QSPCAVAIL_SHIFT;
		if (!hsotg->params.host_dma && qspcavail == 0) {
			no_queue_space = 1;
			break;
		}

		qh = list_entry(hsotg->non_periodic_qh_ptr, struct dwc2_qh,
				qh_list_entry);
		if (!qh->channel)
			goto next;

		/* Make sure EP's TT buffer is clean before queueing qtds */
		if (qh->tt_buffer_dirty)
			goto next;

		fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
			    TXSTS_FSPCAVAIL_SHIFT;
		status = dwc2_queue_transaction(hsotg, qh->channel, fspcavail);

		if (status > 0) {
			more_to_do = 1;
		} else if (status < 0) {
			no_fifo_space = 1;
			break;
		}
next:
		/* Advance to next QH, skipping start-of-list entry */
		hsotg->non_periodic_qh_ptr = hsotg->non_periodic_qh_ptr->next;
		if (hsotg->non_periodic_qh_ptr ==
				&hsotg->non_periodic_sched_active)
			hsotg->non_periodic_qh_ptr =
					hsotg->non_periodic_qh_ptr->next;
	} while (hsotg->non_periodic_qh_ptr != orig_qh_ptr);

	if (!hsotg->params.host_dma) {
		tx_status = dwc2_readl(hsotg, GNPTXSTS);
		qspcavail = (tx_status & TXSTS_QSPCAVAIL_MASK) >>
			    TXSTS_QSPCAVAIL_SHIFT;
		fspcavail = (tx_status & TXSTS_FSPCAVAIL_MASK) >>
			    TXSTS_FSPCAVAIL_SHIFT;

		if (more_to_do || no_queue_space || no_fifo_space) {
			/*
			 * May need to queue more transactions as the request
			 * queue or Tx FIFO empties. Enable the non-periodic
			 * Tx FIFO empty interrupt. (Always use the half-empty
			 * level to ensure that new requests are loaded as
			 * soon as possible.)
			 */
			gintmsk = dwc2_readl(hsotg, GINTMSK);
			gintmsk |= GINTSTS_NPTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		} else {
			/*
			 * Disable the Tx FIFO empty interrupt since there are
			 * no more transactions that need to be queued right
			 * now. This function is called from interrupt
			 * handlers to queue more transactions as transfer
			 * states change.
			 */
			gintmsk = dwc2_readl(hsotg, GINTMSK);
			gintmsk &= ~GINTSTS_NPTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	}
}

/**
 * dwc2_hcd_queue_transactions() - Processes the currently active host channels
 * and queues transactions for these channels to the DWC_otg controller. Called
 * from the HCD interrupt handler functions.
 *
 * @hsotg:   The HCD state structure
 * @tr_type: The type(s) of transactions to queue (non-periodic, periodic,
 *           or both)
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_hcd_queue_transactions(struct dwc2_hsotg *hsotg,
				 enum dwc2_transaction_type tr_type)
{
	/* Process host channels associated with periodic transfers */
	if (tr_type == DWC2_TRANSACTION_PERIODIC ||
	    tr_type == DWC2_TRANSACTION_ALL)
		dwc2_process_periodic_channels(hsotg);

	/* Process host channels associated with non-periodic transfers */
	if (tr_type == DWC2_TRANSACTION_NON_PERIODIC ||
	    tr_type == DWC2_TRANSACTION_ALL) {
		if (!list_empty(&hsotg->non_periodic_sched_active)) {
			dwc2_process_non_periodic_channels(hsotg);
		} else {
			/*
			 * Ensure NP Tx FIFO empty interrupt is disabled when
			 * there are no non-periodic transfers to process
			 */
			u32 gintmsk = dwc2_readl(hsotg, GINTMSK);

			gintmsk &= ~GINTSTS_NPTXFEMP;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
		}
	}
}

static void dwc2_conn_id_status_change(void *data)
{
	struct dwc2_hsotg *hsotg = data;
	u32 count = 0;
	u32 gotgctl;
	unsigned long flags;

	gotgctl = dwc2_readl(hsotg, GOTGCTL);

	/* B-Device connector (Device Mode) */
	if (gotgctl & GOTGCTL_CONID_B) {
		dwc2_vbus_supply_exit(hsotg);
		/* Wait for switch to device mode */
		printf("connId B\n");
		if (hsotg->bus_suspended) {
			printf("Do port resume before switching to device mode\n");
			dwc2_port_resume(hsotg);
		}
		while (!dwc2_is_device_mode(hsotg)) {
			printf("Waiting for Peripheral Mode, Mode=%s\n",
				 dwc2_is_host_mode(hsotg) ? "Host" : "Peripheral");
			msleep(20);
			/*
			 * Sometimes the initial GOTGCTRL read is wrong, so
			 * check it again and jump to host mode if that was
			 * the case.
			 */
			gotgctl = dwc2_readl(hsotg, GOTGCTL);
			if (!(gotgctl & GOTGCTL_CONID_B))
				goto host;
			if (++count > 250)
				break;
		}
		if (count > 250)
			printf("Connection id status change timed out\n");

		/*
		 * Exit Partial Power Down without restoring registers.
		 * No need to check the return value as registers
		 * are not being restored.
		 */
		if (hsotg->in_ppd && hsotg->lx_state == DWC2_L2)
			dwc2_exit_partial_power_down(hsotg, 0, false);

		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		usb_spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_hsotg_core_init_disconnected(hsotg, false);
		if (get_gadget_enabled(hsotg))
			dwc2_hsotg_core_connect(hsotg);
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
	} else {
host:
		/* A-Device connector (Host Mode) */
		printf("connId A\n");
		while (!dwc2_is_host_mode(hsotg)) {
			printf("Waiting for Host Mode, Mode=%s\n",
				 dwc2_is_host_mode(hsotg) ?
				 "Host" : "Peripheral");
			msleep(20);
			if (++count > 250)
				break;
		}
		if (count > 250)
			printf("Connection id status change timed out\n");

		usb_spin_lock_irqsave(&hsotg->lock, flags);
		dwc2_hsotg_disconnect(hsotg);
		call_gadget_set_state(hsotg, USB_STATE_NOTATTACHED);
		call_gadget_vbus_draw(hsotg, 0);
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);

		hsotg->op_state = OTG_STATE_A_HOST;
		/* Initialize the Core for Host mode */
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hcd_start(hsotg);
	}
}

static void dwc2_wakeup_detected(struct hrtimer *hrtimer)
{
	struct dwc2_hsotg *hsotg = container_of(hrtimer, struct dwc2_hsotg, wkp_timer);
	u32 hprt0;

	/*
	 * Clear the Resume after 70ms. (Need 20 ms minimum. Use 70 ms
	 * so that OPT tests pass with all PHYs.)
	 */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 &= ~HPRT0_RES;
	dwc2_writel(hsotg, hprt0, HPRT0);

	dwc2_hcd_rem_wakeup(hsotg);
	hsotg->bus_suspended = false;

	/* Change to L0 state */
	hsotg->lx_state = DWC2_L0;
}

/**
 * dwc2_port_suspend() - Put controller in suspend mode for host.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @windex: The control request wIndex field
 *
 * Return: non-zero if failed to enter suspend mode for host.
 *
 * This function is for entering Host mode suspend.
 * Must NOT be called with interrupt disabled or spinlock held.
 */
int dwc2_port_suspend(struct dwc2_hsotg *hsotg, u16 windex)
{
	unsigned long flags;
	int ret = 0;

	usb_spin_lock_irqsave(&hsotg->lock, flags);

	switch (hsotg->params.power_down) {
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		dwc2_enter_partial_power_down(hsotg);
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		/*
		 * Perform spin unlock and lock because in
		 * "dwc2_host_enter_hibernation()" function there is a spinlock
		 * logic which prevents servicing of any IRQ during entering
		 * hibernation.
		 */
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_enter_hibernation(hsotg, 1);
		usb_spin_lock_irqsave(&hsotg->lock, flags);
		break;
	case DWC2_POWER_DOWN_PARAM_NONE:
		/*
		 * If not hibernation nor partial power down are supported,
		 * clock gating is used to save power.
		 */
		dwc2_host_enter_clock_gating(hsotg);
		break;
	}

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}

/**
 * dwc2_port_resume() - Exit controller from suspend mode for host.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 * Return: non-zero if failed to exit suspend mode for host.
 *
 * This function is for exiting Host mode suspend.
 * Must NOT be called with interrupt disabled or spinlock held.
 */
int dwc2_port_resume(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;
	int ret = 0;

	usb_spin_lock_irqsave(&hsotg->lock, flags);

	switch (hsotg->params.power_down) {
	case DWC2_POWER_DOWN_PARAM_PARTIAL:
		dwc2_exit_partial_power_down(hsotg, 0, true);
		break;
	case DWC2_POWER_DOWN_PARAM_HIBERNATION:
		/* Exit host hibernation. */
		dwc2_exit_hibernation(hsotg, 0, 0, 1);
		break;
	case DWC2_POWER_DOWN_PARAM_NONE:
		/*
		 * If not hibernation nor partial power down are supported,
		 * port resume is done using the clock gating programming flow.
		 */
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_host_exit_clock_gating(hsotg, 0);
		usb_spin_lock_irqsave(&hsotg->lock, flags);
		break;
	}

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	return ret;
}

/* Handles hub class-specific requests */
static int dwc2_hcd_hub_control(struct dwc2_hsotg *hsotg, u16 typereq,
				u16 wvalue, u16 windex, char *buf, u16 wlength)
{
	struct usb_hub_descriptor *hub_desc;
	int retval = 0;
	u32 hprt0;
	u32 port_status;
	u32 speed;
	u32 pwr;

	switch (typereq) {
	case ClearHubFeature:
		switch (wvalue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			/* Nothing required here */
			break;

		default:
			retval = -EINVAL;
			printf("ClearHubFeature request %1xh unknown\n", wvalue);
		}
		break;

	case ClearPortFeature:
		switch (wvalue) {
		case USB_PORT_FEAT_ENABLE:
			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 |= HPRT0_ENA;
			dwc2_writel(hsotg, hprt0, HPRT0);
			break;

		case USB_PORT_FEAT_SUSPEND:
			if (hsotg->bus_suspended)
				retval = dwc2_port_resume(hsotg);
			break;

		case USB_PORT_FEAT_POWER:
			hprt0 = dwc2_read_hprt0(hsotg);
			pwr = hprt0 & HPRT0_PWR;
			hprt0 &= ~HPRT0_PWR;
			dwc2_writel(hsotg, hprt0, HPRT0);
			if (pwr)
				dwc2_vbus_supply_exit(hsotg);
			break;

		case USB_PORT_FEAT_INDICATOR:
			/* Port indicator not supported */
			break;

		case USB_PORT_FEAT_C_CONNECTION:
			/*
			 * Clears driver's internal Connect Status Change flag
			 */
			hsotg->flags.b.port_connect_status_change = 0;
			break;

		case USB_PORT_FEAT_C_RESET:
			/* Clears driver's internal Port Reset Change flag */
			hsotg->flags.b.port_reset_change = 0;
			break;

		case USB_PORT_FEAT_C_ENABLE:
			/*
			 * Clears the driver's internal Port Enable/Disable
			 * Change flag
			 */
			hsotg->flags.b.port_enable_change = 0;
			break;

		case USB_PORT_FEAT_C_SUSPEND:
			/*
			 * Clears the driver's internal Port Suspend Change
			 * flag, which is set when resume signaling on the host
			 * port is complete
			 */
			hsotg->flags.b.port_suspend_change = 0;
			break;

		case USB_PORT_FEAT_C_OVER_CURRENT:
			hsotg->flags.b.port_over_current_change = 0;
			break;

		default:
			retval = -EINVAL;
			printf("ClearPortFeature request %1xh unknown or unsupported\n", wvalue);
		}
		break;

	case GetHubDescriptor:
		hub_desc = (struct usb_hub_descriptor *)buf;
		hub_desc->bDescLength = 9;
		hub_desc->bDescriptorType = USB_DT_HUB;
		hub_desc->bNbrPorts = 1;
		hub_desc->wHubCharacteristics = 0x0001;
		hub_desc->bPwrOn2PwrGood = 1;
		hub_desc->bHubContrCurrent = 0;
		hub_desc->DeviceRemovable[0] = 0;
		hub_desc->DeviceRemovable[1] = 0xff;
		break;

	case GetHubStatus:
		memset(buf, 0, 4);
		break;

	case GetPortStatus:
		if (!windex || windex > 1)
			goto error;

		port_status = 0;
		if (hsotg->flags.b.port_connect_status_change)
			port_status |= USB_PORT_STAT_C_CONNECTION << 16;
		if (hsotg->flags.b.port_enable_change)
			port_status |= USB_PORT_STAT_C_ENABLE << 16;
		if (hsotg->flags.b.port_suspend_change)
			port_status |= USB_PORT_STAT_C_SUSPEND << 16;
		if (hsotg->flags.b.port_reset_change)
			port_status |= USB_PORT_STAT_C_RESET << 16;
		if (hsotg->flags.b.port_over_current_change)
			port_status |= USB_PORT_STAT_C_OVERCURRENT << 16;

		if (dwc2_is_device_mode(hsotg)) {
			/*
			 * Just return 0's for the remainder of the port status
			 * since the port register can't be read if the core
			 * is in device mode.
			 */
			*(u32 *)buf = port_status;
			break;
		}

		hprt0 = dwc2_readl(hsotg, HPRT0);

		if (hprt0 & HPRT0_CONNSTS)
			port_status |= USB_PORT_STAT_CONNECTION;
		if (hprt0 & HPRT0_ENA)
			port_status |= USB_PORT_STAT_ENABLE;
		if (hprt0 & HPRT0_SUSP)
			port_status |= USB_PORT_STAT_SUSPEND;
		if (hprt0 & HPRT0_OVRCURRACT)
			port_status |= USB_PORT_STAT_OVERCURRENT;
		if (hprt0 & HPRT0_RST)
			port_status |= USB_PORT_STAT_RESET;
		if (hprt0 & HPRT0_PWR)
			port_status |= USB_PORT_STAT_POWER;

		speed = (hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
		if (speed == HPRT0_SPD_HIGH_SPEED)
			port_status |= USB_PORT_STAT_HIGH_SPEED;
		else if (speed == HPRT0_SPD_LOW_SPEED)
			port_status |= USB_PORT_STAT_LOW_SPEED;

		if (hprt0 & HPRT0_TSTCTL_MASK)
			port_status |= USB_PORT_STAT_TEST;
		/* USB_PORT_FEAT_INDICATOR unsupported always 0 */

		*(u32 *)buf = port_status;
		break;

	case SetHubFeature:
		/* No HUB features supported */
		break;

	case SetPortFeature:
		if (wvalue != USB_PORT_FEAT_TEST && (!windex || windex > 1))
			goto error;

		if (dwc2_is_device_mode(hsotg)) {
			/*
			* Just return 0's for the remainder of the port status
			* since the port register can't be read if the core
			* is in device mode.
			*/
			break;
		}

		switch (wvalue) {
		case USB_PORT_FEAT_SUSPEND:
				goto error;

		case USB_PORT_FEAT_POWER:
			hprt0 = dwc2_read_hprt0(hsotg);
			pwr = hprt0 & HPRT0_PWR;
			hprt0 |= HPRT0_PWR;
			dwc2_writel(hsotg, hprt0, HPRT0);
			if (!pwr)
				dwc2_vbus_supply_init(hsotg);
			break;

		case USB_PORT_FEAT_RESET:
			hprt0 = dwc2_read_hprt0(hsotg);

			if (hsotg->hibernated)
				dwc2_exit_hibernation(hsotg, 0, 1, 1);

			if (hsotg->in_ppd)
				dwc2_exit_partial_power_down(hsotg, 1, true);

			if (hsotg->params.power_down == DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_host_exit_clock_gating(hsotg, 0);

			hprt0 = dwc2_read_hprt0(hsotg);
			pwr = hprt0 & HPRT0_PWR;
			/* Clear suspend bit if resetting from suspend state */
			hprt0 &= ~HPRT0_SUSP;

			/*
			 * When B-Host the Port reset bit is set in the Start
			 * HCD Callback function, so that the reset is started
			 * within 1ms of the HNP success interrupt
			 */
			if (!dwc2_hcd_is_b_host(hsotg)) {
				hprt0 |= HPRT0_PWR | HPRT0_RST;
				dwc2_writel(hsotg, hprt0, HPRT0);
				if (!pwr)
					dwc2_vbus_supply_init(hsotg);
			}

			/* Clear reset bit in 10ms (FS/LS) or 50ms (HS) */
			msleep(50);
			hprt0 &= ~HPRT0_RST;
			dwc2_writel(hsotg, hprt0, HPRT0);
			hsotg->lx_state = DWC2_L0; /* Now back to On state */
			break;

		case USB_PORT_FEAT_INDICATOR:
			/* Not supported */
			break;

		case USB_PORT_FEAT_TEST:
			hprt0 = dwc2_read_hprt0(hsotg);
			hprt0 &= ~HPRT0_TSTCTL_MASK;
			hprt0 |= (windex >> 8) << HPRT0_TSTCTL_SHIFT;
			dwc2_writel(hsotg, hprt0, HPRT0);
			break;

		default:
			retval = -EINVAL;
			printf("SetPortFeature %1xh unknown or unsupported\n", wvalue);
			break;
		}
		break;

	default:
error:
		retval = -EINVAL;
		printf("Unknown hub control request: %1xh wIndex: %1xh wValue: %1xh\n",
			typereq, windex, wvalue);
		break;
	}

	return retval;
}

static int dwc2_hcd_is_status_changed(struct dwc2_hsotg *hsotg, int port)
{
	int retval;

	if (port != 1)
		return -EINVAL;

	retval = (hsotg->flags.b.port_connect_status_change ||
		  hsotg->flags.b.port_reset_change ||
		  hsotg->flags.b.port_enable_change ||
		  hsotg->flags.b.port_suspend_change ||
		  hsotg->flags.b.port_over_current_change);

	return retval;
}

int dwc2_hcd_get_frame_number(struct dwc2_hsotg *hsotg)
{
	u32 hfnum = dwc2_readl(hsotg, HFNUM);

	return (hfnum & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT;
}

int dwc2_hcd_get_future_frame_number(struct dwc2_hsotg *hsotg, int us)
{
	u32 hprt = dwc2_readl(hsotg, HPRT0);
	u32 hfir = dwc2_readl(hsotg, HFIR);
	u32 hfnum = dwc2_readl(hsotg, HFNUM);
	unsigned int us_per_frame;
	unsigned int frame_number;
	unsigned int remaining;
	unsigned int interval;
	unsigned int phy_clks;

	/* High speed has 125 us per (micro) frame; others are 1 ms per */
	us_per_frame = (hprt & HPRT0_SPD_MASK) ? 1000 : 125;

	/* Extract fields */
	frame_number = (hfnum & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT;
	remaining = (hfnum & HFNUM_FRREM_MASK) >> HFNUM_FRREM_SHIFT;
	interval = (hfir & HFIR_FRINT_MASK) >> HFIR_FRINT_SHIFT;

	/*
	 * Number of phy clocks since the last tick of the frame number after
	 * "us" has passed.
	 */
	phy_clks = (interval - remaining) +
		   DIV_ROUND_UP(interval * us, us_per_frame);

	return dwc2_frame_num_inc(frame_number, phy_clks / interval);
}

int dwc2_hcd_is_b_host(struct dwc2_hsotg *hsotg)
{
	return hsotg->op_state == OTG_STATE_B_HOST;
}

static struct dwc2_hcd_urb *dwc2_hcd_urb_alloc(struct dwc2_hsotg *hsotg,
					       int iso_desc_count)
{
	struct dwc2_hcd_urb *urb;
	u32 size = sizeof(*urb) + iso_desc_count * sizeof(struct dwc2_hcd_iso_packet_desc);

	urb = malloc(size);
	if (urb) {
		memset(urb, 0, size);
		urb->packet_count = iso_desc_count;
	}
	return urb;
}

static void dwc2_hcd_urb_set_pipeinfo(struct dwc2_hsotg *hsotg,
				      struct dwc2_hcd_urb *urb, u8 dev_addr,
				      u8 ep_num, u8 ep_type, u8 ep_dir,
				      u16 maxp, u16 maxp_mult)
{
	urb->pipe_info.dev_addr = dev_addr;
	urb->pipe_info.ep_num = ep_num;
	urb->pipe_info.pipe_type = ep_type;
	urb->pipe_info.pipe_dir = ep_dir;
	urb->pipe_info.maxp = maxp;
	urb->pipe_info.maxp_mult = maxp_mult;
}

/*
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_hcd_dump_state(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	struct dwc2_host_chan *chan;
	struct dwc2_hcd_urb *urb;
	struct dwc2_qtd *qtd;
	int num_channels;
	u32 np_tx_status;
	u32 p_tx_status;
	int i;

	num_channels = hsotg->params.host_channels;
	printf("\n");
	printf("************************************************************\n");
	printf("HCD State:\n");
	printf("  Num channels: %d\n", num_channels);

	for (i = 0; i < num_channels; i++) {
		chan = hsotg->hc_ptr_array[i];
		printf("  Channel %d:\n", i);
		printf("    dev_addr: %d, ep_num: %d, ep_is_in: %d\n",
			chan->dev_addr, chan->ep_num, chan->ep_is_in);
		printf("    speed: %d\n", chan->speed);
		printf("    ep_type: %d\n", chan->ep_type);
		printf("    max_packet: %d\n", chan->max_packet);
		printf("    data_pid_start: %d\n",
			chan->data_pid_start);
		printf("    multi_count: %d\n", chan->multi_count);
		printf("    xfer_started: %d\n",
			chan->xfer_started);
		printf("    xfer_buf: %p\n", chan->xfer_buf);
		printf("    xfer_dma: %08lx\n",
			(unsigned long)chan->xfer_dma);
		printf("    xfer_len: %d\n", chan->xfer_len);
		printf("    xfer_count: %d\n", chan->xfer_count);
		printf("    halt_on_queue: %d\n",
			chan->halt_on_queue);
		printf("    halt_pending: %d\n",
			chan->halt_pending);
		printf("    halt_status: %d\n", chan->halt_status);
		printf("    do_split: %d\n", chan->do_split);
		printf("    complete_split: %d\n",
			chan->complete_split);
		printf("    hub_addr: %d\n", chan->hub_addr);
		printf("    hub_port: %d\n", chan->hub_port);
		printf("    xact_pos: %d\n", chan->xact_pos);
		printf("    requests: %d\n", chan->requests);
		printf("    qh: %p\n", chan->qh);

		if (chan->xfer_started) {
			u32 hfnum, hcchar, hctsiz, hcint, hcintmsk;

			hfnum = dwc2_readl(hsotg, HFNUM);
			hcchar = dwc2_readl(hsotg, HCCHAR(i));
			hctsiz = dwc2_readl(hsotg, HCTSIZ(i));
			hcint = dwc2_readl(hsotg, HCINT(i));
			hcintmsk = dwc2_readl(hsotg, HCINTMSK(i));
			printf("    hfnum: 0x%08x\n", hfnum);
			printf("    hcchar: 0x%08x\n", hcchar);
			printf("    hctsiz: 0x%08x\n", hctsiz);
			printf("    hcint: 0x%08x\n", hcint);
			printf("    hcintmsk: 0x%08x\n", hcintmsk);
		}

		if (!(chan->xfer_started && chan->qh))
			continue;

		list_for_each_entry(qtd, &chan->qh->qtd_list, qtd_list_entry) {
			if (!qtd->in_process)
				break;
			urb = qtd->urb;
			printf("    URB Info:\n");
			printf("      qtd: %p, urb: %p\n",
				qtd, urb);
			if (urb) {
				printf("      Dev: %d, EP: %d %s\n",
					dwc2_hcd_get_dev_addr(&urb->pipe_info),
					dwc2_hcd_get_ep_num(&urb->pipe_info),
					dwc2_hcd_is_pipe_in(&urb->pipe_info) ?
					"IN" : "OUT");
				printf("      Max packet size: %d (%d mult)\n",
					dwc2_hcd_get_maxp(&urb->pipe_info),
					dwc2_hcd_get_maxp_mult(&urb->pipe_info));
				printf("      transfer_buffer: %p\n",
					urb->buf);
				printf("      transfer_dma: %08lx\n",
					(unsigned long)urb->dma);
				printf("      transfer_buffer_length: %d\n",
					urb->length);
				printf("      actual_length: %d\n",
					urb->actual_length);
			}
		}
	}

	printf("  non_periodic_channels: %d\n",
		hsotg->non_periodic_channels);
	printf("  periodic_channels: %d\n",
		hsotg->periodic_channels);
	printf("  periodic_usecs: %d\n", hsotg->periodic_usecs);
	np_tx_status = dwc2_readl(hsotg, GNPTXSTS);
	printf("  NP Tx Req Queue Space Avail: %d\n",
		(np_tx_status & TXSTS_QSPCAVAIL_MASK) >> TXSTS_QSPCAVAIL_SHIFT);
	printf("  NP Tx FIFO Space Avail: %d\n",
		(np_tx_status & TXSTS_FSPCAVAIL_MASK) >> TXSTS_FSPCAVAIL_SHIFT);
	p_tx_status = dwc2_readl(hsotg, HPTXSTS);
	printf("  P Tx Req Queue Space Avail: %d\n",
		(p_tx_status & TXSTS_QSPCAVAIL_MASK) >> TXSTS_QSPCAVAIL_SHIFT);
	printf("  P Tx FIFO Space Avail: %d\n",
		(p_tx_status & TXSTS_FSPCAVAIL_MASK) >> TXSTS_FSPCAVAIL_SHIFT);
	dwc2_dump_global_registers(hsotg);
	dwc2_dump_host_registers(hsotg);
	printf("************************************************************\n");
	printf("\n");
#endif
}

struct wrapper_priv_data {
	struct dwc2_hsotg *hsotg;
};

/* Gets the dwc2_hsotg from a usb_hcd */
static struct dwc2_hsotg *dwc2_hcd_to_hsotg(struct usb_hcd *hcd)
{
	struct wrapper_priv_data *p;

	p = (struct wrapper_priv_data *)&hcd->hcd_priv;
	return p->hsotg;
}

/**
 * dwc2_host_get_tt_info() - Get the dwc2_tt associated with context
 *
 * This will get the dwc2_tt structure (and ttport) associated with the given
 * context (which is really just a struct urb pointer).
 *
 * The first time this is called for a given TT we allocate memory for our
 * structure.  When everyone is done and has called dwc2_host_put_tt_info()
 * then the refcount for the structure will go to 0 and we'll free it.
 *
 * @hsotg:     The HCD state structure for the DWC OTG controller.
 * @context:   The priv pointer from a struct dwc2_hcd_urb.
 * @ttport:    We'll return this device's port number here.  That's used to
 *             reference into the bitmap if we're on a multi_tt hub.
 *
 * Return: a pointer to a struct dwc2_tt.  Don't forget to call
 *         dwc2_host_put_tt_info()!  Returns NULL upon memory alloc failure.
 */

struct dwc2_tt *dwc2_host_get_tt_info(struct dwc2_hsotg *hsotg, void *context, int *ttport)
{
	struct urb *urb = context;
	struct dwc2_tt *dwc_tt = NULL;

	if (urb->dev->tt) {
		*ttport = urb->dev->ttport;

		dwc_tt = urb->dev->tt->hcpriv;
		if (!dwc_tt) {
			size_t bitmap_size;

			/*
			 * For single_tt we need one schedule.  For multi_tt
			 * we need one per port.
			 */
			bitmap_size = DWC2_ELEMENTS_PER_LS_BITMAP *
				      sizeof(dwc_tt->periodic_bitmaps[0]);
			if (urb->dev->tt->multi)
				bitmap_size *= urb->dev->tt->hub->maxchild;

			dwc_tt = malloc(sizeof(*dwc_tt) + bitmap_size);
			if (!dwc_tt)
				return NULL;

			memset(dwc_tt, 0, sizeof(*dwc_tt) + bitmap_size);
			dwc_tt->usb_tt = urb->dev->tt;
			dwc_tt->usb_tt->hcpriv = dwc_tt;
		}

		dwc_tt->refcount++;
	}

	return dwc_tt;
}

/**
 * dwc2_host_put_tt_info() - Put the dwc2_tt from dwc2_host_get_tt_info()
 *
 * Frees resources allocated by dwc2_host_get_tt_info() if all current holders
 * of the structure are done.
 *
 * It's OK to call this with NULL.
 *
 * @hsotg:     The HCD state structure for the DWC OTG controller.
 * @dwc_tt:    The pointer returned by dwc2_host_get_tt_info.
 */
void dwc2_host_put_tt_info(struct dwc2_hsotg *hsotg, struct dwc2_tt *dwc_tt)
{
	/* Model free and make put of NULL a no-op */
	if (!dwc_tt)
		return;

	assert(dwc_tt->refcount >= 1);

	dwc_tt->refcount--;
	if (!dwc_tt->refcount) {
		dwc_tt->usb_tt->hcpriv = NULL;
		free(dwc_tt);
	}
}

int dwc2_host_get_speed(struct dwc2_hsotg *hsotg, void *context)
{
	struct urb *urb = context;

	return urb->dev->speed;
}

static void dwc2_allocate_bus_bandwidth(struct usb_hcd *hcd, u16 bw,
					struct urb *urb)
{
	struct usb_bus *bus = hcd_to_bus(hcd);

	if (urb->interval)
		bus->bandwidth_allocated += bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		bus->bandwidth_isoc_reqs++;
	else
		bus->bandwidth_int_reqs++;
}

static void dwc2_free_bus_bandwidth(struct usb_hcd *hcd, u16 bw,
				    struct urb *urb)
{
	struct usb_bus *bus = hcd_to_bus(hcd);

	if (urb->interval)
		bus->bandwidth_allocated -= bw / urb->interval;
	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
		bus->bandwidth_isoc_reqs--;
	else
		bus->bandwidth_int_reqs--;
}

/*
 * Sets the final status of an URB and returns it to the upper layer. Any
 * required cleanup of the URB is performed.
 *
 * Must be called with interrupt disabled and spinlock held
 */
void dwc2_host_complete(struct dwc2_hsotg *hsotg, struct dwc2_qtd *qtd,
			int status)
{
	struct usb_hcd *hcd = dwc2_hsotg_to_hcd(hsotg);
	struct urb *urb;
	int i;

	if (!hcd) {
		printf("## %s: hcd is NULL ##\n", __func__);
		return;
	}

	if (!qtd) {
		printf("## %s: qtd is NULL ##\n", __func__);
		return;
	}

	if (!qtd->urb) {
		printf("## %s: qtd->urb is NULL ##\n", __func__);
		return;
	}

	urb = qtd->urb->priv;
	if (!urb) {
		printf("## %s: urb->priv is NULL ##\n", __func__);
		return;
	}

	urb->actual_length = dwc2_hcd_urb_get_actual_length(qtd->urb);

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS) {
		urb->error_count = dwc2_hcd_urb_get_error_count(qtd->urb);
		for (i = 0; i < urb->number_of_packets; ++i) {
			urb->iso_frame_desc[i].actual_length =
				dwc2_hcd_urb_get_iso_desc_actual_length(
						qtd->urb, i);
			urb->iso_frame_desc[i].status =
				dwc2_hcd_urb_get_iso_desc_status(qtd->urb, i);
		}
	}

	urb->status = status;
	if (!status) {
		if ((urb->transfer_flags & URB_SHORT_NOT_OK) &&
		    urb->actual_length < urb->transfer_buffer_length)
			urb->status = -EIO;
	}

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS ||
	    usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		struct usb_host_endpoint *ep = urb->ep;

		if (ep)
			dwc2_free_bus_bandwidth(hcd,
					dwc2_hcd_get_ep_bandwidth(hsotg, ep), urb);
	}

	urb->hcpriv = NULL;
	free(qtd->urb);
	qtd->urb = NULL;

	usb_hcd_giveback_urb(hcd, urb);
}

/*
 * Work queue function for starting the HCD when A-Cable is connected
 */
static void dwc2_hcd_start_func(void *data)
{
	struct dwc2_hsotg *hsotg = data;

	dwc2_host_start(hsotg);
}

/*
 * =========================================================================
 *  Linux HC Driver Functions
 * =========================================================================
 */

/*
 * Initializes the DWC_otg controller and its root hub and prepares it for host
 * mode operation. Activates the root port. Returns 0 on success and a negative
 * error code on failure.
 */
static int _dwc2_hcd_start(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;
	u32 hprt0;

	usb_spin_lock_irqsave(&hsotg->lock, flags);
	hsotg->lx_state = DWC2_L0;
	hcd->state = HC_STATE_RUNNING;

	if (dwc2_is_device_mode(hsotg)) {
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
		return 0;	/* why 0 ?? */
	}

	dwc2_hcd_reinit(hsotg);

	hprt0 = dwc2_read_hprt0(hsotg);
	/* Has vbus power been turned on in dwc2_core_host_init ? */
	if (hprt0 & HPRT0_PWR) {
		/* Enable external vbus supply before resuming root hub */
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
		dwc2_vbus_supply_init(hsotg);
		usb_spin_lock_irqsave(&hsotg->lock, flags);
	}

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;
}

/*
 * Halts the DWC_otg host mode operations in a clean manner. USB transfers are
 * stopped.
 */
static void _dwc2_hcd_stop(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;
	u32 hprt0;

	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	usb_spin_lock_irqsave(&hsotg->lock, flags);
	hprt0 = dwc2_read_hprt0(hsotg);
	/* Ensure hcd is disconnected */
	dwc2_hcd_disconnect(hsotg, true);
	dwc2_hcd_stop(hsotg);
	hsotg->lx_state = DWC2_L3;
	hcd->state = HC_STATE_HALT;
	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	/* keep balanced supply init/exit by checking HPRT0_PWR */
	if (hprt0 & HPRT0_PWR)
		dwc2_vbus_supply_exit(hsotg);

	usleep(3000);
}

/* Returns the current frame number */
static int _dwc2_hcd_get_frame_number(struct usb_hcd *hcd)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	return dwc2_hcd_get_frame_number(hsotg);
}

/*
 * Starts processing a USB transfer request specified by a USB Request Block
 * (URB).
 */
static int _dwc2_hcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct usb_host_endpoint *ep = urb->ep;
	struct dwc2_hcd_urb *dwc2_urb;
	int i;
	int retval;
	int alloc_bandwidth = 0;
	u8 ep_type = 0;
	u32 tflags = 0;
	void *buf;
	unsigned long flags;
	struct dwc2_qh *qh;
	bool qh_allocated = false;
	struct dwc2_qtd *qtd;
	struct dwc2_gregs_backup *gr;

	gr = &hsotg->gr_backup;

	if (hsotg->hibernated) {
		if (gr->gotgctl & GOTGCTL_CURMODE_HOST)
			dwc2_exit_hibernation(hsotg, 0, 0, 1);
		else
			dwc2_exit_hibernation(hsotg, 0, 0, 0);
	}

	if (hsotg->in_ppd) {
		dwc2_exit_partial_power_down(hsotg, 0, true);
	}

	if (hsotg->params.power_down == DWC2_POWER_DOWN_PARAM_NONE &&
	    hsotg->bus_suspended) {
		if (dwc2_is_device_mode(hsotg))
			dwc2_gadget_exit_clock_gating(hsotg, 0);
		else
			dwc2_host_exit_clock_gating(hsotg, 0);
	}

	if (!ep)
		return -EINVAL;

	if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS ||
	    usb_pipetype(urb->pipe) == PIPE_INTERRUPT) {
		usb_spin_lock_irqsave(&hsotg->lock, flags);
		if (!dwc2_hcd_is_bandwidth_allocated(hsotg, ep))
			alloc_bandwidth = 1;
		usb_spin_unlock_irqrestore(&hsotg->lock, flags);
	}

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_CONTROL:
		ep_type = USB_ENDPOINT_XFER_CONTROL;
		break;
	case PIPE_ISOCHRONOUS:
		ep_type = USB_ENDPOINT_XFER_ISOC;
		break;
	case PIPE_BULK:
		ep_type = USB_ENDPOINT_XFER_BULK;
		break;
	case PIPE_INTERRUPT:
		ep_type = USB_ENDPOINT_XFER_INT;
		break;
	}

	dwc2_urb = dwc2_hcd_urb_alloc(hsotg, urb->number_of_packets);
	if (!dwc2_urb)
		return -ENOMEM;

	dwc2_hcd_urb_set_pipeinfo(hsotg, dwc2_urb, usb_pipedevice(urb->pipe),
				  usb_pipeendpoint(urb->pipe), ep_type,
				  usb_pipein(urb->pipe),
				  usb_endpoint_maxp(&ep->desc),
				  usb_endpoint_maxp_mult(&ep->desc));

	buf = urb->transfer_buffer;

	if (hcd_uses_dma(hcd)) {
		if (!buf && (urb->transfer_dma % cache_line_size())) {
			printf("%s: unaligned transfer with no transfer_buffer\n", __func__);
			retval = -EINVAL;
			goto fail0;
		}
	}

	if (!(urb->transfer_flags & URB_NO_INTERRUPT))
		tflags |= URB_GIVEBACK_ASAP;
	if (urb->transfer_flags & URB_ZERO_PACKET)
		tflags |= URB_SEND_ZERO_PACKET;

	dwc2_urb->priv = urb;
	dwc2_urb->buf = buf;
	dwc2_urb->dma = urb->transfer_dma;
	dwc2_urb->length = urb->transfer_buffer_length;
	dwc2_urb->setup_packet = urb->setup_packet;
	dwc2_urb->setup_dma = urb->setup_dma;
	dwc2_urb->flags = tflags;
	dwc2_urb->interval = urb->interval;
	dwc2_urb->status = -EINPROGRESS;

	for (i = 0; i < urb->number_of_packets; ++i)
		dwc2_hcd_urb_set_iso_desc_params(dwc2_urb, i,
						 urb->iso_frame_desc[i].offset,
						 urb->iso_frame_desc[i].length);

	urb->hcpriv = dwc2_urb;
	qh = (struct dwc2_qh *)ep->hcpriv;
	/* Create QH for the endpoint if it doesn't exist */
	if (!qh) {
		qh = dwc2_hcd_qh_create(hsotg, dwc2_urb);
		if (!qh) {
			retval = -ENOMEM;
			goto fail0;
		}
		ep->hcpriv = qh;
		qh_allocated = true;
	}

	qtd = malloc(sizeof(*qtd));
	if (!qtd) {
		retval = -ENOMEM;
		goto fail1;
	}
	memset(qtd, 0, sizeof(*qtd));

	usb_spin_lock_irqsave(&hsotg->lock, flags);
	retval = dwc2_hcd_urb_enqueue(hsotg, dwc2_urb, qh, qtd);
	if (retval)
		goto fail3;

	if (alloc_bandwidth) {
		dwc2_allocate_bus_bandwidth(hcd, dwc2_hcd_get_ep_bandwidth(hsotg, ep), urb);
	}

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);

	return 0;

fail3:
	dwc2_urb->priv = NULL;
	if (qh_allocated && qh->channel && qh->channel->qh == qh)
		qh->channel->qh = NULL;
	urb->hcpriv = NULL;
	usb_spin_unlock_irqrestore(&hsotg->lock, flags);
	free(qtd);
fail1:
	if (qh_allocated) {
		struct dwc2_qtd *qtd2, *qtd2_tmp;

		ep->hcpriv = NULL;
		dwc2_hcd_qh_unlink(hsotg, qh);
		/* Free each QTD in the QH's QTD list */
		list_for_each_entry_safe(qtd2, qtd2_tmp, &qh->qtd_list,
					 qtd_list_entry)
			dwc2_hcd_qtd_unlink_and_free(hsotg, qtd2, qh);
		dwc2_hcd_qh_free(hsotg, qh);
	}
fail0:
	free(dwc2_urb);

	return retval;
}

/*
 * Aborts/cancels a USB transfer request. Always returns 0 to indicate success.
 */
static int _dwc2_hcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	int rc = 0;
	unsigned long flags;

	usb_spin_lock_irqsave(&hsotg->lock, flags);

	if (!urb->hcpriv) {
		printf("## urb->hcpriv is NULL ##\n");
		goto out;
	}

	rc = dwc2_hcd_urb_dequeue(hsotg, urb->hcpriv);

	free(urb->hcpriv);
	urb->hcpriv = NULL;

	/* Higher layer software sets URB status */
	usb_hcd_giveback_urb(hcd, urb);
out:
	usb_spin_unlock_irqrestore(&hsotg->lock, flags);
	return rc;
}

/*
 * Frees resources in the DWC_otg controller related to a given endpoint. Also
 * clears state in the HCD related to the endpoint. Any URBs for the endpoint
 * must already be dequeued.
 */
static void _dwc2_hcd_endpoint_disable(struct usb_hcd *hcd,
				       struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	dwc2_hcd_endpoint_disable(hsotg, ep, 250);
}

/*
 * Resets endpoint specific parameter values, in current version used to reset
 * the data toggle (as a WA). This function can be called from usb_clear_halt
 * routine.
 */
static void _dwc2_hcd_endpoint_reset(struct usb_hcd *hcd,
				     struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	unsigned long flags;

	usb_spin_lock_irqsave(&hsotg->lock, flags);
	dwc2_hcd_endpoint_reset(hsotg, ep);
	usb_spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * Creates Status Change bitmap for the root hub and root port. The bitmap is
 * returned in buf. Bit 0 is the status change indicator for the root hub. Bit 1
 * is the status change indicator for the single root port. Returns 1 if either
 * change indicator is 1, otherwise returns 0.
 */
static int _dwc2_hcd_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);

	buf[0] = dwc2_hcd_is_status_changed(hsotg, 1) << 1;
	return buf[0] != 0;
}

/* Handles hub class-specific requests */
static int _dwc2_hcd_hub_control(struct usb_hcd *hcd, u16 typereq, u16 wvalue,
				 u16 windex, char *buf, u16 wlength)
{
	int retval = dwc2_hcd_hub_control(dwc2_hcd_to_hsotg(hcd), typereq,
					  wvalue, windex, buf, wlength);
	return retval;
}

/* Handles hub TT buffer clear completions */
static void _dwc2_hcd_clear_tt_buffer_complete(struct usb_hcd *hcd,
					       struct usb_host_endpoint *ep)
{
	struct dwc2_hsotg *hsotg = dwc2_hcd_to_hsotg(hcd);
	struct dwc2_qh *qh;
	unsigned long flags;

	qh = (struct dwc2_qh *)ep->hcpriv;
	if (!qh)
		return;

	usb_spin_lock_irqsave(&hsotg->lock, flags);
	qh->tt_buffer_dirty = 0;

	if (hsotg->flags.b.port_connect_status)
		dwc2_hcd_queue_transactions(hsotg, DWC2_TRANSACTION_ALL);

	usb_spin_unlock_irqrestore(&hsotg->lock, flags);
}

static struct hc_driver dwc2_hc_driver = {
	.description = "dwc2_hsotg",
	.product_desc = "DWC OTG Controller",
	.hcd_priv_size = sizeof(struct wrapper_priv_data),

	.flags = HCD_MEMORY | HCD_USB2,

	.start = _dwc2_hcd_start,
	.stop = _dwc2_hcd_stop,
	.urb_enqueue = _dwc2_hcd_urb_enqueue,
	.urb_dequeue = _dwc2_hcd_urb_dequeue,
	.endpoint_disable = _dwc2_hcd_endpoint_disable,
	.endpoint_reset = _dwc2_hcd_endpoint_reset,
	.get_frame_number = _dwc2_hcd_get_frame_number,

	.hub_status_data = _dwc2_hcd_hub_status_data,
	.hub_control = _dwc2_hcd_hub_control,
	.clear_tt_buffer_complete = _dwc2_hcd_clear_tt_buffer_complete,
};

/*
 * Frees secondary storage associated with the dwc2_hsotg structure contained
 * in the struct usb_hcd field
 */
static void dwc2_hcd_free(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg;
	u32 dctl;
	int i;

	/* Free memory for QH/QTD lists */
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_inactive);
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_waiting);
	dwc2_qh_list_free(hsotg, &hsotg->non_periodic_sched_active);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_inactive);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_ready);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_assigned);
	dwc2_qh_list_free(hsotg, &hsotg->periodic_sched_queued);

	/* Free memory for the host channels */
	for (i = 0; i < MAX_EPS_CHANNELS; i++) {
		struct dwc2_host_chan *chan = hsotg->hc_ptr_array[i];

		if (chan) {
			hsotg->hc_ptr_array[i] = NULL;
			free(chan);
		}
	}

	if (hsotg->params.host_dma)
		hsotg->status_buf = (void *)KSEG0ADDR(hsotg->status_buf);
	free(hsotg->status_buf);
	hsotg->status_buf = NULL;
	hsotg->status_buf_dma = 0;

	ahbcfg = dwc2_readl(hsotg, GAHBCFG);

	/* Disable all interrupts */
	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(hsotg, ahbcfg, GAHBCFG);
	dwc2_writel(hsotg, 0, GINTMSK);

	if (hsotg->hw_params.snpsid >= DWC2_CORE_REV_3_00a) {
		dctl = dwc2_readl(hsotg, DCTL);
		dctl |= DCTL_SFTDISCON;
		dwc2_writel(hsotg, dctl, DCTL);
	}

	cancel_delayed_work(&hsotg->start_work);

	hrtimer_cancel(&hsotg->wkp_timer);
}

static void dwc2_hcd_release(struct dwc2_hsotg *hsotg)
{
	/* Turn off all host-specific interrupts */
	dwc2_disable_host_interrupts(hsotg);

	dwc2_hcd_free(hsotg);
}

/*
 * Initializes the HCD. This function allocates memory for and initializes the
 * static parts of the usb_hcd and dwc2_hsotg structures. It also registers the
 * USB bus with the core and calls the hc_driver->start() function. It returns
 * a negative error on failure.
 */
int dwc2_hcd_init(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd;
	struct dwc2_host_chan *channel;
	int i, num_channels;
	int retval;
	retval = -ENOMEM;

	assert(!gpio_pin_request(hsotg->usb_info->config_info.vbus_power, "vbus_power"));
	gpio_pin_output_disable(hsotg->usb_info->config_info.vbus_power);

	if (hsotg->params.host_dma)
		dwc2_hc_driver.flags |= HCD_DMA;

	hcd = usb_create_hcd(&dwc2_hc_driver, "dwc2");
	if (!hcd)
		goto error1;

	((struct wrapper_priv_data *)&hcd->hcd_priv)->hsotg = hsotg;
	hsotg->priv = hcd;

	/*
	 * Disable the global interrupt until all the interrupt handlers are
	 * installed
	 */
	dwc2_disable_global_interrupts(hsotg);

	/* Initialize the DWC_otg core, and select the Phy type */
	retval = dwc2_core_init(hsotg, true);
	if (retval)
		goto error2;

	/* Create new workqueue and init work */
	retval = -ENOMEM;
	init_delayed_work_struct(&hsotg->wf_otg, dwc2_conn_id_status_change, hsotg);

	hrtimer_init(&hsotg->wkp_timer, dwc2_wakeup_detected);

	/* Initialize the non-periodic schedule */
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_inactive);
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_waiting);
	INIT_LIST_HEAD(&hsotg->non_periodic_sched_active);

	/* Initialize the periodic schedule */
	INIT_LIST_HEAD(&hsotg->periodic_sched_inactive);
	INIT_LIST_HEAD(&hsotg->periodic_sched_ready);
	INIT_LIST_HEAD(&hsotg->periodic_sched_assigned);
	INIT_LIST_HEAD(&hsotg->periodic_sched_queued);

	INIT_LIST_HEAD(&hsotg->split_order);

	/*
	 * Create a host channel descriptor for each host channel implemented
	 * in the controller. Initialize the channel descriptor array.
	 */
	INIT_LIST_HEAD(&hsotg->free_hc_list);
	num_channels = hsotg->params.host_channels;
	memset(&hsotg->hc_ptr_array[0], 0, sizeof(hsotg->hc_ptr_array));

	for (i = 0; i < num_channels; i++) {
		channel = malloc(sizeof(*channel));
		if (!channel)
			goto error3;
		memset(channel, 0, sizeof(*channel));
		channel->hc_num = i;
		INIT_LIST_HEAD(&channel->split_order_list_entry);
		hsotg->hc_ptr_array[i] = channel;
	}

	/* Initialize work */
	init_delayed_work_struct(&hsotg->start_work, dwc2_hcd_start_func, hsotg);

	hsotg->status_buf = malloc(DWC2_HCD_STATUS_BUF_SIZE);
	if (!hsotg->status_buf)
		goto error3;

	if (hsotg->params.host_dma) {
		hsotg->status_buf = (void *)KSEG1ADDR(hsotg->status_buf);
		hsotg->status_buf_dma = CPHYSADDR(hsotg->status_buf);
	}

	hsotg->periodic_qh_count = 0;

	/* Initiate lx_state to L3 disconnected state */
	hsotg->lx_state = DWC2_L3;

	/*
	 * Finish generic HCD initialization and start the HCD. This function
	 * registers the USB bus, and calls hcd_start method.
	 */
	retval = usb_add_hcd(hcd);
	if (retval < 0)
		goto error3;

	dwc2_hcd_dump_state(hsotg);

	dwc2_enable_global_interrupts(hsotg);

	return 0;

error3:
	dwc2_hcd_release(hsotg);
error2:
	usb_free_hcd(hcd);
error1:
	gpio_pin_release(hsotg->usb_info->config_info.vbus_power);
	printf("%s() FAILED, returning %d\n", __func__, retval);
	return retval;
}

/*
 * Removes the HCD.
 * Frees memory and resources associated with the HCD and deregisters the bus.
 */
void dwc2_hcd_remove(struct dwc2_hsotg *hsotg)
{
	struct usb_hcd *hcd;

	hcd = dwc2_hsotg_to_hcd(hsotg);
	if (!hcd) {
		printf("%s: dwc2_hsotg_to_hcd(hsotg) NULL!\n", __func__);
		return;
	}

	gpio_pin_release(hsotg->usb_info->config_info.vbus_power);
	usb_remove_hcd(hcd);
	hsotg->priv = NULL;

	dwc2_hcd_release(hsotg);
	usb_free_hcd(hcd);
}

/**
 * dwc2_backup_host_registers() - Backup controller host registers.
 * When suspending usb bus, registers needs to be backuped
 * if controller power is disabled once suspended.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
void dwc2_backup_host_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hregs_backup *hr;
	int i;

	/* Backup Host regs */
	hr = &hsotg->hr_backup;
	hr->hcfg = dwc2_readl(hsotg, HCFG);
	hr->hflbaddr = dwc2_readl(hsotg, HFLBADDR);
	hr->haintmsk = dwc2_readl(hsotg, HAINTMSK);
	for (i = 0; i < hsotg->params.host_channels; ++i) {
		hr->hcchar[i] = dwc2_readl(hsotg, HCCHAR(i));
		hr->hcsplt[i] = dwc2_readl(hsotg, HCSPLT(i));
		hr->hcintmsk[i] = dwc2_readl(hsotg, HCINTMSK(i));
		hr->hctsiz[i] = dwc2_readl(hsotg, HCTSIZ(i));
		hr->hcidma[i] = dwc2_readl(hsotg, HCDMA(i));
		hr->hcidmab[i] = dwc2_readl(hsotg, HCDMAB(i));
	}

	hr->hprt0 = dwc2_read_hprt0(hsotg);
	hr->hfir = dwc2_readl(hsotg, HFIR);
	hr->hptxfsiz = dwc2_readl(hsotg, HPTXFSIZ);
	hr->valid = true;
}

/**
 * dwc2_restore_host_registers() - Restore controller host registers.
 * When resuming usb bus, device registers needs to be restored
 * if controller power were disabled.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
void dwc2_restore_host_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hregs_backup *hr;
	int i;

	/* Restore host regs */
	hr = &hsotg->hr_backup;
	if (unlikely(!hr->valid)) {
		printf("%s: no host registers to restore\n", __func__);
		return;
	}
	hr->valid = false;

	dwc2_writel(hsotg, hr->hcfg, HCFG);
	dwc2_writel(hsotg, hr->hflbaddr, HFLBADDR);
	dwc2_writel(hsotg, hr->haintmsk, HAINTMSK);

	for (i = 0; i < hsotg->params.host_channels; ++i) {
		dwc2_writel(hsotg, hr->hcchar[i], HCCHAR(i));
		dwc2_writel(hsotg, hr->hcsplt[i], HCSPLT(i));
		dwc2_writel(hsotg, hr->hcintmsk[i], HCINTMSK(i));
		dwc2_writel(hsotg, hr->hctsiz[i], HCTSIZ(i));
		dwc2_writel(hsotg, hr->hcidma[i], HCDMA(i));
		dwc2_writel(hsotg, hr->hcidmab[i], HCDMAB(i));
	}

	dwc2_writel(hsotg, hr->hprt0, HPRT0);
	dwc2_writel(hsotg, hr->hfir, HFIR);
	dwc2_writel(hsotg, hr->hptxfsiz, HPTXFSIZ);
	hsotg->frame_number = 0;
}

/**
 * dwc2_host_enter_hibernation() - Put controller in Hibernation.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
void dwc2_host_enter_hibernation(struct dwc2_hsotg *hsotg)
{
	unsigned long flags;
	u32 hprt0;
	u32 gusbcfg;
	u32 gpwrdn;

	dwc2_backup_global_registers(hsotg);
	dwc2_backup_host_registers(hsotg);

	/* Enter USB Suspend Mode */
	hprt0 = dwc2_readl(hsotg, HPRT0);
	hprt0 |= HPRT0_SUSP;
	hprt0 &= ~HPRT0_ENA;
	dwc2_writel(hsotg, hprt0, HPRT0);

	/* Wait for the HPRT0.PrtSusp register field to be set */
	if (dwc2_hsotg_wait_bit_set(hsotg, HPRT0, HPRT0_SUSP, 5000))
		printf("Suspend wasn't generated\n");

	/*
	 * We need to disable interrupts to prevent servicing of any IRQ
	 * during going to hibernation
	 */
	usb_spin_lock_irqsave(&hsotg->lock, flags);
	hsotg->lx_state = DWC2_L2;

	gusbcfg = dwc2_readl(hsotg, GUSBCFG);
	if (gusbcfg & GUSBCFG_ULPI_UTMI_SEL) {
		/* ULPI interface */
		gpwrdn = dwc2_readl(hsotg, GPWRDN);
		gpwrdn |= GPWRDN_PMUACTV;
		dwc2_writel(hsotg, gpwrdn, GPWRDN);
		udelay(10);
	} else {
		/* UTMI+ Interface */
		gpwrdn = dwc2_readl(hsotg, GPWRDN);
		gpwrdn |= GPWRDN_PMUACTV;
		dwc2_writel(hsotg, gpwrdn, GPWRDN);
		udelay(10);
	}

	/* Enable interrupts from wake up logic */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PMUINTSEL;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Unmask host mode interrupts in GPWRDN */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_DISCONN_DET_MSK;
	gpwrdn |= GPWRDN_LNSTSCHG_MSK;
	gpwrdn |= GPWRDN_STS_CHGINT_MSK;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Enable Power Down Clamp */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PWRDNCLMP;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Switch off VDD */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PWRDNSWTCH;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);

	hsotg->hibernated = 1;
	hsotg->bus_suspended = 1;
	usb_spin_unlock_irqrestore(&hsotg->lock, flags);
}

/*
 * dwc2_host_exit_hibernation()
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by Device or Host.
 * @param reset: indicates whether resume is initiated by Reset.
 *
 * Return: non-zero if failed to enter to hibernation.
 *
 * This function is for exiting from Host mode hibernation by
 * Host Initiated Resume/Reset and Device Initiated Remote-Wakeup.
 */
void dwc2_host_exit_hibernation(struct dwc2_hsotg *hsotg, int rem_wakeup,
			       int reset)
{
	u32 gpwrdn;
	u32 hprt0;
	struct dwc2_gregs_backup *gr;
	struct dwc2_hregs_backup *hr;

	gr = &hsotg->gr_backup;
	hr = &hsotg->hr_backup;

	dwc2_hib_restore_common(hsotg, rem_wakeup, 1);
	hsotg->hibernated = 0;

	/*
	 * This step is not described in functional spec but if not wait for
	 * this delay, mismatch interrupts occurred because just after restore
	 * core is in Device mode(gintsts.curmode == 0)
	 */
	mdelay(100);

	/* Clear all pending interupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* De-assert Restore */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_RESTORE;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Restore GUSBCFG, HCFG */
	dwc2_writel(hsotg, gr->gusbcfg, GUSBCFG);
	dwc2_writel(hsotg, hr->hcfg, HCFG);

	/* De-assert Wakeup Logic */
	if (!(rem_wakeup && hsotg->hw_params.snpsid >= DWC2_CORE_REV_4_30a)) {
		gpwrdn = dwc2_readl(hsotg, GPWRDN);
		gpwrdn &= ~GPWRDN_PMUACTV;
		dwc2_writel(hsotg, gpwrdn, GPWRDN);
		udelay(10);
	}

	hprt0 = hr->hprt0;
	hprt0 |= HPRT0_PWR;
	hprt0 &= ~HPRT0_ENA;
	hprt0 &= ~HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);

	hprt0 = hr->hprt0;
	hprt0 |= HPRT0_PWR;
	hprt0 &= ~HPRT0_ENA;
	hprt0 &= ~HPRT0_SUSP;

	if (reset) {
		hprt0 |= HPRT0_RST;
		dwc2_writel(hsotg, hprt0, HPRT0);

		/* Wait for Resume time and then program HPRT again */
		mdelay(60);
		hprt0 &= ~HPRT0_RST;
		dwc2_writel(hsotg, hprt0, HPRT0);
	} else {
		hprt0 |= HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);

		/* De-assert Wakeup Logic */
		if ((rem_wakeup && hsotg->hw_params.snpsid >= DWC2_CORE_REV_4_30a)) {
			gpwrdn = dwc2_readl(hsotg, GPWRDN);
			gpwrdn &= ~GPWRDN_PMUACTV;
			dwc2_writel(hsotg, gpwrdn, GPWRDN);
			udelay(10);
		}

		/* Wait for Resume time and then program HPRT again */
		mdelay(100);
		hprt0 &= ~HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);
	}
	/* Clear all interrupt status */
	hprt0 = dwc2_readl(hsotg, HPRT0);
	hprt0 |= HPRT0_CONNDET;
	hprt0 |= HPRT0_ENACHG;
	hprt0 &= ~HPRT0_ENA;
	dwc2_writel(hsotg, hprt0, HPRT0);

	hprt0 = dwc2_readl(hsotg, HPRT0);

	/* Clear all pending interupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Restore global registers */
	dwc2_restore_global_registers(hsotg);

	/* Restore host registers */
	dwc2_restore_host_registers(hsotg);

	if (rem_wakeup) {
		dwc2_hcd_rem_wakeup(hsotg);
		/*
		 * Change "port_connect_status_change" flag to re-enumerate,
		 * because after exit from hibernation port connection status
		 * is not detected.
		 */
		hsotg->flags.b.port_connect_status_change = 1;
	}

	hsotg->hibernated = 0;
	hsotg->bus_suspended = 0;
	hsotg->lx_state = DWC2_L0;
}

/**
 * dwc2_host_enter_partial_power_down() - Put controller in partial
 * power down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 * Return: non-zero if failed to enter host partial power down.
 *
 * This function is for entering Host mode partial power down.
 */
void dwc2_host_enter_partial_power_down(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;

	/* Put this port in suspend mode. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);
	udelay(5);

	/* Wait for the HPRT0.PrtSusp register field to be set */
	if (dwc2_hsotg_wait_bit_set(hsotg, HPRT0, HPRT0_SUSP, 3000))
		printf("Suspend wasn't generated\n");

	/* Backup all registers */
	dwc2_backup_global_registers(hsotg);

	dwc2_backup_host_registers(hsotg);

	/*
	 * Clear any pending interrupts since dwc2 will not be able to
	 * clear them after entering partial_power_down.
	 */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Set in_ppd flag to 1 as here core enters suspend. */
	hsotg->in_ppd = 1;
	hsotg->lx_state = DWC2_L2;
	hsotg->bus_suspended = true;
}

/*
 * dwc2_host_exit_partial_power_down() - Exit controller from host partial
 * power down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by Reset.
 * @restore: indicates whether need to restore the registers or not.
 *
 * Return: non-zero if failed to exit host partial power down.
 *
 * This function is for exiting from Host mode partial power down.
 */
void dwc2_host_exit_partial_power_down(struct dwc2_hsotg *hsotg,
				      int rem_wakeup, bool restore)
{
	u32 hprt0;

	if (restore) {
		dwc2_restore_global_registers(hsotg);
		dwc2_restore_host_registers(hsotg);
	}

	/* Drive resume signaling and exit suspend mode on the port. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_RES;
	hprt0 &= ~HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);
	udelay(5);

	if (!rem_wakeup) {
		/* Stop driveing resume signaling on the port. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 &= ~HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);

		hsotg->bus_suspended = false;
	} else {
		/* Turn on the port power bit. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_PWR;
		dwc2_writel(hsotg, hprt0, HPRT0);

		/* Connect hcd. */
		dwc2_hcd_connect(hsotg);

		hrtimer_start(&hsotg->wkp_timer, 71*1000);
	}

	/* Set lx_state to and in_ppd to 0 as here core exits from suspend. */
	hsotg->in_ppd = 0;
	hsotg->lx_state = DWC2_L0;
}

/**
 * dwc2_host_enter_clock_gating() - Put controller in clock gating.
 *
 * @hsotg: Programming view of the DWC_otg controller
 *
 * This function is for entering Host mode clock gating.
 */
void dwc2_host_enter_clock_gating(struct dwc2_hsotg *hsotg)
{
	u32 hprt0;

	/* Put this port in suspend mode. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);

	hsotg->bus_suspended = true;
	hsotg->lx_state = DWC2_L2;
}

/**
 * dwc2_host_exit_clock_gating() - Exit controller from clock gating.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by remote wakeup
 *
 * This function is for exiting Host mode clock gating.
 */
void dwc2_host_exit_clock_gating(struct dwc2_hsotg *hsotg, int rem_wakeup)
{
	u32 hprt0;

	/* Drive resume signaling and exit suspend mode on the port. */
	hprt0 = dwc2_read_hprt0(hsotg);
	hprt0 |= HPRT0_RES;
	hprt0 &= ~HPRT0_SUSP;
	dwc2_writel(hsotg, hprt0, HPRT0);
	udelay(5);

	if (!rem_wakeup) {
		/* In case of port resume need to wait for 40 ms */
		msleep(USB_RESUME_TIMEOUT);

		/* Stop driveing resume signaling on the port. */
		hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 &= ~HPRT0_RES;
		dwc2_writel(hsotg, hprt0, HPRT0);

		hsotg->bus_suspended = false;
		hsotg->lx_state = DWC2_L0;
	} else {
		hrtimer_start(&hsotg->wkp_timer, 71*1000);
	}
}
