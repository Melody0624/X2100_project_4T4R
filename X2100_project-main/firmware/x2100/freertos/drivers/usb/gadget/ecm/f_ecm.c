// SPDX-License-Identifier: GPL-2.0+
/*
 * f_ecm.c -- USB CDC Ethernet (ECM) link function driver
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 */

/* #define VERBOSE_DEBUG */

#include <os.h>
#include <assert.h>
#include <common.h>

#include <usb/ch9.h>
#include <usb/cdc.h>
#include "../composite.h"

#include "f_ecm.h"
#include "u_ether.h"

/*
 * This function is a "CDC Ethernet Networking Control Model" (CDC ECM)
 * Ethernet link.  The data transfer model is simple (packets sent and
 * received over bulk endpoints using normal short packet termination),
 * and the control model exposes various data and optional notifications.
 *
 * ECM is well standardized and (except for Microsoft) supported by most
 * operating systems with USB host support.  It's the preferred interop
 * solution for Ethernet over USB, at least for firmware based solutions.
 * (Hardware solutions tend to be more minimalist.)  A newer and simpler
 * "Ethernet Emulation Model" (CDC EEM) hasn't yet caught on.
 *
 * Note that ECM requires the use of "alternate settings" for its data
 * interface.  This means that the set_alt() method has real work to do,
 * and also means that a get_alt() method is required.
 */

#define DEFAULT_QLEN            10
#define RX_EXTRA                20

enum ecm_notify_state {
    ECM_NOTIFY_NONE,        /* don't notify */
    ECM_NOTIFY_CONNECT,     /* issue CONNECT next */
    ECM_NOTIFY_SPEED,       /* issue SPEED_CHANGE next */
};

struct f_ecm {
    struct eth_dev      dev;
    u8                  ctrl_id, data_id;
    char                ethaddr[14];

    struct usb_ep       *notify;
    struct usb_request  *notify_req;
    u8                  notify_state;
    int                 notify_count;
    bool                is_open;
};

static inline struct f_ecm *func_to_ecm(struct usb_function *f)
{
    return container_of(f, struct f_ecm, dev.func);
}

/* peak (theoretical) bulk transfer rate in bits-per-second */
static inline unsigned int gether_bitrate(struct usb_gadget *g)
{
    if (gadget_is_dualspeed(g) && g->speed == USB_SPEED_HIGH)
        return 13 * 512 * 8 * 1000 * 8;
    else
        return 19 *  64 * 1 * 1000 * 8;
}

/*-------------------------------------------------------------------------*/

#define ECM_STATUS_INTERVAL_MS          32
#define ECM_STATUS_BYTECOUNT            16    /* 8 byte header + data */

/* interface descriptor: */

static struct usb_interface_assoc_descriptor ecm_iad_descriptor = {
    .bLength =        sizeof ecm_iad_descriptor,
    .bDescriptorType =    USB_DT_INTERFACE_ASSOCIATION,

    /* .bFirstInterface =    DYNAMIC, */
    .bInterfaceCount =    2,    /* control + data */
    .bFunctionClass =    USB_CLASS_COMM,
    .bFunctionSubClass =    USB_CDC_SUBCLASS_ETHERNET,
    .bFunctionProtocol =    USB_CDC_PROTO_NONE,
    /* .iFunction =        DYNAMIC */
};


static struct usb_interface_descriptor ecm_control_intf = {
    .bLength =        sizeof ecm_control_intf,
    .bDescriptorType =    USB_DT_INTERFACE,

    /* .bInterfaceNumber = DYNAMIC */
    /* status endpoint is optional; this could be patched later */
    .bNumEndpoints =    1,
    .bInterfaceClass =    USB_CLASS_COMM,
    .bInterfaceSubClass =    USB_CDC_SUBCLASS_ETHERNET,
    .bInterfaceProtocol =    USB_CDC_PROTO_NONE,
    /* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc ecm_header_desc = {
    .bLength =        sizeof ecm_header_desc,
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_HEADER_TYPE,

    .bcdCDC =        0x0110,
};

static struct usb_cdc_union_desc ecm_union_desc = {
    .bLength =        sizeof(ecm_union_desc),
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_UNION_TYPE,
    /* .bMasterInterface0 =    DYNAMIC */
    /* .bSlaveInterface0 =    DYNAMIC */
};

