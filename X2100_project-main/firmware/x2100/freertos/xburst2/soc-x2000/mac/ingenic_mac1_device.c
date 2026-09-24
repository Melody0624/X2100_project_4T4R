#include <os.h>
#include <shell.h>
#include <assert.h>

#include <lwip/init.h>
#include <lwip/netifapi.h>
#include <lwip/inet.h>
#include <lwip/etharp.h>
#include <lwip/tcpip.h>
#if LWIP_IPV6
#include <lwip/ethip6.h>
#endif

#include <soc/base.h>
#include <soc/cpm.h>
#include <soc/irq.h>

#include <driver/efuse.h>
#include <driver/dtrng.h>

#include <driver/gpio_pin.h>
#include <driver/clk.h>

#include "platfrom_stmmac.h"
#include "stmmac.h"

#include "ingenic_phy_clk.h"

#define JZ_MAC_BASE_ADDR            0x134A0000

#define MAC_INTERFACT_MASK      0x7
#define MAC_INTERFACT_RMII      0x4
#define MAC_INTERFACT_RGMII      0x1
#define CPM_MAC_PHY1            0xe8

#define MAC_CRLT_PORT               GPIO_PORT_B
#define MAC_CRLT_FUNC               GPIO_FUNC_3

#define MAC_RGMII_TX_DELAY_MASK 0xff
#define MAC_RGMII_TX_DELAY      12
#define MAC_RGMII_RX_DELAY_MASK 0xff
#define MAC_RGMII_RX_DELAY      4
#define MAC_RGMII_DELAY_SELECT   0x1
#define MAC_RGMII_TX_SEL_DELAY  19
#define MAC_RGMII_RX_SEL_DELAY  11
#define MAC_SOFT_RESET_MAC           (1 << 3)

#define MAC_RMII_GPIO_PHY_CLK      0xde3300
#define MAC_RGMII_GPIO_PHY_CLK     0xfeff00

#define MAC_TX_CLK_GPIO             (1 << 21)
#define MAC_PHY_CLK_GPIO            (1 << 23)

static struct clk *phy_tx_clk;
static struct clk *mpll_clk;
static struct clk *epll_clk;
static struct clk *mac_clk;

static unsigned int mac_crtl_gpio_mask;

#ifdef CONFIG_X2000_MAC1_RMII_TX_CLK
static unsigned int rmii_tx_clk_rate = CONFIG_X2000_MAC1_RMII_TX_CLK;
#endif

static struct gpio_pin GPIO_PHY_POWER = {CONFIG_X2000_MAC1_PHY_POWER_GPIO};
static struct gpio_pin GPIO_PHY_RESET = {CONFIG_X2000_MAC1_PHY_RESET_GPIO};

