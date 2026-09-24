#include <soc/base.h>
#include <assert.h>
#include <driver/irq.h>
#include <driver/clk.h>
#include <io.h>

#include <usb/usb.h>
#include <usb/platform.h>

#include "core.h"
#include "hcd.h"
#include "gadget.h"

static void dwc2_get_dr_mode(struct dwc2_hsotg *hsotg)
{
	switch (hsotg->usb_info->config_info.dr_mode) {
	case USB_DR_MODE_HOST: {
		hsotg->dr_mode = USB_DR_MODE_HOST;
		break;
	}
	case USB_DR_MODE_PERIPHERAL: {
		hsotg->dr_mode = USB_DR_MODE_PERIPHERAL;
		break;
	}
	default:{
		hsotg->dr_mode = USB_DR_MODE_OTG;
		break;
	}
	}
}

static void __dwc2_lowlevel_hw_enable(struct dwc2_hsotg *hsotg)
{
	struct usb_phy_ops *phy_ops = hsotg->usb_info->driver_info.phy_ops;

	if (hsotg->cgu_clk)
		clk_enable(hsotg->cgu_clk);
	clk_enable(hsotg->clk);

	if (phy_ops->phy_init)
		phy_ops->phy_init();
}

/**
 * dwc2_lowlevel_hw_enable - enable platform lowlevel hw resources
 * @hsotg: The driver state
 *
 * A wrapper for platform code responsible for controlling
 * low-level USB platform resources (phy, clock, regulators)
 */
void dwc2_lowlevel_hw_enable(struct dwc2_hsotg *hsotg)
{
	__dwc2_lowlevel_hw_enable(hsotg);
	hsotg->ll_hw_enabled = true;
}

static void __dwc2_lowlevel_hw_disable(struct dwc2_hsotg *hsotg)
{
	clk_disable(hsotg->clk);
	if (hsotg->cgu_clk)
		clk_disable(hsotg->cgu_clk);
}

/**
 * dwc2_lowlevel_hw_disable - disable platform lowlevel hw resources
 * @hsotg: The driver state
 *
 * A wrapper for platform code responsible for controlling
 * low-level USB platform resources (phy, clock, regulators)
 */
void dwc2_lowlevel_hw_disable(struct dwc2_hsotg *hsotg)
{
	__dwc2_lowlevel_hw_disable(hsotg);
	hsotg->ll_hw_enabled = false;
}

static void dwc2_lowlevel_hw_init(struct dwc2_hsotg *hsotg)
{
	struct clk *parent_clk;
	struct usb_phy_ops *phy_ops = hsotg->usb_info->driver_info.phy_ops;

	/* Clock */
	assert(hsotg->usb_info->driver_info.clk_name);
	hsotg->clk = clk_get(hsotg->usb_info->driver_info.clk_name);
	assert(hsotg->clk);

	hsotg->cgu_clk = clk_get("cgu_usb");
	if (hsotg->cgu_clk) {
		parent_clk = clk_get("ext1");
		assert(parent_clk);
		clk_set_parent(hsotg->cgu_clk, parent_clk);
		clk_set_rate(hsotg->cgu_clk, 24000000);
	}

	if (phy_ops->phy_reset)
		phy_ops->phy_reset();
}

/**
 * dwc2_driver_remove() - Called when the DWC_otg core is unregistered with the
 * DWC_otg driver
 */
int dwc2_driver_remove(struct dwc2_hsotg *hsotg)
{
	struct dwc2_gregs_backup *gr;

	gr = &hsotg->gr_backup;
	/* Exit Hibernation when driver is removed. */
	if (hsotg->hibernated) {
		if (gr->gotgctl & GOTGCTL_CURMODE_HOST)
			dwc2_exit_hibernation(hsotg, 0, 0, 1);
		else
			dwc2_exit_hibernation(hsotg, 0, 0, 0);
	}

	/* Exit Partial Power Down when driver is removed. */
	if (hsotg->in_ppd) {
		dwc2_exit_partial_power_down(hsotg, 0, true);
	}

	/* Exit clock gating when driver is removed. */
	if (hsotg->params.power_down == DWC2_POWER_DOWN_PARAM_NONE &&
	    hsotg->bus_suspended) {
		if (dwc2_is_device_mode(hsotg))
			dwc2_gadget_exit_clock_gating(hsotg, 0);
		else
			dwc2_host_exit_clock_gating(hsotg, 0);
	}

	if (hsotg->hcd_enabled)
		dwc2_hcd_remove(hsotg);
	if (hsotg->gadget_enabled)
		dwc2_hsotg_remove(hsotg);

	destroy_workqueue(&hsotg->wq_otg);

	if (hsotg->ll_hw_enabled)
		dwc2_lowlevel_hw_disable(hsotg);

#ifdef CONFIG_USB_IRQ_THREAD_MODE
	hsotg->irq_handle_running = 0;
	thread_wakeup(hsotg->irq_handle);
	thread_join(hsotg->irq_handle, NULL);
#endif

	disable_irq(hsotg->usb_info->driver_info.irq);
	release_irq(hsotg->usb_info->driver_info.irq);

	return 0;
}