static struct usb_cdc_ether_desc ecm_desc = {
    .bLength =        sizeof ecm_desc,
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_ETHERNET_TYPE,

    /* this descriptor actually adds value, surprise! */
    /* .iMACAddress = DYNAMIC */
    .bmEthernetStatistics =    0, /* no statistics */
    .wMaxSegmentSize =    ETH_FRAME_LEN,
    .wNumberMCFilters =    0,
    .bNumberPowerFilters =    0,
};

/* the default data interface has no endpoints ... */

static struct usb_interface_descriptor ecm_data_nop_intf = {
    .bLength =        sizeof ecm_data_nop_intf,
    .bDescriptorType =    USB_DT_INTERFACE,

    .bInterfaceNumber =    1,
    .bAlternateSetting =    0,
    .bNumEndpoints =    0,
    .bInterfaceClass =    USB_CLASS_CDC_DATA,
    .bInterfaceSubClass =    0,
    .bInterfaceProtocol =    0,
    /* .iInterface = DYNAMIC */
};

/* ... but the "real" data interface has two bulk endpoints */

static struct usb_interface_descriptor ecm_data_intf = {
    .bLength =        sizeof ecm_data_intf,
    .bDescriptorType =    USB_DT_INTERFACE,

    .bInterfaceNumber =    1,
    .bAlternateSetting =    1,
    .bNumEndpoints =    2,
    .bInterfaceClass =    USB_CLASS_CDC_DATA,
    .bInterfaceSubClass =    0,
    .bInterfaceProtocol =    0,
    /* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor fs_ecm_notify_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize =    ECM_STATUS_BYTECOUNT,
    .bInterval =        ECM_STATUS_INTERVAL_MS,
};

static struct usb_endpoint_descriptor fs_ecm_in_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_ecm_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *ecm_fs_function[] = {
    /* CDC ECM control descriptors */
    (struct usb_descriptor_header *) &ecm_iad_descriptor,
    (struct usb_descriptor_header *) &ecm_control_intf,
    (struct usb_descriptor_header *) &ecm_header_desc,
    (struct usb_descriptor_header *) &ecm_union_desc,
    (struct usb_descriptor_header *) &ecm_desc,

    /* NOTE: status endpoint might need to be removed */
    (struct usb_descriptor_header *) &fs_ecm_notify_desc,

    /* data interface, altsettings 0 and 1 */
    (struct usb_descriptor_header *) &ecm_data_nop_intf,
    (struct usb_descriptor_header *) &ecm_data_intf,
    (struct usb_descriptor_header *) &fs_ecm_in_desc,
    (struct usb_descriptor_header *) &fs_ecm_out_desc,
    NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor hs_ecm_notify_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize =    ECM_STATUS_BYTECOUNT,
    .bInterval =        USB_MS_TO_HS_INTERVAL(ECM_STATUS_INTERVAL_MS),
};

static struct usb_endpoint_descriptor hs_ecm_in_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    512,
};

static struct usb_endpoint_descriptor hs_ecm_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =    512,
};

static struct usb_descriptor_header *ecm_hs_function[] = {
    /* CDC ECM control descriptors */
    (struct usb_descriptor_header *) &ecm_iad_descriptor,
    (struct usb_descriptor_header *) &ecm_control_intf,
    (struct usb_descriptor_header *) &ecm_header_desc,
    (struct usb_descriptor_header *) &ecm_union_desc,
    (struct usb_descriptor_header *) &ecm_desc,

    /* NOTE: status endpoint might need to be removed */
    (struct usb_descriptor_header *) &hs_ecm_notify_desc,

    /* data interface, altsettings 0 and 1 */
    (struct usb_descriptor_header *) &ecm_data_nop_intf,
    (struct usb_descriptor_header *) &ecm_data_intf,
    (struct usb_descriptor_header *) &hs_ecm_in_desc,
    (struct usb_descriptor_header *) &hs_ecm_out_desc,
    NULL,
};

static struct netif *ecm_active_netif;

/* string descriptors: */

