#include "u_ether.h"

static void rx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct eth_dev  *dev = ep->driver_data;
    int             status = req->status;

    dev->stats.rx_packets++;

    switch (status) {
        /* normal completion */
        case 0:
            spin_lock(&dev->req_lock);
            list_add_tail(&req->list, &dev->rx_complete_reqs);
            spin_unlock(&dev->req_lock);

            thread_waiter_wakeup(&dev->rx_waiter);
            return;

        /* for hardware automagic (such as pxa) */
        case -ECONNABORTED:        /* endpoint reset */
            printf("rx %s reset\n", ep->name);
        /* software-driven interface shutdown */
        case -ECONNRESET:        /* unlink */
        case -ESHUTDOWN:        /* disconnect etc */
            break;

        /* data overrun */
        case -EOVERFLOW:
        default:
            dev->stats.rx_errors++;
            printf("ecm rx fail status %d\n", status);
            break;
    }

    spin_lock(&dev->req_lock);
    list_add(&req->list, &dev->rx_reqs);
    spin_unlock(&dev->req_lock);
}

void ueth_start_rx_queue(struct eth_dev *dev)
{
    struct usb_request  *req;
    unsigned long       flags;
    int retval;

    usb_spin_lock_irqsave(&dev->req_lock, flags);
    while (!list_empty(&dev->rx_reqs)) {
        req = list_first_entry(&dev->rx_reqs, struct usb_request, list);
        list_del_init(&req->list);
        usb_spin_unlock_irqrestore(&dev->req_lock, flags);

        req->length = dev->req_size;
        req->complete = rx_complete;
        retval = usb_ep_queue(dev->out_ep, req);
        if (!retval)
            req = NULL;

        usb_spin_lock_irqsave(&dev->req_lock, flags);
        if (req) {
            printf("usb_ep_queue failed, retval = %d\n", retval);
            list_add(&req->list, &dev->rx_reqs);
            break;
        }
    }
    usb_spin_unlock_irqrestore(&dev->req_lock, flags);
}

void ueth_rx_thread(void *data)
{
    struct eth_dev      *dev = data;
    struct pbuf         *p_buf = NULL;
    struct usb_request  *req = NULL;
    unsigned long       flags;
    int                 retval = 0;

    while(dev->rx_running) {
        usb_spin_lock_irqsave(&dev->req_lock, flags);
        while (!list_empty(&dev->rx_complete_reqs)) {
            req = list_first_entry(&dev->rx_complete_reqs, struct usb_request, list);
            list_del_init(&req->list);
            usb_spin_unlock_irqrestore(&dev->req_lock, flags);

            if (retval < 0 || req->actual < ETH_HLEN || req->actual > GETHER_MAX_ETH_FRAME_LEN) {
                dev->stats.rx_errors++;
                printf("%s: err rx length %d\n", __func__, req->actual);
                goto err_rx_data;
            }

            p_buf = pbuf_alloc(PBUF_RAW, req->actual, PBUF_RAM);
            if (!p_buf) {
                dev->stats.rx_errors++;
                printf("%s: pbuf alloc fail\n", __func__);
                goto err_rx_data;
            }

            pbuf_take(p_buf, req->buf, req->actual);

            if (dev->netif.input) {
                if(dev->netif.input(p_buf, &dev->netif) != ERR_OK) {
                    printf("%s: ether netif_input: Input error\n", __func__);
                    pbuf_free(p_buf);
                }
            }

err_rx_data:
            if (dev->is_connect) {
                req->length = dev->req_size;
                req->complete = rx_complete;
                retval = usb_ep_queue(dev->out_ep, req);
                if (retval)
                    printf("%s: usb_ep_queue failed, retval = %d\n", __func__, retval);
                else
                    req = NULL;
            }

            usb_spin_lock_irqsave(&dev->req_lock, flags);
            if (req)
                list_add(&req->list, &dev->rx_reqs);
        }
        usb_spin_unlock_irqrestore(&dev->req_lock, flags);
        thread_waiter_wait(&dev->rx_waiter);
    }
}

static void tx_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct eth_dev *dev = ep->driver_data;

    switch (req->status) {
    default:
        dev->stats.tx_errors++;
        printf("ecm tx_complete fail %d\n", req->status);
        break;
    case -ECONNRESET:        /* unlink */
    case -ESHUTDOWN:        /* disconnect etc */
        break;
    case 0:
        dev->stats.tx_bytes += req->length;
        break;
    }

    spin_lock(&dev->req_lock);
    list_add(&req->list, &dev->tx_reqs);
    spin_unlock(&dev->req_lock);
}

