#include <soc/cpm.h>
#include <soc/base.h>
#include <delay.h>
#include <assert.h>

#include <usb/usb.h>
#include <usb/platform.h>

#define OPCR_SPENDN0_BIT			7
#define OPCR_GATE_USBPHY_CLK_BIT	23
#define SRBC_USB_SR					12

#define USBRDT_RESUME_IRQ_ENABLE			31
#define USBRDT_RESUME_CLEAR_IRQ				30
#define USBRDT_RESUME_SPEED					28
#define USBRDT_RESUME_STATUS				27

#define USBRDT_IDDIG_EN                     BIT(24)
#define USBRDT_IDDIG_REG                    BIT(23)

#define USB_PHY_IOBASE    0x10078000
#define usb_phy_inl(off)             inl(USB_PHY_IOBASE + (off))
#define usb_phy_outl(val,off)        outl(val,USB_PHY_IOBASE + (off))

static unsigned int x2000_set_wakeup;

enum usb_device_power otg_phy_power_detect(void)
{
	return USB_POWER_UNKNOWN;
}

static void otg_phy_set_wakeup(int enabled)
{
    x2000_set_wakeup = enabled;
}

void otg_phy_suspend(int suspend, int wakeup)
{
	unsigned long value;

	if (suspend) {
#ifndef CONFIG_USB_DISABLE_VBUS
		/* VBUS voltage level detection power down. */
		value = usb_phy_inl(0x108);
		value |= 1 << 3;
		usb_phy_outl(value, 0x108);
#endif

		/* disable full/low speed driver at the receiver */
		value = usb_phy_inl(0x100);
		value &= ~(1 << 6);
		usb_phy_outl(value, 0x100);

		cpm_clear_bit(OPCR_SPENDN0_BIT, CPM_OPCR);

		if (wakeup && x2000_set_wakeup) {
			/* set wakeup speed */
			value = cpm_inl(CPM_USBRDT);
			value &= ~(0x3 << USBRDT_RESUME_SPEED);
			value |= ((x2000_set_wakeup - 1) & 0x3) << USBRDT_RESUME_SPEED;
			cpm_outl(value, CPM_USBRDT);

			cpm_clear_bit(USBRDT_RESUME_CLEAR_IRQ, CPM_USBRDT);
			cpm_set_bit(USBRDT_RESUME_IRQ_ENABLE, CPM_USBRDT);
		}
	} else {
		if (wakeup && x2000_set_wakeup)
			cpm_clear_bit(USBRDT_RESUME_IRQ_ENABLE, CPM_USBRDT);
		cpm_set_bit(OPCR_SPENDN0_BIT, CPM_OPCR);

		/* enable full/low speed driver at the receiver */
		value = usb_phy_inl(0x100);
		value |= 1 << 6;
		usb_phy_outl(value, 0x100);

#ifndef CONFIG_USB_DISABLE_VBUS
		/* VBUS voltage level detection power on. */
		value = usb_phy_inl(0x108);
		value &= ~(1 << 3);
		usb_phy_outl(value, 0x108);
#endif
	}
}

int otg_phy_resume_status(void)
{
	if (cpm_test_bit(USBRDT_RESUME_STATUS,CPM_USBRDT)) {
		if (cpm_test_bit(USBRDT_RESUME_IRQ_ENABLE,CPM_USBRDT))
			panic("ERROR: usb phy resume irq enable\n");

		cpm_set_bit(USBRDT_RESUME_CLEAR_IRQ, CPM_USBRDT);
		while (cpm_test_bit(USBRDT_RESUME_STATUS,CPM_USBRDT));
		cpm_clear_bit(USBRDT_RESUME_CLEAR_IRQ, CPM_USBRDT);
		return 1;
	}

	return 0;
}

void otg_phy_reset(void)
{
	cpm_set_bit(SRBC_USB_SR, CPM_SRBC);

	cpm_clear_bit(OPCR_GATE_USBPHY_CLK_BIT, CPM_OPCR);
}