static struct usb_string ecm_string_defs[] = {
    [0].s = "CDC Ethernet Control Model (ECM)",
    [1].s = "",
    [2].s = "CDC Ethernet Data",
    [3].s = "CDC ECM",
    {  } /* end of list */
};

static struct usb_gadget_strings ecm_string_table = {
    .language   =       0x0409,    /* en-us */
    .strings    =       ecm_string_defs,
};

static struct usb_gadget_strings *ecm_strings[] = {
    &ecm_string_table,
    NULL,
};

/*-------------------------------------------------------------------------*/

static void ecm_do_notify(struct f_ecm *ecm)
{
    struct eth_dev             *dev = &ecm->dev;
    struct usb_request        *req = ecm->notify_req;
    struct usb_cdc_notification    *event;
    struct usb_composite_dev    *cdev = dev->func.config->cdev;
    u32                *data;
    int                status;

    /* notification already in flight? */
    if (ecm->notify_count)
        return;

    event = req->buf;
    switch (ecm->notify_state) {
    case ECM_NOTIFY_NONE:
        return;

    case ECM_NOTIFY_CONNECT:
        event->bNotificationType = USB_CDC_NOTIFY_NETWORK_CONNECTION;
        if (ecm->is_open)
            event->wValue = 1;
        else
            event->wValue = 0;
        event->wLength = 0;
        req->length = sizeof *event;

        ecm->notify_state = ECM_NOTIFY_SPEED;
        break;

    case ECM_NOTIFY_SPEED:
        event->bNotificationType = USB_CDC_NOTIFY_SPEED_CHANGE;
        event->wValue = 0;
        event->wLength = 8;
        req->length = ECM_STATUS_BYTECOUNT;

        /* SPEED_CHANGE data is up/down speeds in bits/sec */
        data = req->buf + sizeof *event;
        data[0] = gether_bitrate(cdev->gadget);
        data[1] = data[0];

        ecm->notify_state = ECM_NOTIFY_NONE;
        break;
    }
    event->bmRequestType = 0xA1;
    event->wIndex = ecm->ctrl_id;

    ecm->notify_count++;
    status = usb_ep_queue(ecm->notify, req);
    if (status < 0) {
        ecm->notify_count--;
    }
}

static void ecm_notify(struct f_ecm *ecm)
{
    /* NOTE on most versions of Linux, host side cdc-ethernet
     * won't listen for notifications until its netdevice opens.
     * The first notification then sits in the FIFO for a long
     * time, and the second one is queued.
     */
    ecm->notify_state = ECM_NOTIFY_CONNECT;
    ecm_do_notify(ecm);
}

static void ecm_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct f_ecm                *ecm = req->context;

    switch (req->status) {
    case 0:
        /* no fault */
        ecm->notify_count--;
        break;
    case -ECONNRESET:
    case -ESHUTDOWN:
        ecm->notify_count = 0;
        ecm->notify_state = ECM_NOTIFY_NONE;
        break;
    default:
        ecm->notify_count--;
        break;
    }
    ecm_do_notify(ecm);
}

static bool ecm_req_match(struct usb_function *f, const struct usb_ctrlrequest *ctrl, bool config0)
{
    struct f_ecm        *ecm = func_to_ecm(f);
    u16            w_index = ctrl->wIndex;

    if (config0 || (w_index != ecm->ctrl_id))
        return false;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
            | USB_CDC_SET_ETHERNET_PACKET_FILTER:
        break;

    default:
        return false;
    }

    return true;
}