static void jz_mac_init(void)
{
    unsigned int cpm_mphyc = 0;

#ifdef CONFIG_X2000_MAC1_RGMII_MODE
    cpm_mphyc |= MAC_INTERFACT_RGMII;

    if (CONFIG_X2000_MAC1_RX_CLK_DELAY) {
        cpm_mphyc |= (((CONFIG_X2000_MAC1_RX_CLK_DELAY - 1) & MAC_RGMII_RX_DELAY_MASK) << MAC_RGMII_RX_DELAY);
        cpm_mphyc |= (MAC_RGMII_DELAY_SELECT << MAC_RGMII_RX_SEL_DELAY);
    }

    if (CONFIG_X2000_MAC1_TX_CLK_DELAY){
        cpm_mphyc |= (((CONFIG_X2000_MAC1_TX_CLK_DELAY - 1) & MAC_RGMII_TX_DELAY_MASK) << MAC_RGMII_TX_DELAY);
        cpm_mphyc |= (MAC_RGMII_DELAY_SELECT << MAC_RGMII_TX_SEL_DELAY);
    }
#else
    cpm_mphyc |= MAC_INTERFACT_RMII;
#endif

    cpm_outl(cpm_mphyc, CPM_MAC_PHY1);

    cpm_mphyc = cpm_inl(CPM_MAC_PHY1);
    cpm_mphyc |= MAC_SOFT_RESET_MAC;
    cpm_outl(cpm_mphyc, CPM_MAC_PHY1);
    msleep(1);
    cpm_mphyc = cpm_inl(CPM_MAC_PHY1);
    cpm_mphyc &= ~MAC_SOFT_RESET_MAC;
    cpm_outl(cpm_mphyc, CPM_MAC_PHY1);
    msleep(1);

    mpll_clk = clk_get("mpll");
    assert(mpll_clk);

    epll_clk = clk_get("epll");
    assert(epll_clk);

    phy_tx_clk = clk_get("cgu_mactxphy1");
    assert(phy_tx_clk);

#ifdef CONFIG_X2000_MAC1_RGMII_MODE
    clk_set_parent(phy_tx_clk, mpll_clk);
    clk_set_rate(phy_tx_clk, 125000000);
    clk_enable(phy_tx_clk);
#else
    /* 100M used*/
    clk_set_parent(phy_tx_clk, epll_clk);
    if (rmii_tx_clk_rate) {
        clk_set_rate(phy_tx_clk, rmii_tx_clk_rate);
        clk_enable(phy_tx_clk);
    }
#endif

#ifdef CONFIG_X2000_MAC1_RGMII_MODE
    mac_crtl_gpio_mask = MAC_RGMII_GPIO_PHY_CLK;
#else
    mac_crtl_gpio_mask = MAC_RMII_GPIO_PHY_CLK;

    if (rmii_tx_clk_rate)
        mac_crtl_gpio_mask |= MAC_TX_CLK_GPIO;
#endif

#ifdef CONFIG_X2000_MAC1_NOT_USE_PHY_CLK
    mac_crtl_gpio_mask &= ~MAC_PHY_CLK_GPIO;
#else
    mac_enable_phy_clk();
#endif

    gpio_port_set_func(MAC_CRLT_PORT, mac_crtl_gpio_mask, MAC_CRLT_FUNC | GPIO_PULL_HIZ);

    gpio_pin_output_enable(GPIO_PHY_POWER);
    gpio_pin_output_disable(GPIO_PHY_RESET);

    mac_clk = clk_get("gate_gmac1");
    assert(mac_clk);

    clk_enable(mac_clk);
    msleep(CONFIG_X2000_MAC1_PHY_POWER_DELAY);
}

static void jz_mac_exit(void)
{
	clk_disable(mac_clk);
    clk_put(mac_clk);

    gpio_pin_output_enable(GPIO_PHY_RESET);
    gpio_pin_output_disable(GPIO_PHY_POWER);

    gpio_port_set_func(MAC_CRLT_PORT, mac_crtl_gpio_mask, GPIO_OUTPUT0);

#ifndef CONFIG_X2000_MAC1_NOT_USE_PHY_CLK
    mac_disable_phy_clk();
#endif

#ifdef CONFIG_X2000_MAC1_RGMII_MODE
    clk_disable(phy_tx_clk);
#else
    if (rmii_tx_clk_rate)
        clk_disable(phy_tx_clk);
#endif

    clk_put(phy_tx_clk);
    clk_put(epll_clk);
    clk_put(mpll_clk);
}

static void mac_phy_reset(void)
{
    if (gpio_is_valid(GPIO_PHY_RESET.gpio)) {
        gpio_pin_output_enable(GPIO_PHY_RESET);
        usleep(CONFIG_X2000_MAC1_PHY_RESET_DELAY);
        gpio_pin_output_disable(GPIO_PHY_RESET);
        usleep(CONFIG_X2000_MAC1_PHY_RESET_DELAY);
    }
}

static void jz_fix_mac_speed(void *priv, unsigned int speed)
{
#ifdef CONFIG_X2000_MAC1_RGMII_MODE
    unsigned long rate = 125000000;
    unsigned long tmp;

    switch (speed) {
    case SPEED_1000:
        rate = 125000000;
        clk_set_parent(phy_tx_clk, mpll_clk);
        break;
    case SPEED_100:
        rate = 25000000;
        clk_set_parent(phy_tx_clk, epll_clk);
        break;
    case SPEED_10:
        rate = 2500000;
        clk_set_parent(phy_tx_clk, epll_clk);
        break;
    default:
        printf("%s: invalid speed %u\n", __func__, speed);
        break;
    }

    clk_set_rate(phy_tx_clk, rate);
    tmp = clk_get_rate(phy_tx_clk);
    if (tmp != rate)
        printf("%s: clk_set_rate error, need rate %ld, real rate %ld\n", __func__, rate, tmp);

#endif
}

