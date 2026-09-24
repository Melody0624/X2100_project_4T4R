#include "core.h"
#include "hcd.h"

static const char *dwc2_op_state_str(struct dwc2_hsotg *hsotg)
{
	switch (hsotg->op_state) {
	case OTG_STATE_A_HOST:
		return "a_host";
	case OTG_STATE_A_SUSPEND:
		return "a_suspend";
	case OTG_STATE_A_PERIPHERAL:
		return "a_peripheral";
	case OTG_STATE_B_PERIPHERAL:
		return "b_peripheral";
	case OTG_STATE_B_HOST:
		return "b_host";
	default:
		return "unknown";
	}
}

/**
 * dwc2_handle_usb_port_intr - handles OTG PRTINT interrupts.
 * When the PRTINT interrupt fires, there are certain status bits in the Host
 * Port that needs to get cleared.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_usb_port_intr(struct dwc2_hsotg *hsotg)
{
	u32 hprt0 = dwc2_readl(hsotg, HPRT0);

	if (hprt0 & HPRT0_ENACHG) {
		hprt0 &= ~HPRT0_ENA;
		dwc2_writel(hsotg, hprt0, HPRT0);
	}
}

/**
 * dwc2_handle_mode_mismatch_intr() - Logs a mode mismatch warning message
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_mode_mismatch_intr(struct dwc2_hsotg *hsotg)
{
	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_MODEMIS, GINTSTS);

	printf("Mode Mismatch Interrupt: currently in %s mode\n",
		 dwc2_is_host_mode(hsotg) ? "Host" : "Device");
}

/**
 * dwc2_handle_otg_intr() - Handles the OTG Interrupts. It reads the OTG
 * Interrupt Register (GOTGINT) to determine what interrupt has occurred.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_handle_otg_intr(struct dwc2_hsotg *hsotg)
{
	u32 gotgint;
	u32 gotgctl;
	u32 gintmsk;

	gotgint = dwc2_readl(hsotg, GOTGINT);
	gotgctl = dwc2_readl(hsotg, GOTGCTL);
	printf("++OTG Interrupt gotgint=%0x [%s]\n", gotgint, dwc2_op_state_str(hsotg));

	if (gotgint & GOTGINT_SES_END_DET) {
		gotgctl = dwc2_readl(hsotg, GOTGCTL);

		if (dwc2_is_device_mode(hsotg))
			dwc2_hsotg_disconnect(hsotg);

		call_gadget_set_state(hsotg, USB_STATE_NOTATTACHED);
		call_gadget_vbus_draw(hsotg, 0);

		if (hsotg->op_state == OTG_STATE_B_HOST) {
			hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		} else {
			/*
			 * If not B_HOST and Device HNP still set, HNP did
			 * not succeed!
			 */
			if (gotgctl & GOTGCTL_DEVHNPEN) {
				printf("Device Not Connected/Responding!\n");
			}

			/*
			 * If Session End Detected the B-Cable has been
			 * disconnected
			 */
			/* Reset to a clean state */
			hsotg->lx_state = DWC2_L0;
		}

		gotgctl = dwc2_readl(hsotg, GOTGCTL);
		gotgctl &= ~GOTGCTL_DEVHNPEN;
		dwc2_writel(hsotg, gotgctl, GOTGCTL);
	}

	if (gotgint & GOTGINT_SES_REQ_SUC_STS_CHNG) {
		gotgctl = dwc2_readl(hsotg, GOTGCTL);
		if (gotgctl & GOTGCTL_SESREQSCS) {
			/* Clear Session Request */
			gotgctl = dwc2_readl(hsotg, GOTGCTL);
			gotgctl &= ~GOTGCTL_SESREQ;
			dwc2_writel(hsotg, gotgctl, GOTGCTL);
		}
	}

	if (gotgint & GOTGINT_HST_NEG_SUC_STS_CHNG) {
		/*
		 * Print statements during the HNP interrupt handling
		 * can cause it to fail
		 */
		gotgctl = dwc2_readl(hsotg, GOTGCTL);
		/*
		 * WA for 3.00a- HW is not setting cur_mode, even sometimes
		 * this does not help
		 */
		if (hsotg->hw_params.snpsid >= DWC2_CORE_REV_3_00a)
			udelay(100);
		if (gotgctl & GOTGCTL_HSTNEGSCS) {
			if (dwc2_is_host_mode(hsotg)) {
				hsotg->op_state = OTG_STATE_B_HOST;
				/*
				 * Need to disable SOF interrupt immediately.
				 * When switching from device to host, the PCD
				 * interrupt handler won't handle the interrupt
				 * if host mode is already set. The HCD
				 * interrupt handler won't get called if the
				 * HCD state is HALT. This means that the
				 * interrupt does not get handled and Linux
				 * complains loudly.
				 */
				gintmsk = dwc2_readl(hsotg, GINTMSK);
				gintmsk &= ~GINTSTS_SOF;
				dwc2_writel(hsotg, gintmsk, GINTMSK);

				/* Initialize the Core for Host mode */
				dwc2_hcd_start(hsotg);
				hsotg->op_state = OTG_STATE_B_HOST;
			}
		} else {
			gotgctl = dwc2_readl(hsotg, GOTGCTL);
			gotgctl &= ~(GOTGCTL_HNPREQ | GOTGCTL_DEVHNPEN);
			dwc2_writel(hsotg, gotgctl, GOTGCTL);
			printf("Device Not Connected/Responding\n");
		}
	}

	if (gotgint & GOTGINT_HST_NEG_DET) {
		/*
		 * The disconnect interrupt is set at the same time as
		 * Host Negotiation Detected. During the mode switch all
		 * interrupts are cleared so the disconnect interrupt
		 * handler will not get executed.
		 */
		if (dwc2_is_device_mode(hsotg)) {
			dwc2_hcd_disconnect(hsotg, false);
			hsotg->op_state = OTG_STATE_A_PERIPHERAL;
		} else {
			/* Need to disable SOF interrupt immediately */
			gintmsk = dwc2_readl(hsotg, GINTMSK);
			gintmsk &= ~GINTSTS_SOF;
			dwc2_writel(hsotg, gintmsk, GINTMSK);
			dwc2_hcd_start(hsotg);
			hsotg->op_state = OTG_STATE_A_HOST;
		}
	}

	if (gotgint & GOTGINT_A_DEV_TOUT_CHG)
		printf(" ++OTG Interrupt: A-Device Timeout Change++\n");
	if (gotgint & GOTGINT_DBNCE_DONE)
		printf(" ++OTG Interrupt: Debounce Done++\n");

	/* Clear GOTGINT */
	dwc2_writel(hsotg, gotgint, GOTGINT);
}

