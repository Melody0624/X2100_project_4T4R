#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <driver/gpio_pin.h>
#include "drivers/usb/core.h"
#include <soc/irq.h>

struct usb_phy_ops {
    void (*phy_init)(void);
    void (*phy_reset)(void);

    void (*phy_suspend)(int suspend, int wakeup);
    int (*phy_resume_status)(void);
    void (*phy_set_wakeup)(int enabled);

    enum usb_device_power (*phy_power_detect)(void);
};

/* each dwc2 driver info */
struct dwc2_driver_info {
    void *regs;
    char *clk_name;
    int irq;
    struct usb_phy_ops *phy_ops;
};

/* config from iconfigTool */
struct user_config_info {
    enum usb_dr_mode dr_mode;
    bool disable_vbus;
    u8 speed;
    struct gpio_pin vbus_power;
};

struct platform_info {
    struct dwc2_driver_info driver_info;
    struct user_config_info config_info;
};

/* it is defined at cpm_usb.c, depending on the specific soc's usb. */
extern const struct platform_info soc_usb_info[];

int get_soc_usb_num(void);

#endif /* _PLATFORM_H_ */