#include <assert.h>
#include <delay.h>

#include "core.h"

/**
 * dwc2_backup_global_registers() - Backup global controller registers.
 * When suspending usb bus, registers needs to be backuped
 * if controller power is disabled once suspended.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
void dwc2_backup_global_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;

	/* Backup global regs */
	gr = &hsotg->gr_backup;

	gr->gotgctl = dwc2_readl(hsotg, GOTGCTL);
	gr->gintmsk = dwc2_readl(hsotg, GINTMSK);
	gr->gahbcfg = dwc2_readl(hsotg, GAHBCFG);
	gr->gusbcfg = dwc2_readl(hsotg, GUSBCFG);
	gr->grxfsiz = dwc2_readl(hsotg, GRXFSIZ);
	gr->gnptxfsiz = dwc2_readl(hsotg, GNPTXFSIZ);
	gr->gdfifocfg = dwc2_readl(hsotg, GDFIFOCFG);
	gr->glpmcfg = dwc2_readl(hsotg, GLPMCFG);
	gr->gi2cctl = dwc2_readl(hsotg, GI2CCTL);
	gr->valid = true;
}

/**
 * dwc2_restore_global_registers() - Restore controller global registers.
 * When resuming usb bus, device registers needs to be restored
 * if controller power were disabled.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
void dwc2_restore_global_registers(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;

	/* Restore global regs */
	gr = &hsotg->gr_backup;
	if (!gr->valid) {
		panic("%s: no global registers to restore\n", __func__);
	}
	gr->valid = false;

	dwc2_writel(hsotg, 0xffffffff, GINTSTS);
	dwc2_writel(hsotg, gr->gotgctl, GOTGCTL);
	dwc2_writel(hsotg, gr->gintmsk, GINTMSK);
	dwc2_writel(hsotg, gr->gusbcfg, GUSBCFG);
	dwc2_writel(hsotg, gr->gahbcfg, GAHBCFG);
	dwc2_writel(hsotg, gr->grxfsiz, GRXFSIZ);
	dwc2_writel(hsotg, gr->gnptxfsiz, GNPTXFSIZ);
	dwc2_writel(hsotg, gr->gdfifocfg, GDFIFOCFG);
	dwc2_writel(hsotg, gr->glpmcfg, GLPMCFG);
	dwc2_writel(hsotg, gr->gi2cctl, GI2CCTL);
}

/**
 * dwc2_exit_partial_power_down() - Exit controller from Partial Power Down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: indicates whether resume is initiated by Reset.
 * @restore: Controller registers need to be restored
 */
void dwc2_exit_partial_power_down(struct dwc2_hsotg *hsotg, int rem_wakeup,
				 bool restore)
{
	struct dwc2_gregs_backup *gr;

	gr = &hsotg->gr_backup;

	/*
	 * Restore host or device regisers with the same mode core enterted
	 * to partial power down by checking "GOTGCTL_CURMODE_HOST" backup
	 * value of the "gotgctl" register.
	 */
	if (gr->gotgctl & GOTGCTL_CURMODE_HOST)
		dwc2_host_exit_partial_power_down(hsotg, rem_wakeup, restore);
	else
		dwc2_gadget_exit_partial_power_down(hsotg, restore);
}

/**
 * dwc2_enter_partial_power_down() - Put controller in Partial Power Down.
 *
 * @hsotg: Programming view of the DWC_otg controller
 */
void dwc2_enter_partial_power_down(struct dwc2_hsotg *hsotg)
{
	if (dwc2_is_host_mode(hsotg))
		dwc2_host_enter_partial_power_down(hsotg);
	else
		dwc2_gadget_enter_partial_power_down(hsotg);
}

/**
 * dwc2_restore_essential_regs() - Restore essiential regs of core.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rmode: Restore mode, enabled in case of remote-wakeup.
 * @is_host: Host or device mode.
 */