void otg_phy_init(void)
{
	unsigned long value;

	/* set phy power reset */
	value = cpm_inl(CPM_USBPCR);
	value |= 1 << 31; // work as OTG
	value &= ~(3 << 28); // idpullup from otg controller
	value |= 1 << 22; // power on reset
	cpm_outl(value, CPM_USBPCR);

	/* set phy port reset */
	value = cpm_inl(CPM_USBPCR1);
	value |= 1 << 21;
	cpm_outl(value, CPM_USBPCR1);

	udelay(10);

	/* clear phy power reset */
	value = cpm_inl(CPM_USBPCR);
	value &= ~(1 << 22);
	cpm_outl(value, CPM_USBPCR);

	udelay(10);

	/* clear phy suspend */
	cpm_set_bit(OPCR_SPENDN0_BIT, CPM_OPCR);

	/* wait phy PLL */
	udelay(1000);

	/* clear phy port reset */
	value = cpm_inl(CPM_USBPCR1);
	value &= ~(1 << 21);
	cpm_outl(value, CPM_USBPCR1);

	/* wait phy operational */
	udelay(10);

	/* clear controller reset */
	cpm_clear_bit(SRBC_USB_SR, CPM_SRBC);

	/* DP DM pull-down by the controller */
	value = cpm_inl(CPM_USBPCR1);
	value |= 1 << 28; // DM pull down by controller
	value |= 1 << 29; // DP pull down by controller
	value |= 1 << 30; // Chirp K or SE0 resume enable
	cpm_outl(value, CPM_USBPCR1);

	/* Hs Rx squelch and reference voltage */
	value = usb_phy_inl(0x64);
	value &= ~(0xF << 3);
	value |= 0x2 << 3;
	usb_phy_outl(value, 0x64);

#ifdef CONFIG_USB_DISABLE_VBUS
	/* VBUS voltage level detection power down. */
	value = usb_phy_inl(0x108);
	value |= 0x1 << 3;
	usb_phy_outl(value, 0x108);
#endif
}

/* OTG PHY */
struct usb_phy_ops usb0_phy_ops = {
    .phy_init = otg_phy_init,
    .phy_reset = otg_phy_reset,

    .phy_suspend = otg_phy_suspend,
    .phy_resume_status = otg_phy_resume_status,

    .phy_power_detect = otg_phy_power_detect,

    .phy_set_wakeup = otg_phy_set_wakeup,
};

const struct platform_info soc_usb_info[] = {
#ifdef CONFIG_USB0
    {
        /* dwc2 driver info */
        .driver_info.regs = (void *)io_addr(OTG_IOBASE),
        .driver_info.clk_name = "otg",
        .driver_info.irq = IRQ_OTG,
        .driver_info.phy_ops = &usb0_phy_ops,

        /* config from iconfigTool */
#if defined(CONFIG_USB_HOST)
        .config_info.dr_mode = USB_DR_MODE_HOST,
        .config_info.vbus_power = { CONFIG_USB_HOST_VBUS_POWER_PIN },
#elif defined(CONFIG_USB_PERIPHERAL)
        .config_info.dr_mode = USB_DR_MODE_PERIPHERAL,
#else
        .config_info.dr_mode = USB_DR_MODE_OTG,
        .config_info.vbus_power = { CONFIG_USB_HOST_VBUS_POWER_PIN },
#endif

#ifdef CONFIG_USB_DISABLE_VBUS
        .config_info.disable_vbus = true,
#endif

#ifdef CONFIG_USB_FORCE_FULL_SPEED
        .config_info.speed =  DWC2_SPEED_PARAM_FULL,
#endif
    },
#endif /* CONFIG_USB0 */
};

int get_soc_usb_num(void)
{
    return (sizeof(soc_usb_info) / sizeof(struct platform_info));
}

/**
 * otg_phy_set_mode - The software switches the working mode of OTG
 * @dr_mode: Desired mode of OTG operation
 *
 * Returns 0 if success, error code on error
 */
int otg_phy_set_mode(enum otg_phy_mode mode)
{
    unsigned long usbrdt;
    int ret = 0;

    usbrdt = cpm_inl(CPM_USBRDT);

    switch (mode) {
    case OTG_PHY_MODE_PERIPHERAL: {
        usbrdt |= USBRDT_IDDIG_EN;
        usbrdt |= USBRDT_IDDIG_REG;
        /* ID level is high */
        cpm_outl(usbrdt, CPM_USBRDT);
        break;
    }
    case OTG_PHY_MODE_HOST: {
        usbrdt |= USBRDT_IDDIG_EN;
        usbrdt &= ~USBRDT_IDDIG_REG;
        /* ID level is low */
        cpm_outl(usbrdt, CPM_USBRDT);
        break;
    }
    case OTG_PHY_MODE_OTG: {
        /* Confirm the working mode according to the hardware ID PIN */
        usbrdt &= ~(USBRDT_IDDIG_EN | USBRDT_IDDIG_REG);
        cpm_outl(usbrdt, CPM_USBRDT);
        break;
    }
    default:
        ret = -EPERM;
        break;
    }

    return ret;
}