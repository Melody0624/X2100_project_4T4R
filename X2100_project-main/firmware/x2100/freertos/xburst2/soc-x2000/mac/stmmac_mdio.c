#include <io.h>
#include <os.h>
#include <pthread.h>
#include <driver/gpio.h>
#include <driver/irq.h>

#include <lwip/netifapi.h>

#include "mii.h"
#include "stmmac.h"

#define MII_BUSY 0x00000001
#define MII_WRITE 0x00000002

#define MII_WAIT_TIME	3000

#define PHY_MAX_ADDR	32

static DEFINE_MUTEX(phy_lock);

static int stmmac_mdio_busy_wait(unsigned long ioaddr, unsigned int mii_addr)
{
	int i;

	for (i = 0; i < MII_WAIT_TIME; i++) {
		if (!(readl(ioaddr + mii_addr) & MII_BUSY))
			return 0;
		msleep(1);
	}

	printf("%s: mii wait timeout\n", __func__);

	return -EBUSY;
}

/**
 * stmmac_mdio_read
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr reg bits 15-11
 * @phyreg: MII addr reg bits 10-6
 * Description: it reads data from the MII register from within the phy device.
 * For the 7111 GMAC, we must set the bit 0 in the MII address register while
 * accessing the PHY registers.
 * Fortunately, it seems this has no drawback for the 7109 MAC.
 */
int stmmac_mdio_read(struct stmmac_priv *priv, int phyaddr, int phyreg)
{
	unsigned int mii_address = priv->hw.mii.addr;
	unsigned int mii_data = priv->hw.mii.data;

	mutex_lock(&phy_lock);

	int data;
	u16 regValue = (((phyaddr << 11) & (0x0000F800)) |
			((phyreg << 6) & (0x000007C0)));
	regValue |= MII_BUSY | ((priv->plat->clk_csr & 0xF) << 2);

	if (stmmac_mdio_busy_wait(priv->plat->ioaddr, mii_address))
		goto mdio_read_err;

	writel(regValue, priv->plat->ioaddr + mii_address);

	if (stmmac_mdio_busy_wait(priv->plat->ioaddr, mii_address))
		goto mdio_read_err;

	/* Read the data from the MII data register */
	data = (int)readl(priv->plat->ioaddr + mii_data);

	mutex_unlock(&phy_lock);

	return data;

mdio_read_err:
	mutex_unlock(&phy_lock);
	return -EBUSY;
}

/**
 * stmmac_mdio_write
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr reg bits 15-11
 * @phyreg: MII addr reg bits 10-6
 * @phydata: phy data
 * Description: it writes the data into the MII register from within the device.
 */
int stmmac_mdio_write(struct stmmac_priv *priv, int phyaddr, int phyreg, u16 phydata)
{
	unsigned int mii_address = priv->hw.mii.addr;
	unsigned int mii_data = priv->hw.mii.data;

	mutex_lock(&phy_lock);

	u16 value =
		(((phyaddr << 11) & (0x0000F800)) | ((phyreg << 6) & (0x000007C0)))
		| MII_WRITE;

	value |= MII_BUSY | ((priv->plat->clk_csr & 0xF) << 2);

	/* Wait until any existing MII operation is complete */
	if (stmmac_mdio_busy_wait(priv->plat->ioaddr, mii_address))
		goto mdio_write_err;

	/* Set the MII address register to write */
	writel(phydata, priv->plat->ioaddr + mii_data);
	writel(value, priv->plat->ioaddr + mii_address);

	/* Wait until any existing MII operation is complete */
	if (stmmac_mdio_busy_wait(priv->plat->ioaddr, mii_address))
		goto mdio_write_err;

	mutex_unlock(&phy_lock);

	return 0;

mdio_write_err:
	mutex_unlock(&phy_lock);
	return -EBUSY;
}

/**
 * stmmac_mdio_reset
 * @bus: points to the mii_bus structure
 * Description: reset the MII bus
 */