static inline int is_promisc(u16 cdc_filter)
{
    return cdc_filter & USB_CDC_PACKET_TYPE_PROMISCUOUS;
}

static inline bool is_multicast_ether_addr(const u8 *addr)
{
    u16 a = *(const u16 *)addr;
    return 0x01 & a;
}

static inline bool is_broadcast_ether_addr(const u8 *addr)
{
    return (*(const u16 *)(addr + 0) &
        *(const u16 *)(addr + 2) &
        *(const u16 *)(addr + 4)) == 0xffff;
}

static err_t eth_start_xmit(struct eth_dev *dev, struct pbuf *p)
{
    int length = 0;
    int retval = 0;
    struct usb_request    *req = NULL;
    unsigned long        flags;
    u16 cdc_filter = dev->cdc_filter;

    if (!dev->is_connect) {
        printf("%s: Port Not connected.\n", __func__);
        return ERR_CONN;
    }

    dev->stats.tx_packets++;

    /* apply outgoing CDC or RNDIS filters */
    if (p && !is_promisc(cdc_filter)) {
        u8        *dest = p->payload;

        if (is_multicast_ether_addr(dest)) {
            u16    type;

            /* ignores USB_CDC_PACKET_TYPE_MULTICAST and host
             * SET_ETHERNET_MULTICAST_FILTERS requests
             */
            if (is_broadcast_ether_addr(dest))
                type = USB_CDC_PACKET_TYPE_BROADCAST;
            else
                type = USB_CDC_PACKET_TYPE_ALL_MULTICAST;

            if (!(cdc_filter & type))
                return ERR_OK;
        }
        /* ignores USB_CDC_PACKET_TYPE_DIRECTED */
    }

    usb_spin_lock_irqsave(&dev->req_lock, flags);
    if (list_empty(&dev->tx_reqs)) {
        usb_spin_unlock_irqrestore(&dev->req_lock, flags);
        return ERR_USE;
    }

    req = list_first_entry(&dev->tx_reqs, struct usb_request, list);
    list_del_init(&req->list);
    usb_spin_unlock_irqrestore(&dev->req_lock, flags);

    while (p) {
        memcpy(req->buf + length, p->payload, p->len);
        length += p->len;
        p = p->next;
    }

    req->context = dev;
    req->complete = tx_complete;

    /* ECM requires no zlp if transfer is dwNtbInMaxSize */
    req->zero = 1;

    /* use zlp framing on tx for strict CDC-Ether conformance,
     * though any robust network rx path ignores extra padding.
     * and some hardware doesn't like to write zlps.
     */
    if (req->zero && !dev->is_zlp_ok && (length % dev->in_ep->maxpacket) == 0)
        length++;

    req->length = length;

    retval = usb_ep_queue(dev->in_ep, req);
    if (retval) {
        printf("%s: tx queue fail %d\n", __func__, retval);
        dev->stats.tx_dropped++;

        usb_spin_lock_irqsave(&dev->req_lock, flags);
        list_add(&req->list, &dev->tx_reqs);
        usb_spin_unlock_irqrestore(&dev->req_lock, flags);
    }

    return ERR_OK;
}

static err_t ecm_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
    struct eth_dev *dev = (struct eth_dev*)netif->state;
    unsigned long flags;

    usb_spin_lock_irqsave(&dev->xmit_lock, flags);
    int ret = eth_start_xmit(dev, p);
    usb_spin_unlock_irqrestore(&dev->xmit_lock, flags);

    return ret;
}

err_t ecm_netif_device_init(struct netif *netif)
{
    struct eth_dev *dev = (struct eth_dev*)netif->state;

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "ecm";
#endif /* LWIP_NETIF_HOSTNAME */

    netif->name[0] = 'u';
    netif->name[1] = 'e';

    /* set hw address to 6 */
    netif->hwaddr_len   = 6;
    /* maximum transfer unit */
    netif->mtu          = dev->param->mtu;

    /* set linkoutput */
    netif->linkoutput   = ecm_netif_linkoutput;

    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

#if LWIP_IGMP
    netif->flags |= NETIF_FLAG_IGMP;
#endif

    memcpy(netif->hwaddr, dev->param->dev_mac, sizeof(netif->hwaddr));

    /* set output */
    netif->output       = etharp_output;

    /* set default netif */
    if (netif_default == NULL)
        netif_set_default(netif);

    /* set interface up */
    netif_set_up(netif);

    return ERR_OK;
}