static void dwc2_restore_essential_regs(struct dwc2_hsotg *hsotg, int rmode,
					int is_host)
{
	struct dwc2_gregs_backup *gr;
	struct dwc2_dregs_backup *dr;
	struct dwc2_hregs_backup *hr;

	gr = &hsotg->gr_backup;
	dr = &hsotg->dr_backup;
	hr = &hsotg->hr_backup;

	/* Umnask global Interrupt in GAHBCFG and restore it */
	dwc2_writel(hsotg, gr->gahbcfg | GAHBCFG_GLBL_INTR_EN, GAHBCFG);

	/* Clear all pending interupts */
	dwc2_writel(hsotg, 0xffffffff, GINTSTS);

	/* Unmask restore done interrupt */
	dwc2_writel(hsotg, GINTSTS_RESTOREDONE, GINTMSK);

	/* Restore GUSBCFG and HCFG/DCFG */
	dwc2_writel(hsotg, gr->gusbcfg, GUSBCFG);

	if (is_host) {
		dwc2_writel(hsotg, hr->hcfg, HCFG);
		udelay(10);
	} else {
		dwc2_writel(hsotg, dr->dcfg, DCFG);
		udelay(10);
	}
}

/**
 * dwc2_hib_restore_common() - Common part of restore routine.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: Remote-wakeup, enabled in case of remote-wakeup.
 * @is_host: Host or device mode.
 */
void dwc2_hib_restore_common(struct dwc2_hsotg *hsotg, int rem_wakeup,
			     int is_host)
{
	u32 gpwrdn;

	/* Switch-on voltage to the core */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PWRDNSWTCH;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Reset core */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Enable restore from PMU */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_RESTORE;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Disable Power Down Clamp */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PWRDNCLMP;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(50);

	if (!is_host && rem_wakeup)
		udelay(70);

	/* Deassert reset core */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn |= GPWRDN_PWRDNRSTN;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Disable PMU interrupt */
	gpwrdn = dwc2_readl(hsotg, GPWRDN);
	gpwrdn &= ~GPWRDN_PMUINTSEL;
	dwc2_writel(hsotg, gpwrdn, GPWRDN);
	udelay(10);

	/* Set Restore Essential Regs bit in PCGCCTL register */
	dwc2_restore_essential_regs(hsotg, rem_wakeup, is_host);

	/*
	 * Wait For Restore_done Interrupt. This mechanism of polling the
	 * interrupt is introduced to avoid any possible race conditions
	 */
	if (dwc2_hsotg_wait_bit_set(hsotg, GINTSTS, GINTSTS_RESTOREDONE,
				    20000)) {
		printf("%s: Restore Done wasn't generated here\n", __func__);
	} else {
		printf("restore done  generated here\n");
		/*
		 * To avoid restore done interrupt storm after restore is
		 * generated clear GINTSTS_RESTOREDONE bit.
		 */
		dwc2_writel(hsotg, GINTSTS_RESTOREDONE, GINTSTS);
	}
}

/**
 * dwc2_wait_for_mode() - Waits for the controller mode.
 * @hsotg:	Programming view of the DWC_otg controller.
 * @host_mode:	If true, waits for host mode, otherwise device mode.
 */
static void dwc2_wait_for_mode(struct dwc2_hsotg *hsotg,
			       bool host_mode)
{
	u32 i;

	for (i = 0; i < 1000; i++) {
		if (dwc2_is_host_mode(hsotg) == host_mode)
			return;
		msleep(1);
	}
}