static struct plat_stmmacenet_data mac1_device_data = {
    .name = "et",

    .ioaddr = GMAC1_IOBASE,
    .irq = IRQ_GMAC1,

    .phy_addr = CONFIG_X2000_MAC1_PHY_ADDR,
    .phy_irq_gpio = CONFIG_X2000_MAC1_PHY_IRQ_GPIO,

#ifdef CONFIG_X2000_MAC1_RGMII_MODE
    .interface = PHY_INTERFACE_MODE_RGMII,
#else
    .interface = PHY_INTERFACE_MODE_RMII,
#endif

    .mtu = 1500,
    .phy_reset = mac_phy_reset,
    .clk_csr = CONFIG_X2000_MAC1_PHY_MDC,
    .fix_mac_speed  = jz_fix_mac_speed,
};

static err_t ethernetif_linkoutput(struct netif *netif, struct pbuf *p)
{
    struct stmmac_priv *priv = (struct stmmac_priv*)netif->state;

    return stmmac_xmit(priv, p);
}


static err_t eth_netif_device_init(struct netif *netif)
{
    int err;
    struct stmmac_priv *priv = (struct stmmac_priv*)netif->state;

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

    netif->name[0] = priv->plat->name[0];
    netif->name[1] = priv->plat->name[1];

    /* set hw address to 6 */
    netif->hwaddr_len   = 6;
    /* maximum transfer unit */
    netif->mtu          = priv->plat->mtu;

    /* set linkoutput */
    netif->linkoutput   = ethernetif_linkoutput;

    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

#if LWIP_IGMP
    netif->flags |= NETIF_FLAG_IGMP;
#endif

    netif->hwaddr[0] = priv->plat->hw_addr[0];
    netif->hwaddr[1] = priv->plat->hw_addr[1];
    netif->hwaddr[2] = priv->plat->hw_addr[2];
    netif->hwaddr[3] = priv->plat->hw_addr[3];
    netif->hwaddr[4] = priv->plat->hw_addr[4];
    netif->hwaddr[5] = priv->plat->hw_addr[5];

    /* set output */
    netif->output       = etharp_output;

#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
    netif->ip6_autoconfig_enabled = 1;
    netif_create_ip6_linklocal_address(netif, 1);

#if LWIP_IPV6_MLD
    netif->flags |= NETIF_FLAG_MLD6;

    /*
    * For hardware/netifs that implement MAC filtering.
    * All-nodes link-local is handled by default, so we must let the hardware know
    * to allow multicast packets in.
    * Should set mld_mac_filter previously. */
    if (netif->mld_mac_filter != NULL)
    {
        ip6_addr_t ip6_allnodes_ll;
        ip6_addr_set_allnodes_linklocal(&ip6_allnodes_ll);
        netif->mld_mac_filter(netif, &ip6_allnodes_ll, NETIF_ADD_MAC_FILTER);
    }
#endif /* LWIP_IPV6_MLD */

#endif /* LWIP_IPV6 */

    err = stmmac_init_phy(priv);
    if (err) {
        printf("stmmac_init_phy fail %d\n", err);
        return ERR_IF;
    }

    err = stmmac_open(priv);
    if (err) {
        printf("stmmac_open fail %d\n", err);
        return ERR_IF;
    }

    /* set default netif */
    if (netif_default == NULL)
        netif_set_default(netif);

    /* set interface up */
    netif_set_up(netif);

#ifndef CONFIG_X2000_MAC1_IPV4_ADDR
    /* if this interface uses DHCP, start the DHCP client */
    dhcp_start(netif);
#endif

    stmmac_init_phy_monitor(priv);

    return ERR_OK;
}