/**
 * dwc2_handle_conn_id_status_change_intr() - Handles the Connector ID Status
 * Change Interrupt
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * Reads the OTG Interrupt Register (GOTCTL) to determine whether this is a
 * Device to Host Mode transition or a Host to Device Mode transition. This only
 * occurs when the cable is connected/removed from the PHY connector.
 */
static void dwc2_handle_conn_id_status_change_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintmsk;

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_CONIDSTSCHNG, GINTSTS);

	/* Need to disable SOF interrupt immediately */
	gintmsk = dwc2_readl(hsotg, GINTMSK);
	gintmsk &= ~GINTSTS_SOF;
	dwc2_writel(hsotg, gintmsk, GINTMSK);

	printf(" ++Connector ID Status Change Interrupt++  (%s)\n",
		dwc2_is_host_mode(hsotg) ? "Host" : "Device");

	if ((hsotg->usb_info->config_info.dr_mode == USB_DR_MODE_OTG) &&
		 hsotg->wq_otg.active)
			queue_delayed_work(&hsotg->wq_otg, &hsotg->wf_otg, 1*1000*1000);
}

/**
 * dwc2_handle_session_req_intr() - This interrupt indicates that a device is
 * initiating the Session Request Protocol to request the host to turn on bus
 * power so a new session can begin
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * This handler responds by turning on bus power. If the DWC_otg controller is
 * in low power mode, this handler brings the controller out of low power mode
 * before turning on bus power.
 */