/**
 * dwc2_iddig_filter_enabled() - Returns true if the IDDIG debounce
 * filter is enabled.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static bool dwc2_iddig_filter_enabled(struct dwc2_hsotg *hsotg)
{
	u32 gsnpsid;
	u32 ghwcfg4;

	if (!dwc2_hw_is_otg(hsotg))
		return false;

	/* Check if core configuration includes the IDDIG filter. */
	ghwcfg4 = dwc2_readl(hsotg, GHWCFG4);
	if (!(ghwcfg4 & GHWCFG4_IDDIG_FILT_EN))
		return false;

	/*
	 * Check if the IDDIG debounce filter is bypassed. Available
	 * in core version >= 3.10a.
	 */
	gsnpsid = dwc2_readl(hsotg, GSNPSID);
	if (gsnpsid >= DWC2_CORE_REV_3_10a) {
		u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);

		if (gotgctl & GOTGCTL_DBNCE_FLTR_BYPASS)
			return false;
	}

	return true;
}

/*
 * dwc2_enter_hibernation() - Common function to enter hibernation.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @is_host: True if core is in host mode.
 *
 * Return: 0 if successful, negative error code otherwise
 */
void dwc2_enter_hibernation(struct dwc2_hsotg *hsotg, int is_host)
{
	if (is_host)
		dwc2_host_enter_hibernation(hsotg);
	else
		dwc2_gadget_enter_hibernation(hsotg);
}

/*
 * dwc2_exit_hibernation() - Common function to exit from hibernation.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @rem_wakeup: Remote-wakeup, enabled in case of remote-wakeup.
 * @reset: Enabled in case of restore with reset.
 * @is_host: True if core is in host mode.
 *
 * Return: 0 if successful, negative error code otherwise
 */
void dwc2_exit_hibernation(struct dwc2_hsotg *hsotg, int rem_wakeup,
			  int reset, int is_host)
{
	if (is_host)
		dwc2_host_exit_hibernation(hsotg, rem_wakeup, reset);
	else
		dwc2_gadget_exit_hibernation(hsotg, rem_wakeup, reset);
}

/*
 * Do core a soft reset of the core.  Be careful with this because it
 * resets all the internal state machines of the core.
 */
void dwc2_core_reset(struct dwc2_hsotg *hsotg, bool skip_wait)
{
	u32 greset;
	bool wait_for_host_mode = false;

	/*
	 * If the current mode is host, either due to the force mode
	 * bit being set (which persists after core reset) or the
	 * connector id pin, a core soft reset will temporarily reset
	 * the mode to device. A delay from the IDDIG debounce filter
	 * will occur before going back to host mode.
	 *
	 * Determine whether we will go back into host mode after a
	 * reset and account for this delay after the reset.
	 */
	if (dwc2_iddig_filter_enabled(hsotg)) {
		u32 gotgctl = dwc2_readl(hsotg, GOTGCTL);
		u32 gusbcfg = dwc2_readl(hsotg, GUSBCFG);

		if (!(gotgctl & GOTGCTL_CONID_B) ||
		    (gusbcfg & GUSBCFG_FORCEHOSTMODE)) {
			wait_for_host_mode = true;
		}
	}

	/* Core Soft Reset */
	greset = dwc2_readl(hsotg, GRSTCTL);
	greset |= GRSTCTL_CSFTRST;
	dwc2_writel(hsotg, greset, GRSTCTL);

	if (dwc2_hsotg_wait_bit_clear(hsotg, GRSTCTL, GRSTCTL_CSFTRST, 100000))
		panic("%s: HANG! Soft Reset timeout GRSTCTL_CSFTRST\n", __func__);

	/*
	 * Switching from device mode to host mode by disconnecting
	 * device cable core enters and exits form hibernation.
	 * However, the fifo map remains not cleared. It results
	 * to a WARNING (WARNING: CPU: 5 PID: 0 at drivers/usb/dwc2/
	 * gadget.c:307 dwc2_hsotg_init_fifo+0x12/0x152 [dwc2])
	 * if in host mode we disconnect the micro a to b host
	 * cable. Because core reset occurs.
	 * To avoid the WARNING, fifo_map should be cleared
	 * in dwc2_core_reset() function by taking into account configs.
	 * fifo_map must be cleared only if driver is configured in
	 * "CONFIG_USB_DWC2_PERIPHERAL" or "CONFIG_USB_DWC2_DUAL_ROLE"
	 * mode.
	 */
	dwc2_clear_fifo_map(hsotg);

	/* Wait for AHB master IDLE state */
	if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL, GRSTCTL_AHBIDLE, 100000))
		panic("%s: HANG! AHB Idle timeout GRSTCTL GRSTCTL_AHBIDLE\n", __func__);

	if (wait_for_host_mode && !skip_wait)
		dwc2_wait_for_mode(hsotg, true);
}