void usb_common_irq(struct dwc2_hsotg *hsotg)
{
	if (dwc2_handle_common_intr(hsotg) == IRQ_HANDLED)
		return;

#ifdef CONFIG_USB_IS_DEVICE
	if (dwc2_hsotg_irq(hsotg) == IRQ_HANDLED)
		return;
#endif

#ifdef CONFIG_USB_IS_HOST
	if (dwc2_handle_hcd_intr(hsotg) == IRQ_HANDLED)
		return;
#endif
}

void usb_irq_handle(int irq, void *data)
{
	struct dwc2_hsotg *hsotg = (struct dwc2_hsotg *)data;

#ifdef CONFIG_USB_IRQ_THREAD_MODE
	disable_irq(irq);
	thread_wakeup(hsotg->irq_handle);
#else
	usb_common_irq(hsotg);
#endif
}

#ifdef CONFIG_USB_IRQ_THREAD_MODE
int intc_irq_get_src(int irq);

void usb_irq_thread(void *data)
{
	struct dwc2_hsotg *hsotg = (struct dwc2_hsotg *)data;

	while (hsotg->irq_handle_running) {
		thread_wait();

		if (!hsotg->irq_handle_running)
			break;

		while (intc_irq_get_src(IRQ_OTG))
			usb_common_irq(hsotg);

		enable_irq(IRQ_OTG);
	}
}
#endif

int dwc2_driver_probe(struct dwc2_hsotg *hsotg)
{
	int retval;

	spin_lock_init_recursive(&hsotg->lock);

	dwc2_lowlevel_hw_init(hsotg);

	dwc2_lowlevel_hw_enable(hsotg);

	dwc2_get_dr_mode(hsotg);

	/*
	 * Reset before dwc2_get_hwparams() then it could get power-on real
	 * reset value form registers.
	 */
	dwc2_core_reset(hsotg, false);

	/* Detect config values from hardware */
	dwc2_get_hwparams(hsotg);

	/*
	 * For OTG cores, set the force mode bits to reflect the value
	 * of dr_mode. Force mode bits should not be touched at any
	 * other time after this.
	 */
	dwc2_force_dr_mode(hsotg);

	dwc2_init_params(hsotg);

	create_workqueue(&hsotg->wq_otg);

	if (hsotg->dr_mode != USB_DR_MODE_HOST) {
		retval = dwc2_gadget_init(hsotg);
		if (retval)
			goto error;
		hsotg->gadget_enabled = 1;
	}

	if (hsotg->dr_mode != USB_DR_MODE_PERIPHERAL) {
		retval = dwc2_hcd_init(hsotg);
		if (retval) {
			if (hsotg->gadget_enabled)
				dwc2_hsotg_remove(hsotg);
			goto error;
		}
		hsotg->hcd_enabled = 1;
	}

	hsotg->hibernated = 0;

	/* Gadget code manages lowlevel hw on its own */
	if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
		dwc2_lowlevel_hw_disable(hsotg);

#ifdef CONFIG_USB_IRQ_THREAD_MODE
	hsotg->irq_handle_running = 1;
	hsotg->irq_handle = thread_create("usb irq thread", 8192, usb_irq_thread, hsotg);
	//thread_set_priority(hsotg->irq_handle, OS_priority_high);
#endif
	request_irq(hsotg->usb_info->driver_info.irq, IRQ_TYPE_NONE, usb_irq_handle, "USB_IRQ_HANDLE", hsotg);

	return 0;

error:
	dwc2_lowlevel_hw_disable(hsotg);
	return retval;
}