static int ecm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
    struct f_ecm        *ecm = func_to_ecm(f);
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request    *req = cdev->req;
    int            value = -EOPNOTSUPP;
    u16            w_index = ctrl->wIndex;
    u16            w_value = ctrl->wValue;
    u16            w_length = ctrl->wLength;

    /* composite driver infrastructure handles everything except
     * CDC class messages; interface activation uses set_alt().
     */
    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
            | USB_CDC_SET_ETHERNET_PACKET_FILTER:
        /* see 6.2.30: no data, wIndex = interface,
         * wValue = packet filter bitmap
         */
        if (w_length != 0 || w_index != ecm->ctrl_id)
            goto invalid;
        ECM_DBG("packet filter %02x\n", w_value);
        /* REVISIT locking of cdc_filter.  This assumes the UDC
         * driver won't have a concurrent packet TX irq running on
         * another CPU; or that if it does, this write is atomic...
         */
        ecm->dev.cdc_filter = w_value;
        value = 0;
        break;

    /* and optionally:
     * case USB_CDC_SEND_ENCAPSULATED_COMMAND:
     * case USB_CDC_GET_ENCAPSULATED_RESPONSE:
     * case USB_CDC_SET_ETHERNET_MULTICAST_FILTERS:
     * case USB_CDC_SET_ETHERNET_PM_PATTERN_FILTER:
     * case USB_CDC_GET_ETHERNET_PM_PATTERN_FILTER:
     * case USB_CDC_GET_ETHERNET_STATISTIC:
     */

    default:
invalid:
        ECM_DBG("invalid control req%02x.%02x v%04x i%04x l%d\n",
            ctrl->bRequestType, ctrl->bRequest,
            w_value, w_index, w_length);
    }

    /* respond with data transfer or status phase? */
    if (value >= 0) {
        ECM_DBG("ecm req%02x.%02x v%04x i%04x l%d\n",
            ctrl->bRequestType, ctrl->bRequest,
            w_value, w_index, w_length);
        req->zero = 0;
        req->length = value;
        value = usb_ep_queue(cdev->gadget->ep0, req);
        if (value < 0)
            ECM_DBG("ecm req %02x.%02x response err %d\n",
                    ctrl->bRequestType, ctrl->bRequest, value);
    }

    /* device either stalls (value < 0) or reports success */
    return value;
}

/*-------------------------------------------------------------------------*/

/*
 * Callbacks let us notify the host about connect/disconnect when the
 * net device is opened or closed.
 *
 * For testing, note that link states on this side include both opened
 * and closed variants of:
 *
 *   - disconnected/unconfigured
 *   - configured but inactive (data alt 0)
 *   - configured and active (data alt 1)
 *
 * Each needs to be tested with unplug, rmmod, SET_CONFIGURATION, and
 * SET_INTERFACE (altsetting).  Remember also that "configured" doesn't
 * imply the host is actually polling the notification endpoint, and
 * likewise that "active" doesn't imply it's actually using the data
 * endpoints for traffic.
 */

static void ecm_notify_connect(struct eth_dev *dev)
{
    struct f_ecm *ecm = func_to_ecm(&dev->func);

    ecm->is_open = true;
    ecm_notify(ecm);
}

static void ecm_notify_disconnect(struct eth_dev *dev)
{
    struct f_ecm *ecm = func_to_ecm(&dev->func);

    ecm->is_open = false;
    ecm_notify(ecm);
}

int gether_connect(struct eth_dev *dev)
{
    int ret = 0;

    if (!dev)
        return -EINVAL;

    dev->in_ep->driver_data = dev;
    ret = usb_ep_enable(dev->in_ep);
    if (ret != 0) {
        ECM_DBG("enable %s --> %d\n", dev->in_ep->name, ret);
        return ret;
    }

    dev->out_ep->driver_data = dev;
    ret = usb_ep_enable(dev->out_ep);
    if (ret != 0) {
        ECM_DBG("enable %s --> %d\n", dev->out_ep->name, ret);
        usb_ep_disable(dev->in_ep);
        return ret;
    }

    dev->is_connect = true;
    if (netif_is_up(&dev->netif)) {
        ecm_notify_connect(dev);
        netif_set_link_up(&dev->netif);
        ueth_start_rx_queue(dev);
    } else {
        ecm_notify_disconnect(dev);
    }

    return 0;
}

void gether_disconnect(struct eth_dev *dev)
{
    netif_set_link_down(&dev->netif);

    dev->is_connect = false;

    usb_ep_disable(dev->in_ep);
    dev->in_ep->desc = NULL;

    usb_ep_disable(dev->out_ep);
    dev->out_ep->desc = NULL;
}