static void dwc2_handle_session_req_intr(struct dwc2_hsotg *hsotg)
{
	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_SESSREQINT, GINTSTS);

	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->lx_state == DWC2_L2) {
			if (hsotg->in_ppd) {
				dwc2_exit_partial_power_down(hsotg, 0, true);
			}

			/* Exit gadget mode clock gating. */
			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_gadget_exit_clock_gating(hsotg, 0);
		}

		/*
		 * Report disconnect if there is any previous session
		 * established
		 */
		dwc2_hsotg_disconnect(hsotg);
		call_gadget_set_state(hsotg, USB_STATE_POWERED);

		if (hsotg->wf_power_detect.thread_func)
			queue_work(&hsotg->wq_otg, &hsotg->wf_power_detect);
	} else {
		/* Turn on the port power bit. */
		u32 hprt0 = dwc2_read_hprt0(hsotg);
		hprt0 |= HPRT0_PWR;
		dwc2_writel(hsotg, hprt0, HPRT0);
		/* Connect hcd after port power is set. */
		dwc2_hcd_connect(hsotg);
	}
}

/*
 * This interrupt indicates that the DWC_otg controller has detected a
 * resume or remote wakeup sequence. If the DWC_otg controller is in
 * low power mode, the handler must brings the controller out of low
 * power mode. The controller automatically begins resume signaling.
 * The handler schedules a time to stop resume signaling.
 */
static void dwc2_handle_wakeup_detected_intr(struct dwc2_hsotg *hsotg)
{

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_WKUPINT, GINTSTS);

	if (dwc2_is_device_mode(hsotg)) {
		if (hsotg->lx_state == DWC2_L2) {
			if (hsotg->in_ppd) {
				u32 dctl = dwc2_readl(hsotg, DCTL);
				/* Clear Remote Wakeup Signaling */
				dctl &= ~DCTL_RMTWKUPSIG;
				dwc2_writel(hsotg, dctl, DCTL);
				dwc2_exit_partial_power_down(hsotg, 1, true);
				call_gadget(hsotg, resume);
			}

			/* Exit gadget mode clock gating. */
			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_gadget_exit_clock_gating(hsotg, 0);
		} else {
			/* Change to L0 state */
			hsotg->lx_state = DWC2_L0;
		}
	} else {
		if (hsotg->lx_state == DWC2_L2) {
			if (hsotg->in_ppd) {
				dwc2_exit_partial_power_down(hsotg, 1, true);
			}

			if (hsotg->params.power_down ==
			    DWC2_POWER_DOWN_PARAM_NONE && hsotg->bus_suspended)
				dwc2_host_exit_clock_gating(hsotg, 1);

			/* dwc2_wakeup_detected */
		} else {
			/* Change to L0 state */
			hsotg->lx_state = DWC2_L0;
		}
	}
}

/*
 * This interrupt indicates that a device has been disconnected from the
 * root port
 */
static void dwc2_handle_disconnect_intr(struct dwc2_hsotg *hsotg)
{
	dwc2_writel(hsotg, GINTSTS_DISCONNINT, GINTSTS);

	if (hsotg->op_state == OTG_STATE_A_HOST)
		dwc2_hcd_disconnect(hsotg, false);
}

/*
 * This interrupt indicates that SUSPEND state has been detected on the USB.
 *
 * For HNP the USB Suspend interrupt signals the change from "a_peripheral"
 * to "a_host".
 *
 * When power management is enabled the core will be put in low power mode.
 */
static void dwc2_handle_usb_suspend_intr(struct dwc2_hsotg *hsotg)
{
	u32 dsts;

	/* Clear interrupt */
	dwc2_writel(hsotg, GINTSTS_USBSUSP, GINTSTS);

	if (dwc2_is_device_mode(hsotg)) {
		/*
		 * Check the Device status register to determine if the Suspend
		 * state is active
		 */
		dsts = dwc2_readl(hsotg, DSTS);

		/* Ignore suspend request before enumeration */
		if (call_gadget_get_state(hsotg) <= USB_STATE_POWERED)
			return;

		if (dsts & DSTS_SUSPSTS) {
			switch (hsotg->params.power_down) {
			case DWC2_POWER_DOWN_PARAM_PARTIAL:
				dwc2_enter_partial_power_down(hsotg);

				udelay(100);

				/* Ask phy to be suspended */
				break;
			case DWC2_POWER_DOWN_PARAM_HIBERNATION:
				dwc2_enter_hibernation(hsotg, 0);
				break;
			case DWC2_POWER_DOWN_PARAM_NONE:
				/*
				 * If neither hibernation nor partial power down are supported,
				 * clock gating is used to save power.
				 */
				dwc2_gadget_enter_clock_gating(hsotg);
			}

			/*
			 * Change to L2 (suspend) state before releasing
			 */
			hsotg->lx_state = DWC2_L2;

			/* Call gadget suspend callback */
			call_gadget(hsotg, suspend);
		}
	} else {
		if (hsotg->op_state == OTG_STATE_A_PERIPHERAL) {
			/* Change to L2 (suspend) state */
			hsotg->lx_state = DWC2_L2;
			/* Clear the a_peripheral flag, back to a_host */
			dwc2_hcd_start(hsotg);
			hsotg->op_state = OTG_STATE_A_HOST;
		}
	}
}