static void stmmac_mdio_reset(struct stmmac_priv *priv)
{
	unsigned int mii_address = priv->hw.mii.addr;

	if (priv->plat->phy_reset)
		priv->plat->phy_reset();

	/* This is a workaround for problems with the STE101P PHY.
	 * It doesn't complete its reset until at least one clock cycle
	 * on MDC, so perform a dummy mdio read.
	 */
	writel(0, priv->plat->ioaddr + mii_address);
}

int stmmac_mdio_scan(struct stmmac_priv *priv)
{
	int i;
	int found = 0;
	int phy_reg;
	u32 phy_id = 0;

	/* reset mac phy */
	stmmac_mdio_reset(priv);

	/* find mac phy */
	if (priv->plat->phy_addr > 0) {
		phy_reg = stmmac_mdio_read(priv, priv->plat->phy_addr, MII_PHYSID1);
		if (phy_reg < 0)
			goto stmmac_mdio_read_err;

		phy_id = (phy_reg & 0xffff) << 16;

		phy_reg = stmmac_mdio_read(priv, priv->plat->phy_addr, MII_PHYSID2);
		if (phy_reg < 0)
			goto stmmac_mdio_read_err;

		phy_id |= (phy_reg & 0xffff);

		if (phy_id != 0 && phy_id != 0xffffffff) {
			found = 1;
			printf("mac phy found, id 0x%x\n", phy_id);
		}
	} else {
		for (i = 0; i < PHY_MAX_ADDR; i++) {
			phy_reg = stmmac_mdio_read(priv, i, MII_PHYSID1);
			if (phy_reg < 0)
				goto stmmac_mdio_read_err;

			phy_id = (phy_reg & 0xffff) << 16;

			phy_reg = stmmac_mdio_read(priv, i, MII_PHYSID2);
			if (phy_reg < 0)
				goto stmmac_mdio_read_err;

			phy_id |= (phy_reg & 0xffff);

			if (phy_id != 0 && phy_id != 0xffffffff) {
				found = 1;
				priv->plat->phy_addr = i;
				printf("Found mac phy id 0x%08x\n", phy_id);
				break;
			}
		}
	}

	if (!found) {
		printf("No found mac phy !!!\n");
		return -ENODEV;
	}

	return 0;

stmmac_mdio_read_err:
	printf("stmmac_mdio_read fail\n");
	return -EIO;
}

static int phy_update_link(struct stmmac_priv *priv)
{
	int status;
	int phy_addr = priv->plat->phy_addr;

	/* Do a fake read */
	status = stmmac_mdio_read(priv, phy_addr, MII_BMSR);
	if (status < 0)
		return status;

	/* Read link and autonegotiation status */
	status = stmmac_mdio_read(priv, phy_addr, MII_BMSR);
	if (status < 0)
		return status;

	if ((status & BMSR_LSTATUS) == 0)
		priv->phy_status.link = 0;
	else
		priv->phy_status.link = 1;

	return 0;
}

/**
 * phy_interface_mode_is_rgmii - Convenience function for testing if a
 * PHY interface mode is RGMII (all variants)
 * @mode: the phy_interface_t enum
 */
static inline int phy_interface_mode_is_rgmii(phy_interface_t mode)
{
	return mode >= PHY_INTERFACE_MODE_RGMII &&
		mode <= PHY_INTERFACE_MODE_RGMII_TXID;
}