static int ecm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
    int ret = 0;
    struct f_ecm        *ecm = func_to_ecm(f);
    struct eth_dev         *dev = &ecm->dev;
    struct usb_composite_dev *cdev = f->config->cdev;

    /* Control interface has only altsetting 0 */
    if (intf == ecm->ctrl_id) {
        if (alt != 0)
            goto fail;

        usb_ep_disable(ecm->notify);
        if (!(ecm->notify->desc)) {
            if (config_ep_by_speed(cdev->gadget, f, ecm->notify))
                goto fail;
        }
        usb_ep_enable(ecm->notify);

    /* Data interface has two altsettings, 0 and 1 */
    } else if (intf == ecm->data_id) {
        if (alt > 1)
            goto fail;

        if (dev->in_ep->enabled) {
            gether_disconnect(dev);
        }

        if (!dev->in_ep->desc ||
            !dev->out_ep->desc) {
            if (config_ep_by_speed(cdev->gadget, f, dev->in_ep) ||
                config_ep_by_speed(cdev->gadget, f, dev->out_ep)) {
                dev->in_ep->desc = NULL;
                dev->out_ep->desc = NULL;
                goto fail;
            }
        }

        /* CDC Ethernet only sends data in non-default altsettings.
         * Changing altsettings resets filters, statistics, etc.
         */
        if (alt == 1) {
            /* Enable zlps by default for ECM conformance;
             * override for musb_hdrc (avoids txdma ovhead).
             */
            ecm->dev.cdc_filter = DEFAULT_FILTER;
            ret = gether_connect(dev);
            if (ret < 0)
                return ret;
        }

        /* NOTE this can be a minor disagreement with the ECM spec,
         * which says speed notifications will "always" follow
         * connection notifications.  But we allow one connect to
         * follow another (if the first is in flight), and instead
         * just guarantee that a speed notification is always sent.
         */
        ecm_notify(ecm);
    } else
        goto fail;

    return 0;
fail:
    return -EINVAL;
}

static int ecm_get_alt(struct usb_function *f, unsigned intf)
{
    struct f_ecm        *ecm = func_to_ecm(f);

    if (intf == ecm->ctrl_id)
        return 0;
    return ecm->dev.in_ep->enabled ? 1 : 0;
}

static void ecm_disable(struct usb_function *f)
{
    struct f_ecm        *ecm = func_to_ecm(f);

    ecm_active_netif = NULL;

    if (ecm->dev.in_ep->enabled) {
        gether_disconnect(&ecm->dev);
    } else {
        ecm->dev.in_ep->desc = NULL;
        ecm->dev.out_ep->desc = NULL;
    }

    usb_ep_disable(ecm->notify);
    ecm->notify->desc = NULL;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *ecm_req_alloc(struct usb_ep *ep, unsigned len)
{
    struct usb_request    *req;

    req = usb_ep_alloc_request(ep);

    if (req != NULL) {
        req->length = len;
        req->buf = cache_align_malloc(len);
        if (req->buf == NULL) {
            usb_ep_free_request(ep, req);
            return NULL;
        }
    }

    return req;
}

static void ecm_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        free(req->buf);
        usb_ep_free_request(ep, req);
    }
}

/*-------------------------------------------------------------------------*/

/* ethernet function driver setup/binding */