#define GINTMSK_COMMON	(GINTSTS_WKUPINT | GINTSTS_SESSREQINT |		\
			 GINTSTS_CONIDSTSCHNG | GINTSTS_OTGINT |	\
			 GINTSTS_MODEMIS | GINTSTS_DISCONNINT |		\
			 GINTSTS_USBSUSP | GINTSTS_PRTINT |		\
			 GINTSTS_LPMTRANRCVD)

/*
 * This function returns the Core Interrupt register
 */
static u32 dwc2_read_common_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintsts;
	u32 gintmsk;
	u32 gahbcfg;
	u32 gintmsk_common = GINTMSK_COMMON;

	gintsts = dwc2_readl(hsotg, GINTSTS);
	gintmsk = dwc2_readl(hsotg, GINTMSK);
	gahbcfg = dwc2_readl(hsotg, GAHBCFG);

	if (gahbcfg & GAHBCFG_GLBL_INTR_EN)
		return gintsts & gintmsk & gintmsk_common;
	else
		return 0;
}

/**
 * dwc_handle_gpwrdn_disc_det() - Handles the gpwrdn disconnect detect.
 * Exits hibernation without restoring registers.
 *
 * @hsotg: Programming view of DWC_otg controller
 * @gpwrdn: GPWRDN register
 */
static inline void dwc_handle_gpwrdn_disc_det(struct dwc2_hsotg *hsotg,
					      u32 gpwrdn)
{
	u32 gpwrdn_tmp;

	/* Switch-on voltage to the core */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PWRDNSWTCH;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Reset core */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Disable Power Down Clamp */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PWRDNCLMP;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Deassert reset core */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp |= GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);
	udelay(5);

	/* Disable PMU interrupt */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PMUINTSEL;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);

	/* De-assert Wakeup Logic */
	gpwrdn_tmp = dwc2_readl(hsotg, GPWRDN);
	gpwrdn_tmp &= ~GPWRDN_PMUACTV;
	dwc2_writel(hsotg, gpwrdn_tmp, GPWRDN);

	hsotg->hibernated = 0;
	hsotg->bus_suspended = 0;

	if (gpwrdn & GPWRDN_IDSTS) {
		hsotg->op_state = OTG_STATE_B_PERIPHERAL;
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hsotg_core_init_disconnected(hsotg, false);
		if (get_gadget_enabled(hsotg))
			dwc2_hsotg_core_connect(hsotg);
	} else {
		hsotg->op_state = OTG_STATE_A_HOST;

		/* Initialize the Core for Host mode */
		dwc2_core_init(hsotg, false);
		dwc2_enable_global_interrupts(hsotg);
		dwc2_hcd_start(hsotg);
	}
}

/*
 * GPWRDN interrupt handler.
 *
 * The GPWRDN interrupts are those that occur in both Host and
 * Device mode while core is in hibernated state.
 */
