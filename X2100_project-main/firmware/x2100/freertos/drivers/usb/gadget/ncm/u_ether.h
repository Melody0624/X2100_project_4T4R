#ifndef __U_ETHER_NCM_H__
#define __U_ETHER_NCM_H__

#include <os.h>

#include "../composite.h"
#include "../../usb_lock.h"

#include <usb/gadget_ncm.h>

#include <lwip/netifapi.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/inet.h>

#define ETH_ALEN            6           /* Octets in one ethernet addr     */
#define ETH_HLEN            14          /* Total octets in header.     */
#define ETH_FRAME_LEN       1514        /* Max. octets in frame sans FCS */
#define GETHER_MAX_MTU_SIZE 15412
#define GETHER_MAX_ETH_FRAME_LEN (GETHER_MAX_MTU_SIZE + ETH_HLEN)

#define USB_CDC_PACKET_TYPE_PROMISCUOUS      (1 << 0)
#define USB_CDC_PACKET_TYPE_ALL_MULTICAST    (1 << 1) /* no filter */
#define USB_CDC_PACKET_TYPE_DIRECTED         (1 << 2)
#define USB_CDC_PACKET_TYPE_BROADCAST        (1 << 3)
#define USB_CDC_PACKET_TYPE_MULTICAST        (1 << 4) /* filtered */

#define DEFAULT_FILTER    (USB_CDC_PACKET_TYPE_BROADCAST \
                          |USB_CDC_PACKET_TYPE_ALL_MULTICAST \
                          |USB_CDC_PACKET_TYPE_PROMISCUOUS \
                          |USB_CDC_PACKET_TYPE_DIRECTED)

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

struct eth_rx_frame {
    void *buf;
    u32 buf_len;
    struct list_head list;
};

struct eth_dev {
    struct usb_function func;

    struct usb_ncm_param *param;
    struct netif netif;
    struct eth_device_stats stats;

    /* endpoints handle full and/or high speeds */
    struct usb_ep   *in_ep;
    struct usb_ep   *out_ep;

    bool            is_connect;

    spinlock_t      req_lock, xmit_lock;    /* guard {rx,tx}_reqs */
    struct list_head tx_reqs, rx_reqs, rx_complete_reqs;
    struct list_head rx_frames;

    bool            is_zlp_ok;
    u16             cdc_filter;
    /* NCM requires fixed size bundles */
    bool            is_fixed;
    u32             fixed_out_len;
    u32             fixed_in_len;

    int             (*wrap)(struct usb_request *req, struct pbuf *p);
    int             (*unwrap)(struct usb_request *req);

    bool            rx_running;
    thread_ptr_t    rx_thread;
    thread_waiter_t rx_waiter;
};

int8_t ncm_netif_device_init(struct netif *netif);
void ueth_rx_thread(void *data);
void ueth_start_rx_queue(struct eth_dev *dev);

struct eth_rx_frame *ncm_rx_frame_alloc(u32 len);
void ncm_rx_frame_free(struct eth_rx_frame *frame);

#endif /* __U_ETHER_NCM_H__ */