static int ecm_bind(struct usb_configuration *c, struct usb_function *f)
{
    struct usb_composite_dev *cdev = c->cdev;
    struct f_ecm         *ecm = func_to_ecm(f);
    struct eth_dev     *eth_dev = &ecm->dev;
    struct usb_string    *us;
    struct usb_ep        *ep;
    struct usb_request      *req;
    int    status, i;

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

    ecm_string_defs[1].s = ecm->ethaddr;

    us = usb_gstrings_attach(cdev, ecm_strings, ARRAY_SIZE(ecm_string_defs));
    if (IS_ERR(us))
        return PTR_ERR(us);

    ecm_control_intf.iInterface = us[0].id;
    ecm_data_intf.iInterface = us[2].id;
    ecm_desc.iMACAddress = us[1].id;
    ecm_iad_descriptor.iFunction = us[3].id;

    /* allocate instance-specific interface IDs */
    status = usb_interface_id(c, f);
    if (status < 0)
        return status;
    ecm->ctrl_id = status;
    ecm_iad_descriptor.bFirstInterface = status;

    ecm_control_intf.bInterfaceNumber = status;
    ecm_union_desc.bMasterInterface0 = status;

    status = usb_interface_id(c, f);
    if (status < 0)
        return status;
    ecm->data_id = status;

    ecm_data_nop_intf.bInterfaceNumber = status;
    ecm_data_intf.bInterfaceNumber = status;
    ecm_union_desc.bSlaveInterface0 = status;

    status = -ENODEV;

    /* allocate instance-specific endpoints */
    ep = usb_ep_autoconfig(cdev->gadget, &fs_ecm_in_desc);
    if (!ep)
        return status;
    ecm->dev.in_ep = ep;

    ep = usb_ep_autoconfig(cdev->gadget, &fs_ecm_out_desc);
    if (!ep)
        return status;
    ecm->dev.out_ep = ep;

    /* NOTE:  a status/notification endpoint is *OPTIONAL* but we
     * don't treat it that way.  It's simpler, and some newer CDC
     * profiles (wireless handsets) no longer treat it as optional.
     */
    ep = usb_ep_autoconfig(cdev->gadget, &fs_ecm_notify_desc);
    if (!ep)
        return status;
    ecm->notify = ep;

    hs_ecm_in_desc.bEndpointAddress = fs_ecm_in_desc.bEndpointAddress;
    hs_ecm_out_desc.bEndpointAddress = fs_ecm_out_desc.bEndpointAddress;
    hs_ecm_notify_desc.bEndpointAddress = fs_ecm_notify_desc.bEndpointAddress;

    status = usb_assign_descriptors(f, ecm_fs_function, ecm_hs_function);
    if (status)
        goto fail_free_descs;

    status = -ENOMEM;

    /* allocate tx/rx request */
    for (i = 0; i < DEFAULT_QLEN; i++) {
        req = ecm_req_alloc(ecm->dev.in_ep, ecm->dev.req_size);
        if (!req)
            goto fail_tx_reqs;
        req->context = ecm;
        list_add(&req->list, &eth_dev->tx_reqs);
    }

    for (i = 0; i < DEFAULT_QLEN; i++) {
        req = ecm_req_alloc(ecm->dev.out_ep, ecm->dev.req_size);
        if (!req)
            goto fail_rx_reqs;
        req->context = ecm;
        list_add(&req->list, &eth_dev->rx_reqs);
    }

    /* allocate notification request and buffer */
    ecm->notify_req = usb_ep_alloc_request(ep);
    if (!ecm->notify_req)
        goto fail_notify_req;
    ecm->notify_req->buf = malloc(ECM_STATUS_BYTECOUNT);
    if (!ecm->notify_req->buf)
        goto fail_notify_req;
    ecm->notify_req->length = ECM_STATUS_BYTECOUNT;
    ecm->notify_req->context = ecm;
    ecm->notify_req->complete = ecm_notify_complete;

    ipaddr.addr = inet_addr(eth_dev->param->ipaddr);
    netmask.addr = inet_addr(eth_dev->param->netmask);
    gw.addr = inet_addr(eth_dev->param->gw);

    status = netifapi_netif_add(&eth_dev->netif, &ipaddr, &netmask, &gw, eth_dev, ecm_netif_device_init, tcpip_input);
    if (status) {
        printf("%s: netifapi_netif_add fail\n", __func__);
        status = -ENOMEM;
        goto fail_notify_req;
    }

    ecm_active_netif = &eth_dev->netif;

    eth_dev->rx_running = true;
    thread_waiter_init(&eth_dev->rx_waiter);
    eth_dev->rx_thread = thread_create("rx_thread", 4096, ueth_rx_thread, eth_dev);
    assert(eth_dev->rx_thread);

    ECM_DBG("CDC Ethernet: %s speed IN/%s OUT/%s NOTIFY/%s\n",
            gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
            ecm->dev.in_ep->name, ecm->dev.out_ep->name,
            ecm->notify->name);
    return 0;

fail_notify_req:
    if (ecm->notify_req) {
        free(ecm->notify_req->buf);
        usb_ep_free_request(ecm->notify, ecm->notify_req);
    }

fail_rx_reqs:
    while (!list_empty(&eth_dev->rx_reqs)) {
        req = list_first_entry(&eth_dev->rx_reqs, struct usb_request, list);
        list_del(&req->list);
        ecm_req_free(eth_dev->out_ep, req);
    }

fail_tx_reqs:
    while (!list_empty(&eth_dev->tx_reqs)) {
        req = list_first_entry(&eth_dev->tx_reqs, struct usb_request, list);
        list_del(&req->list);
        ecm_req_free(eth_dev->in_ep, req);
    }

fail_free_descs:
    usb_free_all_descriptors(f);

    printf("%s: can't bind, err %d\n", f->name, status);

    return status;
}