/**
 * dwc2_force_mode() - Force the mode of the controller.
 *
 * Forcing the mode is needed for two cases:
 *
 * 1) If the dr_mode is set to either HOST or PERIPHERAL we force the
 * controller to stay in a particular mode regardless of ID pin
 * changes. We do this once during probe.
 *
 * 2) During probe we want to read reset values of the hw
 * configuration registers that are only available in either host or
 * device mode. We may need to force the mode if the current mode does
 * not allow us to access the register in the mode that we want.
 *
 * In either case it only makes sense to force the mode if the
 * controller hardware is OTG capable.
 *
 * Checks are done in this function to determine whether doing a force
 * would be valid or not.
 *
 * If a force is done, it requires a IDDIG debounce filter delay if
 * the filter is configured and enabled. We poll the current mode of
 * the controller to account for this delay.
 *
 * @hsotg: Programming view of DWC_otg controller
 * @host: Host mode flag
 */
void dwc2_force_mode(struct dwc2_hsotg *hsotg, bool host)
{
	u32 gusbcfg;
	u32 set;
	u32 clear;

	/*
	 * Force mode has no effect if the hardware is not OTG.
	 */
	if (!dwc2_hw_is_otg(hsotg))
		return;

	/*
	 * If dr_mode is either peripheral or host only, there is no
	 * need to ever force the mode to the opposite mode.
	 */
	assert(!(host && hsotg->dr_mode == USB_DR_MODE_PERIPHERAL));
	assert(!(!host && hsotg->dr_mode == USB_DR_MODE_HOST));

	gusbcfg = dwc2_readl(hsotg, GUSBCFG);

	set = host ? GUSBCFG_FORCEHOSTMODE : GUSBCFG_FORCEDEVMODE;
	clear = host ? GUSBCFG_FORCEDEVMODE : GUSBCFG_FORCEHOSTMODE;

	gusbcfg &= ~clear;
	gusbcfg |= set;
	dwc2_writel(hsotg, gusbcfg, GUSBCFG);

	dwc2_wait_for_mode(hsotg, host);
}

/**
 * dwc2_clear_force_mode() - Clears the force mode bits.
 *
 * After clearing the bits, wait up to 100 ms to account for any
 * potential IDDIG filter delay. We can't know if we expect this delay
 * or not because the value of the connector ID status is affected by
 * the force mode. We only need to call this once during probe if
 * dr_mode == OTG.
 *
 * @hsotg: Programming view of DWC_otg controller
 */
static void dwc2_clear_force_mode(struct dwc2_hsotg *hsotg)
{
	u32 gusbcfg;

	if (!dwc2_hw_is_otg(hsotg))
		return;

	gusbcfg = dwc2_readl(hsotg, GUSBCFG);
	gusbcfg &= ~GUSBCFG_FORCEHOSTMODE;
	gusbcfg &= ~GUSBCFG_FORCEDEVMODE;
	dwc2_writel(hsotg, gusbcfg, GUSBCFG);

	if (dwc2_iddig_filter_enabled(hsotg))
		msleep(100);
}

/*
 * Sets or clears force mode based on the dr_mode parameter.
 */
void dwc2_force_dr_mode(struct dwc2_hsotg *hsotg)
{
	switch (hsotg->dr_mode) {
	case USB_DR_MODE_HOST:
		dwc2_force_mode(hsotg, true);
		break;
	case USB_DR_MODE_PERIPHERAL:
		dwc2_force_mode(hsotg, false);
		break;
	case USB_DR_MODE_OTG:
		dwc2_clear_force_mode(hsotg);
		break;
	default:
		assert(0);
	}
}