static int phy_read_status(struct stmmac_priv *priv)
{
	int adv;
	int lpa;
	int bmcr;
	int lpagb = 0;
	struct phy_status *phy_status = &priv->phy_status;
	int phy_addr = priv->plat->phy_addr;

	bmcr = stmmac_mdio_read(priv, phy_addr, MII_BMCR);
	if (bmcr < 0)
		return bmcr;

	if (bmcr & BMCR_ANENABLE) {
		if (phy_interface_mode_is_rgmii(priv->plat->interface)) {
			lpagb = stmmac_mdio_read(priv, phy_addr, MII_STAT1000);
			if (lpagb < 0)
				return lpagb;

			adv = stmmac_mdio_read(priv, phy_addr, MII_CTRL1000);
			if (adv < 0)
				return adv;

			lpagb &= adv << 2;
		}

		lpa = stmmac_mdio_read(priv, phy_addr, MII_LPA);
		if (lpa < 0)
			return lpa;

		adv = stmmac_mdio_read(priv, phy_addr, MII_ADVERTISE);
		if (adv < 0)
			return adv;

		lpa &= adv;

		phy_status->speed = SPEED_10;
		phy_status->duplex = DUPLEX_HALF;
		phy_status->pause = phy_status->asym_pause = 0;

		if (lpagb & (LPA_1000FULL | LPA_1000HALF)) {
			phy_status->speed = SPEED_1000;

			if (lpagb & LPA_1000FULL)
				phy_status->duplex = DUPLEX_FULL;
		} else if (lpa & (LPA_100FULL | LPA_100HALF)) {
			phy_status->speed = SPEED_100;

			if (lpa & LPA_100FULL)
				phy_status->duplex = DUPLEX_FULL;
		} else
			if (lpa & LPA_10FULL)
				phy_status->duplex = DUPLEX_FULL;

		if (phy_status->duplex == DUPLEX_FULL){
			phy_status->pause = lpa & LPA_PAUSE_CAP ? 1 : 0;
			phy_status->asym_pause = lpa & LPA_PAUSE_ASYM ? 1 : 0;
		}
	} else {
		if (bmcr & BMCR_FULLDPLX)
			phy_status->duplex = DUPLEX_FULL;
		else
			phy_status->duplex = DUPLEX_HALF;

		if (bmcr & BMCR_SPEED1000)
			phy_status->speed = SPEED_1000;
		else if (bmcr & BMCR_SPEED100)
			phy_status->speed = SPEED_100;
		else
			phy_status->speed = SPEED_10;

		phy_status->pause = phy_status->asym_pause = 0;
	}

	return 0;
}

/**
 * stmmac_hw_fix_mac_speed: callback for speed selection
 * @priv: driver private structure
 * Description: on some platforms (e.g. ST), some HW system configuraton
 * registers have to be set according to the link speed negotiated.
 */
static inline void stmmac_hw_fix_mac_speed(struct stmmac_priv *priv)
{
	if (likely(priv->plat->fix_mac_speed))
		priv->plat->fix_mac_speed(priv, priv->phy_status.speed);
}

/**
 * stmmac_adjust_link
 * @dev: net device structure
 * Description: it adjusts the link parameters.
 */
