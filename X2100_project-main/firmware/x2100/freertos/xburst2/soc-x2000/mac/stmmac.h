#ifndef __STMMAC_H__
#define __STMMAC_H__

#include <spinlock.h>
#include <os.h>

#include <lwip/netif.h>
#include <lwip/pbuf.h>

#include "mac_common.h"
#include "platfrom_stmmac.h"

#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000

#define DUPLEX_HALF		0x00
#define DUPLEX_FULL		0x01
#define DUPLEX_UNKNOWN		0xff

struct net_device_stats {
	unsigned long	rx_packets;
	unsigned long	tx_packets;
	unsigned long	rx_bytes;
	unsigned long	tx_bytes;
	unsigned long	rx_errors;
	unsigned long	tx_errors;
	unsigned long	rx_dropped;
	unsigned long	tx_dropped;
	unsigned long	multicast;
	unsigned long	collisions;
	unsigned long	rx_length_errors;
	unsigned long	rx_over_errors;
	unsigned long	rx_crc_errors;
	unsigned long	rx_frame_errors;
	unsigned long	rx_fifo_errors;
	unsigned long	rx_missed_errors;
	unsigned long	tx_aborted_errors;
	unsigned long	tx_carrier_errors;
	unsigned long	tx_fifo_errors;
	unsigned long	tx_heartbeat_errors;
	unsigned long	tx_window_errors;
	unsigned long	rx_compressed;
	unsigned long	tx_compressed;
};

struct phy_status {
	int link;
	int speed;
	int duplex;
	int pause;
	int asym_pause;
};

struct stmmac_priv {
	/* Frequently used values are kept adjacent for cache effect */
	struct dma_extended_desc *dma_etx;
	struct dma_desc *dma_tx;
	u8 **tx_skbuff;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	unsigned int dma_tx_size;
	dma_addr_t *tx_skbuff_dma;
	dma_addr_t dma_tx_phy;
	int hwts_tx_en;
	spinlock_t tx_lock;

	struct dma_desc *dma_rx;
	struct dma_extended_desc *dma_erx;
	u8 **rx_skbuff;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	unsigned int dma_rx_size;
	unsigned int dma_buf_sz;
	u32 rx_riwt;
	int hwts_rx_en;
	dma_addr_t *rx_skbuff_dma;
	dma_addr_t dma_rx_phy;

	struct mac_device_info hw;
	spinlock_t lock;

	int oldlink;
	int speed;
	int oldduplex;
	unsigned int flow_ctrl;
	unsigned int pause;

	struct stmmac_extra_stats xstats;
	struct plat_stmmacenet_data *plat;
	struct dma_features dma_cap;
	int hw_cap_support;
	int synopsys_id;
	int pcs;
	unsigned int mode;
	int extend_desc;
	u32 adv_ts;
	int use_riwt;

	struct netif netif;

	int rx_running;
	thread_ptr_t rx_thread;
	thread_waiter_t rx_waiter;
	thread_waiter_t rx_exit_waiter;

	int phy_running;
	thread_ptr_t phy_thread;
	thread_waiter_t phy_irq_waiter;
	thread_waiter_t phy_exit_waiter;

	struct phy_status phy_status;
	struct net_device_stats	stats;
};

extern const struct stmmac_desc_ops enh_desc_ops;
extern const struct stmmac_desc_ops ndesc_ops;

int stmmac_mdio_scan(struct stmmac_priv *priv);
int stmmac_init_phy(struct stmmac_priv *priv);

void stmmac_init_phy_monitor(struct stmmac_priv *priv);
void stmmac_exit_phy_monitor(struct stmmac_priv *priv);

int stmmac_mdio_read(struct stmmac_priv *priv, int phyaddr, int phyreg);
int stmmac_mdio_write(struct stmmac_priv *priv, int phyaddr, int phyreg, u16 phydata);

void dwmac1000_setup(struct stmmac_priv *priv);
struct stmmac_priv *stmmac_dvr_probe(struct plat_stmmacenet_data *plat_dat);
void stmmac_dvr_remove(struct stmmac_priv *priv);
int stmmac_open(struct stmmac_priv *priv);
void stmmac_release(struct stmmac_priv *priv);
err_t stmmac_xmit(struct stmmac_priv *priv, struct pbuf *p);

#endif /* __STMMAC_H__ */