void dwc2_suspend(struct dwc2_hsotg *dwc2)
{
	struct usb_phy_ops *phy_ops = dwc2->usb_info->driver_info.phy_ops;

	if (dwc2_is_device_mode(dwc2)) {	/* device mode */
		if (get_gadget_enabled(dwc2)) {
			u32 speed;

			switch (call_gadget_get_speed(dwc2)) {
				case USB_SPEED_LOW:
					speed = HPRT0_SPD_LOW_SPEED;
					break;
				case USB_SPEED_FULL:
					speed = HPRT0_SPD_FULL_SPEED;
					break;
				case USB_SPEED_HIGH:
				default:
					speed = HPRT0_SPD_HIGH_SPEED;
					break;
			}

			if (phy_ops->phy_set_wakeup)
				phy_ops->phy_set_wakeup(speed + 1);

			dwc2->wakeup_enable = 1;
			if (phy_ops->phy_suspend)
				phy_ops->phy_suspend(1, 1);
		} else {
			if (phy_ops->phy_suspend)
				phy_ops->phy_suspend(1, 0);

			if (dwc2->ll_hw_enabled) {
				__dwc2_lowlevel_hw_disable(dwc2);
				dwc2->phy_off_for_suspend = true;
			}
		}
	} else {	/* host mode */
		if (get_host_connect_status(dwc2) == 0)
			dwc2_vbus_supply_exit(dwc2);

		if (dwc2->vbus_supply_enabled) {
			u32 hprt = dwc2_readl(dwc2, HPRT0);
			u32 prtspd = (hprt & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
			if (phy_ops->phy_set_wakeup)
				phy_ops->phy_set_wakeup(prtspd + 1);

			dwc2->wakeup_enable = 1;
			if (phy_ops->phy_suspend)
				phy_ops->phy_suspend(1, 1);
		} else {
			if (phy_ops->phy_suspend)
				phy_ops->phy_suspend(1, 0);

			if (dwc2->ll_hw_enabled) {
				__dwc2_lowlevel_hw_disable(dwc2);
				dwc2->phy_off_for_suspend = true;
			}
		}
	}
}

void dwc2_resume(struct dwc2_hsotg *dwc2)
{
	struct usb_phy_ops *phy_ops = dwc2->usb_info->driver_info.phy_ops;

	if (dwc2->phy_off_for_suspend) {
		dwc2->phy_off_for_suspend = false;

		if (dwc2->ll_hw_enabled)
			__dwc2_lowlevel_hw_enable(dwc2);
	}

	if (dwc2->wakeup_enable) {
		if (phy_ops->phy_suspend)
			phy_ops->phy_suspend(0, 1);

		dwc2->wakeup_enable = 0;
	} else {
		if (phy_ops->phy_suspend)
			phy_ops->phy_suspend(0, 0);
	}

	if (phy_ops->phy_set_wakeup)
		phy_ops->phy_set_wakeup(0);

	if (dwc2_is_host_mode(dwc2)) {
		if (get_host_connect_status(dwc2) == 0)
			dwc2_vbus_supply_init(dwc2);
	}
}


/*-------------------------------------------------------------------------*/

static struct dwc2_hsotg *soc_hsotg;
static int soc_usb_num;

#ifdef CONFIG_USB_IS_DEVICE
static usb_power_callback_t soc_usb_power_callback;
static usb_state_callback_t soc_usb_state_callback;
#endif

int usb_core_init(void)
{
	int ret = 0, i = 0;
	soc_usb_num = get_soc_usb_num();

	assert(soc_usb_num);
	soc_hsotg = (struct dwc2_hsotg *)malloc(soc_usb_num * sizeof(struct dwc2_hsotg));

	assert(soc_hsotg);
	memset(soc_hsotg, 0, soc_usb_num * sizeof(struct dwc2_hsotg));

#ifdef CONFIG_USB_IS_DEVICE
	/* Currently, only the USB0 supports device mode. */
	soc_hsotg[0].gadget.power_callback = soc_usb_power_callback;
	soc_hsotg[0].gadget.state_callback = soc_usb_state_callback;
#endif

	for (i = 0; i < soc_usb_num; i++) {
		assert(soc_usb_info[i].driver_info.phy_ops);

		soc_hsotg[i].regs = soc_usb_info[i].driver_info.regs;
		soc_hsotg[i].usb_info = &soc_usb_info[i];

		ret = dwc2_driver_probe(&soc_hsotg[i]);
		assert(!ret);
	}

	return ret;
}

void usb_core_exit(void)
{
	int i = 0;

	assert(soc_hsotg);

	for (i = 0; i < soc_usb_num; i++) {
		dwc2_driver_remove(&soc_hsotg[i]);
	}

	free(soc_hsotg);
	soc_hsotg = NULL;

#ifdef CONFIG_USB_IS_DEVICE
	soc_usb_power_callback = NULL;
	soc_usb_state_callback = NULL;
#endif
}

void usb_core_suspend(void)
{
	int i = 0;

	assert(soc_hsotg);

	for (i = 0; i < soc_usb_num; i++) {
		dwc2_suspend(&soc_hsotg[i]);
	}
}

void usb_core_resume(void)
{
	int i = 0;

	assert(soc_hsotg);

	for (i = 0; i < soc_usb_num; i++) {
		dwc2_resume(&soc_hsotg[i]);
	}
}


#ifdef CONFIG_USB_IS_DEVICE

void usb_device_set_state_callback(usb_state_callback_t state_cb)
{
	soc_usb_state_callback = state_cb;
}
void usb_device_set_power_callback(usb_power_callback_t power_cb)
{
	soc_usb_power_callback = power_cb;
}

void usb_device_vbus_status_update(int status)
{
	usb_udc_vbus_handler(&soc_hsotg[0].gadget, status);
}

#endif