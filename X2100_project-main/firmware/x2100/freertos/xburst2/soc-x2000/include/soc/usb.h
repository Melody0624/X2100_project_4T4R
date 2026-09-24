#ifndef _SOC_USB_H_
#define _SOC_USB_H_

enum otg_phy_mode {
    OTG_PHY_MODE_HOST,
    OTG_PHY_MODE_PERIPHERAL,
    OTG_PHY_MODE_OTG,
};

extern int otg_phy_set_mode(enum otg_phy_mode mode);

#endif /* _SOC_USB_H_ */