/**
 * dwc2_dump_host_registers() - Prints the host registers
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_dump_host_registers(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	u32 *addr;
	int i;

	printf("Host Global Registers\n");
	addr = hsotg->regs + HCFG;
	printf("HCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HCFG));
	addr = hsotg->regs + HFIR;
	printf("HFIR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HFIR));
	addr = hsotg->regs + HFNUM;
	printf("HFNUM	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HFNUM));
	addr = hsotg->regs + HPTXSTS;
	printf("HPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HPTXSTS));
	addr = hsotg->regs + HAINT;
	printf("HAINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HAINT));
	addr = hsotg->regs + HAINTMSK;
	printf("HAINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HAINTMSK));

	addr = hsotg->regs + HPRT0;
	printf("HPRT0	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HPRT0));

	for (i = 0; i < hsotg->params.host_channels; i++) {
		printf("Host Channel %d Specific Registers\n", i);
		addr = hsotg->regs + HCCHAR(i);
		printf("HCCHAR	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCCHAR(i)));
		addr = hsotg->regs + HCSPLT(i);
		printf("HCSPLT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCSPLT(i)));
		addr = hsotg->regs + HCINT(i);
		printf("HCINT	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCINT(i)));
		addr = hsotg->regs + HCINTMSK(i);
		printf("HCINTMSK	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCINTMSK(i)));
		addr = hsotg->regs + HCTSIZ(i);
		printf("HCTSIZ	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCTSIZ(i)));
		addr = hsotg->regs + HCDMA(i);
		printf("HCDMA	 @0x%08lX : 0x%08X\n",
			(unsigned long)addr, dwc2_readl(hsotg, HCDMA(i)));
	}
#endif
}

/**
 * dwc2_dump_global_registers() - Prints the core global registers
 *
 * @hsotg: Programming view of DWC_otg controller
 *
 * NOTE: This function will be removed once the peripheral controller code
 * is integrated and the driver is stable
 */