static void stmmac_adjust_link(struct stmmac_priv *priv)
{
	unsigned long flags;
	int new_state = 0;
	struct phy_status *phy_status = &priv->phy_status;
	unsigned int fc = priv->flow_ctrl, pause = priv->pause;

	spin_lock_irqsave(&priv->lock, flags);

	if (phy_status->link) {
		u32 ctrl = readl(priv->plat->ioaddr + MAC_CTRL_REG);

		/* Now we make sure that we can be in full duplex mode.
		 * If not, we operate in half-duplex mode. */
		if (phy_status->duplex != priv->oldduplex) {
			new_state = 1;
			if (!(phy_status->duplex))
				ctrl &= ~priv->hw.link.duplex;
			else
				ctrl |= priv->hw.link.duplex;
			priv->oldduplex = phy_status->duplex;
		}

		/* Flow Control operation */
		if (phy_status->pause)
			priv->hw.mac->flow_ctrl(priv->plat->ioaddr, phy_status->duplex,
						 fc, pause);

		if (phy_status->speed != priv->speed) {
			new_state = 1;
			switch (phy_status->speed) {
			case 1000:
				ctrl &= ~priv->hw.link.port;
				stmmac_hw_fix_mac_speed(priv);
				break;
			case 100:
			case 10:
				ctrl |= priv->hw.link.port;
				if (phy_status->speed == SPEED_100) {
					ctrl |= priv->hw.link.speed;
				} else {
					ctrl &= ~(priv->hw.link.speed);
				}
				stmmac_hw_fix_mac_speed(priv);
				break;
			default:
				printf("%s: Speed (%d) not 10/100\n", __func__, phy_status->speed);
				break;
			}

			priv->speed = phy_status->speed;
		}

		writel(ctrl, priv->plat->ioaddr + MAC_CTRL_REG);

		if (!priv->oldlink) {
			new_state = 1;
			priv->oldlink = 1;
		}
	} else if (priv->oldlink) {
		new_state = 1;
		priv->oldlink = 0;
		priv->speed = 0;
		priv->oldduplex = -1;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (new_state) {
		struct netif *netif = &priv->netif;

		if (phy_status->link) {
			printf("%c%c%d - Link is Up - %d/%s\n", netif->name[0], netif->name[1],
				netif->num, phy_status->speed,
				DUPLEX_FULL == phy_status->duplex ? "Full" : "Half");
			netifapi_netif_set_link_up(netif);
		} else {
			printf("%c%c%d - Link is Down\n", netif->name[0], netif->name[1], netif->num);
			netifapi_netif_set_link_down(netif);
		}
	}
}

static void phy_state_machine(void *data)
{
	int err = 0;
	struct stmmac_priv *priv = data;
	struct phy_status *phy_status = &priv->phy_status;

	while (priv->phy_running) {
		/* Update the link, but return if there was an error */
		err = phy_update_link(priv);
		if (err < 0) {
			printf("phy_update_link fail\n");
			goto wait_next;
		}

		if (!phy_status->link) {
			stmmac_adjust_link(priv);
			goto wait_next;
		}

		err = phy_read_status(priv);
		if (err < 0) {
			printf("phy_read_status fail\n");
			goto wait_next;
		}

		stmmac_adjust_link(priv);

wait_next:
		if (gpio_is_valid(priv->plat->phy_irq_gpio))
			thread_waiter_wait(&priv->phy_irq_waiter);
		else
			msleep(1000);
	}

	thread_waiter_wakeup(&priv->phy_exit_waiter);
}

static void phy_irq_handler(int irq, void *data)
{
	struct stmmac_priv *priv = data;
	thread_waiter_wakeup(&priv->phy_irq_waiter);
}

int stmmac_init_phy(struct stmmac_priv *priv)
{
	return 0;
}

void stmmac_init_phy_monitor(struct stmmac_priv *priv)
{
	int gpio = priv->plat->phy_irq_gpio;

	priv->oldlink = 0;
	priv->speed = 0;
	priv->oldduplex = -1;

	priv->phy_running = 1;
	thread_waiter_init(&priv->phy_irq_waiter);
	thread_waiter_init(&priv->phy_exit_waiter);

	if (gpio_is_valid(gpio))
		request_irq(gpio_to_irq(gpio), IRQ_TYPE_EDGE_FALLING, phy_irq_handler, "mac_phy", priv);

	priv->phy_thread = thread_create("phy_thread", 4096, phy_state_machine, priv);
}

void stmmac_exit_phy_monitor(struct stmmac_priv *priv)
{
	int gpio = priv->plat->phy_irq_gpio;

	priv->phy_running = 0;

	if (gpio_is_valid(gpio)) {
		disable_irq(gpio_to_irq(gpio));
        release_irq(gpio_to_irq(gpio));
		thread_waiter_wakeup(&priv->phy_irq_waiter);
	}

	thread_waiter_wait(&priv->phy_exit_waiter);
	netifapi_netif_set_link_down(&priv->netif);
}