static void mac_hw_addr_init(u8 *hw_addr)
{
    u32 i, sum;
    u8 chip_id_buff[CHIP_ID_SIZE];

    efuse_read_segment(CHIP_ID, chip_id_buff, CHIP_ID_SIZE);

    hw_addr[0] = chip_id_buff[2];
    hw_addr[1] = chip_id_buff[3];
    hw_addr[2] = chip_id_buff[8];
    hw_addr[3] = chip_id_buff[9];
    hw_addr[4] = chip_id_buff[10];
    hw_addr[5] = chip_id_buff[11];

    sum = 0;
    for (i = 0; i < 6; i++)
        sum += hw_addr[i];

    if (!sum) {
        for (i = 0; i < 6; i++)
            hw_addr[i] = dtrng_read_random_data() % 256;
    }

    hw_addr[0] &= 0xfe;	/* clear multicast bit */
    hw_addr[0] |= 0x02;	/* set local assignment bit (IEEE802) */
    hw_addr[5]++;
}

static struct stmmac_priv *stmmac_priv;

#ifdef CONFIG_SHELL
#include <shell.h>
static int mac_phy_shell_init;
void cmd_mac1_phy(struct cmd_arg *arg, int argc, char **argv)
{
    int ret;
    int reg_data;
    int reg_offset;

    if (argc < 2) {
        printf("Please input: mac1_phy <register_offset> [value]\n");
        printf("    mac1_phy 2\n");
        return;
    }

    if (!stmmac_priv) {
        printf("mac1 driver uninitialized\n");
        return;
    }

    reg_offset = atoi(argv[1]);

    if (argc == 3) {
        reg_data = atoi(argv[2]);
        ret = stmmac_mdio_write(stmmac_priv, stmmac_priv->plat->phy_addr, reg_offset, reg_data);
        if (ret < 0) {
            printf("%s: stmmac_mdio_write fail\n", __func__);
            return;
        }
        printf("mac1 phy write reg[0x%x], val[0x%x]\n", reg_offset, reg_data);
    }

    ret = stmmac_mdio_read(stmmac_priv, stmmac_priv->plat->phy_addr, reg_offset);
    if (ret < 0) {
        printf("%s: stmmac_mdio_read fail\n", __func__);
        return;
    }

    printf("mac1 phy read reg[0x%x] val[0x%x]\n", reg_offset, ret);
}
#endif

void soc_mac1_init(void)
{
    err_t err;
    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;
    struct netif* netif;
    struct stmmac_priv *priv;

    assert(!stmmac_priv);

    jz_mac_init();

    mac_hw_addr_init(mac1_device_data.hw_addr);
    priv = stmmac_dvr_probe(&mac1_device_data);
    if (!priv) {
        jz_mac_exit();
        printf("stmmac_dvr_probe fail\n");
        return;
    }

#ifdef CONFIG_X2000_MAC1_IPV4_ADDR
    ipaddr.addr = inet_addr(CONFIG_X2000_MAC1_IPADDR);
    gw.addr = inet_addr(CONFIG_X2000_MAC1_GWADDR);
    netmask.addr = inet_addr(CONFIG_X2000_MAC1_MSKADDR);
#else
    IP4_ADDR(&ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
#endif

    netif = &priv->netif;
    err = netifapi_netif_add(netif, &ipaddr, &netmask, &gw, priv, eth_netif_device_init, tcpip_input);
    if (err) {
        printf("netifapi_netif_add fail\n");
        stmmac_dvr_remove(priv);
        jz_mac_exit();
        return;
    }

    assert(!stmmac_priv);
    stmmac_priv = priv;

#ifdef CONFIG_SHELL
    if (!mac_phy_shell_init) {
        mac_phy_shell_init = 1;
        shell_cmd_register(cmd_mac1_phy, "mac1_phy", NULL, "mac1_phy");
    }
#endif
}

void soc_mac1_deinit(void)
{
    assert(stmmac_priv);

    struct netif* netif = &stmmac_priv->netif;

    stmmac_exit_phy_monitor(stmmac_priv);

#ifndef CONFIG_X2000_MAC1_IPV4_ADDR
    netifapi_dhcp_release_and_stop(netif);
#endif
    netifapi_netif_set_down(netif);
    netifapi_netif_remove(netif);

    stmmac_release(stmmac_priv);
    stmmac_dvr_remove(stmmac_priv);
    jz_mac_exit();
    stmmac_priv = NULL;
}