static void ecm_unbind(struct usb_configuration *c, struct usb_function *f)
{
    struct f_ecm        *ecm = func_to_ecm(f);
    struct eth_dev *eth_dev = &ecm->dev;
    struct usb_request *req;

    eth_dev->rx_running = false;
    thread_waiter_wakeup(&eth_dev->rx_waiter);
    thread_join(eth_dev->rx_thread, NULL);

    ecm_active_netif = NULL;
    netifapi_netif_remove(&eth_dev->netif);

    if (ecm->notify_count) {
        usb_ep_dequeue(ecm->notify, ecm->notify_req);
        ecm->notify_count = 0;
    }

    free(ecm->notify_req->buf);
    usb_ep_free_request(ecm->notify, ecm->notify_req);

    while (!list_empty(&eth_dev->rx_complete_reqs)) {
        req = list_first_entry(&eth_dev->rx_complete_reqs, struct usb_request, list);
        list_del(&req->list);
        ecm_req_free(eth_dev->out_ep, req);
    }

    while (!list_empty(&eth_dev->rx_reqs)) {
        req = list_first_entry(&eth_dev->rx_reqs, struct usb_request, list);
        list_del(&req->list);
        ecm_req_free(eth_dev->out_ep, req);
    }

    while (!list_empty(&eth_dev->tx_reqs)) {
        req = list_first_entry(&eth_dev->tx_reqs, struct usb_request, list);
        list_del(&req->list);
        ecm_req_free(eth_dev->in_ep, req);
    }

    usb_free_all_descriptors(f);
}

struct usb_function *ecm_device_alloc(struct usb_ecm_param *param)
{
    struct f_ecm        *ecm;

    assert(param);
    ecm = malloc(sizeof(*ecm));
    if (!ecm){
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    memset(ecm, 0, sizeof(*ecm));

    ecm_active_netif = NULL;

    ecm->dev.param = param;

    /* export host's Ethernet address in CDC format */
    snprintf(ecm->ethaddr, sizeof(ecm->ethaddr), "%02x%02x%02x%02x%02x%02x",
        param->host_mac[0], param->host_mac[1], param->host_mac[2],
        param->host_mac[3], param->host_mac[4], param->host_mac[5]);
    ecm_string_defs[1].s = ecm->ethaddr;

    spin_lock_init(&ecm->dev.req_lock);
    spin_lock_init(&ecm->dev.xmit_lock);
    INIT_LIST_HEAD(&ecm->dev.tx_reqs);
    INIT_LIST_HEAD(&ecm->dev.rx_reqs);
    INIT_LIST_HEAD(&ecm->dev.rx_complete_reqs);

    ecm->dev.cdc_filter = DEFAULT_FILTER;
    ecm->dev.req_size = sizeof(struct ethhdr) + ecm->dev.param->mtu + RX_EXTRA;

    /* ECM has special (and complex) framing */
    ecm->dev.func.name = "cdc_ethernet";
    ecm->dev.func.bind = ecm_bind;
    ecm->dev.func.unbind = ecm_unbind;
    ecm->dev.func.set_alt = ecm_set_alt;
    ecm->dev.func.get_alt = ecm_get_alt;
    ecm->dev.func.req_match = ecm_req_match;
    ecm->dev.func.setup = ecm_setup;
    ecm->dev.func.disable = ecm_disable;

    return &ecm->dev.func;
}

void ecm_device_free(struct usb_function *f)
{
    struct f_ecm *ecm = func_to_ecm(f);
    free(ecm);
}

struct netif *ecm_get_active_netif(void)
{
    return ecm_active_netif;
}