static void dwc2_handle_gpwrdn_intr(struct dwc2_hsotg *hsotg)
{
	u32 gpwrdn;
	int linestate;

	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	/* clear all interrupt */
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	linestate = (gpwrdn & GPWRDN_LINESTATE_MASK) >> GPWRDN_LINESTATE_SHIFT;

	if ((gpwrdn & GPWRDN_DISCONN_DET) &&
	    (gpwrdn & GPWRDN_DISCONN_DET_MSK) && !linestate) {
		/*
		 * Call disconnect detect function to exit from
		 * hibernation
		 */
		dwc_handle_gpwrdn_disc_det(hsotg, gpwrdn);
	} else if ((gpwrdn & GPWRDN_LNSTSCHG) &&
		   (gpwrdn & GPWRDN_LNSTSCHG_MSK) && linestate) {
		if (hsotg->hw_params.hibernation &&
		    hsotg->hibernated) {
			if (gpwrdn & GPWRDN_IDSTS) {
				dwc2_exit_hibernation(hsotg, 0, 0, 0);
				call_gadget(hsotg, resume);
			} else {
				dwc2_exit_hibernation(hsotg, 1, 0, 1);
			}
		}
	} else if ((gpwrdn & GPWRDN_RST_DET) &&
		   (gpwrdn & GPWRDN_RST_DET_MSK)) {
		if (!linestate) {
			dwc2_exit_hibernation(hsotg, 0, 1, 0);
		}
	} else if ((gpwrdn & GPWRDN_STS_CHGINT) &&
		   (gpwrdn & GPWRDN_STS_CHGINT_MSK)) {
		/*
		 * As GPWRDN_STS_CHGINT exit from hibernation flow is
		 * the same as in GPWRDN_DISCONN_DET flow. Call
		 * disconnect detect helper function to exit from
		 * hibernation.
		 */
		dwc_handle_gpwrdn_disc_det(hsotg, gpwrdn);
	}
}

/*
 * Common interrupt handler
 *
 * The common interrupts are those that occur in both Host and Device mode.
 * This handler handles the following interrupts:
 * - Mode Mismatch Interrupt
 * - OTG Interrupt
 * - Connector ID Status Change Interrupt
 * - Disconnect Interrupt
 * - Session Request Interrupt
 * - Resume / Remote Wakeup Detected Interrupt
 * - Suspend Interrupt
 */
irqreturn_t dwc2_handle_common_intr(struct dwc2_hsotg *hsotg)
{
	u32 gintsts;
	irqreturn_t retval = IRQ_NONE;
	struct usb_phy_ops *phy_ops = hsotg->usb_info->driver_info.phy_ops;

	spin_lock(&hsotg->lock);

	if (unlikely(!dwc2_is_controller_alive(hsotg))) {
		printf("USB Controller is dead\n");
		goto out;
	}

	/* Reading current frame number value in device or host modes. */
	if (dwc2_is_device_mode(hsotg))
		hsotg->frame_number = (dwc2_readl(hsotg, DSTS)
				       & DSTS_SOFFN_MASK) >> DSTS_SOFFN_SHIFT;
	else
		hsotg->frame_number = (dwc2_readl(hsotg, HFNUM)
				       & HFNUM_FRNUM_MASK) >> HFNUM_FRNUM_SHIFT;

	gintsts = dwc2_read_common_intr(hsotg);
	if (gintsts & ~GINTSTS_PRTINT)
		retval = IRQ_HANDLED;

	/* In case of hibernated state gintsts must not work */
	if (hsotg->hibernated) {
		dwc2_handle_gpwrdn_intr(hsotg);
		retval = IRQ_HANDLED;
		goto out;
	}

	if (gintsts & GINTSTS_MODEMIS)
		dwc2_handle_mode_mismatch_intr(hsotg);
	if (gintsts & GINTSTS_OTGINT)
		dwc2_handle_otg_intr(hsotg);
	if (gintsts & GINTSTS_CONIDSTSCHNG)
		dwc2_handle_conn_id_status_change_intr(hsotg);
	if (gintsts & GINTSTS_DISCONNINT)
		dwc2_handle_disconnect_intr(hsotg);
	if (gintsts & GINTSTS_SESSREQINT)
		dwc2_handle_session_req_intr(hsotg);
	if ((gintsts & GINTSTS_WKUPINT) ||
		(phy_ops->phy_resume_status && phy_ops->phy_resume_status()))
			dwc2_handle_wakeup_detected_intr(hsotg);
	if (gintsts & GINTSTS_USBSUSP)
		dwc2_handle_usb_suspend_intr(hsotg);

	if (gintsts & GINTSTS_PRTINT) {
		/*
		 * The port interrupt occurs while in device mode with HPRT0
		 * Port Enable/Disable
		 */
		if (dwc2_is_device_mode(hsotg)) {
			dwc2_handle_usb_port_intr(hsotg);
			retval = IRQ_HANDLED;
		}
	}

out:
	spin_unlock(&hsotg->lock);
	return retval;
}