void dwc2_dump_global_registers(struct dwc2_hsotg *hsotg)
{
#ifdef DEBUG
	u32 *addr;

	printf("Core Global Registers\n");
	addr = hsotg->regs + GOTGCTL;
	printf("GOTGCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GOTGCTL));
	addr = hsotg->regs + GOTGINT;
	printf("GOTGINT	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GOTGINT));
	addr = hsotg->regs + GAHBCFG;
	printf("GAHBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GAHBCFG));
	addr = hsotg->regs + GUSBCFG;
	printf("GUSBCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GUSBCFG));
	addr = hsotg->regs + GRSTCTL;
	printf("GRSTCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GRSTCTL));
	addr = hsotg->regs + GINTSTS;
	printf("GINTSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GINTSTS));
	addr = hsotg->regs + GINTMSK;
	printf("GINTMSK	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GINTMSK));
	addr = hsotg->regs + GRXSTSR;
	printf("GRXSTSR	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GRXSTSR));
	addr = hsotg->regs + GRXFSIZ;
	printf("GRXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GRXFSIZ));
	addr = hsotg->regs + GNPTXFSIZ;
	printf("GNPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GNPTXFSIZ));
	addr = hsotg->regs + GNPTXSTS;
	printf("GNPTXSTS	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GNPTXSTS));
	addr = hsotg->regs + GI2CCTL;
	printf("GI2CCTL	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GI2CCTL));
	addr = hsotg->regs + GUID;
	printf("GUID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GUID));
	addr = hsotg->regs + GSNPSID;
	printf("GSNPSID	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GSNPSID));
	addr = hsotg->regs + GHWCFG1;
	printf("GHWCFG1	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG1));
	addr = hsotg->regs + GHWCFG2;
	printf("GHWCFG2	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG2));
	addr = hsotg->regs + GHWCFG3;
	printf("GHWCFG3	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG3));
	addr = hsotg->regs + GHWCFG4;
	printf("GHWCFG4	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GHWCFG4));
	addr = hsotg->regs + GLPMCFG;
	printf("GLPMCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GLPMCFG));
	addr = hsotg->regs + GPWRDN;
	printf("GPWRDN	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GPWRDN));
	addr = hsotg->regs + GDFIFOCFG;
	printf("GDFIFOCFG	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, GDFIFOCFG));
	addr = hsotg->regs + HPTXFSIZ;
	printf("HPTXFSIZ	 @0x%08lX : 0x%08X\n",
		(unsigned long)addr, dwc2_readl(hsotg, HPTXFSIZ));
#endif
}

/**
 * dwc2_flush_tx_fifo() - Flushes a Tx FIFO
 *
 * @hsotg: Programming view of DWC_otg controller
 * @num:   Tx FIFO to flush
 */
void dwc2_flush_tx_fifo(struct dwc2_hsotg *hsotg, const int num)
{
	u32 greset;

	/* Wait for AHB master IDLE state */
	if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL, GRSTCTL_AHBIDLE, 10000))
		printf("%s:  HANG! AHB Idle GRSCTL\n", __func__);

	greset = GRSTCTL_TXFFLSH;
	greset |= num << GRSTCTL_TXFNUM_SHIFT & GRSTCTL_TXFNUM_MASK;
	dwc2_writel(hsotg, greset, GRSTCTL);

	if (dwc2_hsotg_wait_bit_clear(hsotg, GRSTCTL, GRSTCTL_TXFFLSH, 10000))
		printf("%s:  HANG! timeout GRSTCTL GRSTCTL_TXFFLSH\n", __func__);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

/**
 * dwc2_flush_rx_fifo() - Flushes the Rx FIFO
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_flush_rx_fifo(struct dwc2_hsotg *hsotg)
{
	u32 greset;

	/* Wait for AHB master IDLE state */
	if (dwc2_hsotg_wait_bit_set(hsotg, GRSTCTL, GRSTCTL_AHBIDLE, 10000))
		printf("%s:  HANG! AHB Idle GRSCTL\n", __func__);

	greset = GRSTCTL_RXFFLSH;
	dwc2_writel(hsotg, greset, GRSTCTL);

	/* Wait for RxFIFO flush done */
	if (dwc2_hsotg_wait_bit_clear(hsotg, GRSTCTL, GRSTCTL_RXFFLSH, 10000))
		printf("%s: HANG! timeout GRSTCTL GRSTCTL_RXFFLSH\n", __func__);

	/* Wait for at least 3 PHY Clocks */
	udelay(1);
}

bool dwc2_is_controller_alive(struct dwc2_hsotg *hsotg)
{
	if (dwc2_readl(hsotg, GSNPSID) == 0xffffffff)
		return false;
	else
		return true;
}

/**
 * dwc2_enable_global_interrupts() - Enables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_enable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg, GAHBCFG);
	ahbcfg |= GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(hsotg, ahbcfg, GAHBCFG);
}

/**
 * dwc2_disable_global_interrupts() - Disables the controller's Global
 * Interrupt in the AHB Config register
 *
 * @hsotg: Programming view of DWC_otg controller
 */
void dwc2_disable_global_interrupts(struct dwc2_hsotg *hsotg)
{
	u32 ahbcfg = dwc2_readl(hsotg, GAHBCFG);
	ahbcfg &= ~GAHBCFG_GLBL_INTR_EN;
	dwc2_writel(hsotg, ahbcfg, GAHBCFG);
}

/* Returns the controller's GHWCFG2.OTG_MODE. */
unsigned int dwc2_op_mode(struct dwc2_hsotg *hsotg)
{
	u32 ghwcfg2 = dwc2_readl(hsotg, GHWCFG2);

	return (ghwcfg2 & GHWCFG2_OP_MODE_MASK) >>
		GHWCFG2_OP_MODE_SHIFT;
}

/* Returns true if the controller is capable of DRD. */
bool dwc2_hw_is_otg(struct dwc2_hsotg *hsotg)
{
	unsigned int op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_HNP_SRP_CAPABLE) ||
		(op_mode == GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE) ||
		(op_mode == GHWCFG2_OP_MODE_NO_HNP_SRP_CAPABLE);
}

/* Returns true if the controller is host-only. */
bool dwc2_hw_is_host(struct dwc2_hsotg *hsotg)
{
	unsigned int op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_HOST) ||
		(op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_HOST);
}

/* Returns true if the controller is device-only. */
bool dwc2_hw_is_device(struct dwc2_hsotg *hsotg)
{
	unsigned int op_mode = dwc2_op_mode(hsotg);

	return (op_mode == GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE) ||
		(op_mode == GHWCFG2_OP_MODE_NO_SRP_CAPABLE_DEVICE);
}

/**
 * dwc2_hsotg_wait_bit_set - Waits for bit to be set.
 * @hsotg: Programming view of DWC_otg controller.
 * @offset: Register's offset where bit/bits must be set.
 * @mask: Mask of the bit/bits which must be set.
 * @timeout: Timeout to wait.
 *
 * Return: 0 if bit/bits are set or -ETIMEDOUT in case of timeout.
 */
int dwc2_hsotg_wait_bit_set(struct dwc2_hsotg *hsotg, u32 offset, u32 mask,
			    u32 timeout)
{
	u32 i;

	for (i = 0; i < timeout; i++) {
		if (dwc2_readl(hsotg, offset) & mask)
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

/**
 * dwc2_hsotg_wait_bit_clear - Waits for bit to be clear.
 * @hsotg: Programming view of DWC_otg controller.
 * @offset: Register's offset where bit/bits must be set.
 * @mask: Mask of the bit/bits which must be set.
 * @timeout: Timeout to wait.
 *
 * Return: 0 if bit/bits are set or -ETIMEDOUT in case of timeout.
 */
int dwc2_hsotg_wait_bit_clear(struct dwc2_hsotg *hsotg, u32 offset, u32 mask,
			      u32 timeout)
{
	u32 i;

	for (i = 0; i < timeout; i++) {
		if (!(dwc2_readl(hsotg, offset) & mask))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}

/*
 * Initializes the FSLSPClkSel field of the HCFG register depending on the
 * PHY type
 */
void dwc2_init_fs_ls_pclk_sel(struct dwc2_hsotg *hsotg)
{
	u32 hcfg, val;

	if ((hsotg->hw_params.hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI &&
	     hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED &&
	     hsotg->params.ulpi_fs_ls) ||
	    hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS) {
		/* Full speed PHY */
		val = HCFG_FSLSPCLKSEL_48_MHZ;
	} else {
		/* High speed PHY running at full speed or high speed */
		val = HCFG_FSLSPCLKSEL_30_60_MHZ;
	}

	hcfg = dwc2_readl(hsotg, HCFG);
	hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
	hcfg |= val << HCFG_FSLSPCLKSEL_SHIFT;
	dwc2_writel(hsotg, hcfg, HCFG);
}

static int dwc2_fs_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg;
	int retval = 0;

	/*
	 * core_init() is now called on every switch so only call the
	 * following for the first time through
	 */
	if (select_phy) {
		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		if (!(usbcfg & GUSBCFG_PHYSEL)) {
			usbcfg |= GUSBCFG_PHYSEL;
			dwc2_writel(hsotg, usbcfg, GUSBCFG);

			/* Reset after a PHY select */
			dwc2_core_reset(hsotg, false);
		}
	}

	/*
	 * Program DCFG.DevSpd or HCFG.FSLSPclkSel to 48Mhz in FS. Also
	 * do this on HNP Dev/Host mode switches (done in dev_init and
	 * host_init).
	 */
	if (dwc2_is_host_mode(hsotg))
		dwc2_init_fs_ls_pclk_sel(hsotg);

	return retval;
}

static int dwc2_hs_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg, usbcfg_old;
	int retval = 0;

	if (!select_phy)
		return 0;

	usbcfg = dwc2_readl(hsotg, GUSBCFG);
	usbcfg_old = usbcfg;

	switch (hsotg->params.phy_type) {
	case DWC2_PHY_TYPE_PARAM_ULPI:
		/* ULPI interface */
		usbcfg |= GUSBCFG_ULPI_UTMI_SEL;
		usbcfg &= ~(GUSBCFG_PHYIF16 | GUSBCFG_DDRSEL);
		if (hsotg->params.phy_ulpi_ddr)
			usbcfg |= GUSBCFG_DDRSEL;

		/* Set external VBUS indicator as needed. */
		if (hsotg->params.oc_disable)
			usbcfg |= (GUSBCFG_ULPI_INT_VBUS_IND |
				   GUSBCFG_INDICATORPASSTHROUGH);
		break;
	case DWC2_PHY_TYPE_PARAM_UTMI:
		/* UTMI+ interface */
		usbcfg &= ~(GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16);
		if (hsotg->params.phy_utmi_width == 16)
			usbcfg |= GUSBCFG_PHYIF16;
		break;
	default:
		panic("FS PHY selected at HS!\n");
		break;
	}

	if (usbcfg != usbcfg_old) {
		dwc2_writel(hsotg, usbcfg, GUSBCFG);

		/* Reset after setting the PHY parameters */
		dwc2_core_reset(hsotg, false);
	}

	return retval;
}

static void dwc2_set_turnaround_time(struct dwc2_hsotg *hsotg)
{
	u32 usbcfg;

	if (hsotg->params.phy_type != DWC2_PHY_TYPE_PARAM_UTMI)
		return;

	usbcfg = dwc2_readl(hsotg, GUSBCFG);

	usbcfg &= ~GUSBCFG_USBTRDTIM_MASK;
	if (hsotg->params.phy_utmi_width == 16)
		usbcfg |= 5 << GUSBCFG_USBTRDTIM_SHIFT;
	else
		usbcfg |= 9 << GUSBCFG_USBTRDTIM_SHIFT;

	dwc2_writel(hsotg, usbcfg, GUSBCFG);
}

int dwc2_phy_init(struct dwc2_hsotg *hsotg, bool select_phy)
{
	u32 usbcfg;
	int retval = 0;

	if ((hsotg->params.speed == DWC2_SPEED_PARAM_FULL ||
	     hsotg->params.speed == DWC2_SPEED_PARAM_LOW) &&
	    hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS) {
		/* If FS/LS mode with FS/LS PHY */
		retval = dwc2_fs_phy_init(hsotg, select_phy);
		if (retval)
			return retval;
	} else {
		/* High speed PHY */
		retval = dwc2_hs_phy_init(hsotg, select_phy);
		if (retval)
			return retval;

		if (dwc2_is_device_mode(hsotg))
			dwc2_set_turnaround_time(hsotg);
	}

	if (hsotg->hw_params.hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI &&
	    hsotg->hw_params.fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED &&
	    hsotg->params.ulpi_fs_ls) {
		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		usbcfg |= GUSBCFG_ULPI_FS_LS;
		usbcfg |= GUSBCFG_ULPI_CLK_SUSP_M;
		dwc2_writel(hsotg, usbcfg, GUSBCFG);
	} else {
		usbcfg = dwc2_readl(hsotg, GUSBCFG);
		usbcfg &= ~GUSBCFG_ULPI_FS_LS;
		usbcfg &= ~GUSBCFG_ULPI_CLK_SUSP_M;
		dwc2_writel(hsotg, usbcfg, GUSBCFG);
	}

	return retval;
}
