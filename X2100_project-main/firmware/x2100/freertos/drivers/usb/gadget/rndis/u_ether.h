#ifndef __U_ETHER_H__
#define __U_ETHER_H__

#include <os.h>

#include "../composite.h"
#include "../../usb_lock.h"

#include <usb/gadget_rndis.h>

#include <lwip/netifapi.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/inet.h>

#define ETH_ALEN 6

struct eth_device_stats {
    u32 rx_packets;
    u32 tx_packets;
    u32 rx_bytes;
    u32 tx_bytes;
    u32 rx_errors;
    u32 tx_errors;
    u32 rx_dropped;
    u32 tx_dropped;
};

struct eth_dev {
    struct usb_function func;

    struct usb_rndis_param *param;
    struct netif netif;
    struct eth_device_stats stats;

    /* endpoints handle full and/or high speeds */
    struct usb_ep *in_ep;
    struct usb_ep *out_ep;

    bool is_connect;

    spinlock_t req_lock;    /* guard {rx,tx}_reqs */
    struct list_head tx_reqs, rx_reqs, rx_complete_reqs;

    bool    is_zlp_ok;
    u16     cdc_filter;
    u32 max_packet_size;

    unsigned header_len;
    void (*wrap)(void *buf, u32 data_len);
    void *(*unwrap)(void *buf, u32 buf_len, u32 *data_len);

    bool rx_running;
    thread_ptr_t rx_thread;
    thread_waiter_t rx_waiter;
};

int8_t rndis_netif_device_init(struct netif *netif);
void ueth_rx_thread(void *data);
void ueth_start_rx_queue(struct eth_dev *dev);

#endif /* __U_ETHER_H__ */
