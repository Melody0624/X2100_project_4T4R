/*
 * f_rndis.c -- RNDIS link function driver
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2003-2004 Robert Schwebel, Benedikt Spranger
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 *                    Author: Michal Nazarewicz (mina86@mina86.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <os.h>
#include <assert.h>
#include "../composite.h"
#include <usb/cdc.h>

#include "rndis.h"
#include "f_rndis.h"
#include "u_ether.h"

#define DEFAULT_QLEN    8
#define RX_EXTRA    22    /* bytes guarding against rx overflows */

#define STATUS_BYTECOUNT        8    /* 8 bytes data */
#define RNDIS_STATUS_INTERVAL_MS    32

struct ethhdr {
    unsigned char    h_dest[ETH_ALEN];    /* destination eth addr    */
    unsigned char    h_source[ETH_ALEN];    /* source ether addr    */
    u16        h_proto;        /* packet type ID field    */
} __attribute__((packed));

struct f_rndis {
    struct eth_dev dev;
    u8 ctrl_id, data_id;
    u32 vendorID;
    const char *manufacturer;
    struct rndis_params *params;

    struct usb_ep *notify;
    struct usb_request *notify_req;
    int notify_count;
};

static inline struct f_rndis *func_to_rndis(struct usb_function *f)
{
    return container_of(f, struct f_rndis, dev.func);
}

/* peak (theoretical) bulk transfer rate in bits-per-second */
static inline unsigned int gether_bitrate(struct usb_gadget *g)
{
     if (g->speed == USB_SPEED_HIGH)
        return 10 * 512 * 8 * 1000 * 8;
    else
        return 20 * 64 * 1 * 1000 * 8;
}

/* interface descriptor: */
static struct usb_interface_descriptor rndis_control_intf = {
    .bLength =        sizeof rndis_control_intf,
    .bDescriptorType =    USB_DT_INTERFACE,

    /* .bInterfaceNumber = DYNAMIC */
    /* status endpoint is optional; this could be patched later */
    .bNumEndpoints =    1,
    .bInterfaceClass =    USB_CLASS_COMM,
    .bInterfaceSubClass =   USB_CDC_SUBCLASS_ACM,
    .bInterfaceProtocol =   USB_CDC_ACM_PROTO_VENDOR,
    /* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc header_desc = {
    .bLength =        sizeof header_desc,
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_HEADER_TYPE,

    .bcdCDC = 0x0110,
};

static struct usb_cdc_call_mgmt_descriptor call_mgmt_descriptor = {
    .bLength =        sizeof call_mgmt_descriptor,
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_CALL_MANAGEMENT_TYPE,

    .bmCapabilities =   0x00,
    .bDataInterface =    0x01,
};

static struct usb_cdc_acm_descriptor rndis_acm_descriptor = {
    .bLength =        sizeof rndis_acm_descriptor,
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_ACM_TYPE,

    .bmCapabilities =    0x00,
};

static struct usb_cdc_union_desc rndis_union_desc = {
    .bLength =        sizeof(rndis_union_desc),
    .bDescriptorType =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_UNION_TYPE,
    /* .bMasterInterface0 =    DYNAMIC */
    /* .bSlaveInterface0 =    DYNAMIC */
};

/* the data interface has two bulk endpoints */

static struct usb_interface_descriptor rndis_data_intf = {
    .bLength =        sizeof rndis_data_intf,
    .bDescriptorType =    USB_DT_INTERFACE,

    .bNumEndpoints =    2,
    .bInterfaceClass =    USB_CLASS_CDC_DATA,
    .bInterfaceSubClass =    0,
    .bInterfaceProtocol =    0,
    /* .iInterface = DYNAMIC */
};


static struct usb_interface_assoc_descriptor
rndis_iad_descriptor = {
    .bLength =        sizeof rndis_iad_descriptor,
    .bDescriptorType =    USB_DT_INTERFACE_ASSOCIATION,

    // .bFirstInterface =    0, /* XXX, hardcoded */
    .bInterfaceCount =     2,    // control + data
    .bFunctionClass =    USB_CLASS_COMM,
    .bFunctionSubClass =    USB_CDC_SUBCLASS_ETHERNET,
    .bFunctionProtocol =    USB_CDC_PROTO_NONE,
    /* .iFunction = DYNAMIC */
};

/* full speed support: */
static struct usb_endpoint_descriptor fs_notify_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize =    STATUS_BYTECOUNT,
    .bInterval =        RNDIS_STATUS_INTERVAL_MS,
};

static struct usb_endpoint_descriptor fs_in_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor fs_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *eth_fs_function[] = {
    (struct usb_descriptor_header *) &rndis_iad_descriptor,

    /* control interface matches ACM, not Ethernet */
    (struct usb_descriptor_header *) &rndis_control_intf,
    (struct usb_descriptor_header *) &header_desc,
    (struct usb_descriptor_header *) &call_mgmt_descriptor,
    (struct usb_descriptor_header *) &rndis_acm_descriptor,
    (struct usb_descriptor_header *) &rndis_union_desc,
    (struct usb_descriptor_header *) &fs_notify_desc,

    /* data interface has no altsetting */
    (struct usb_descriptor_header *) &rndis_data_intf,
    (struct usb_descriptor_header *) &fs_in_desc,
    (struct usb_descriptor_header *) &fs_out_desc,
    NULL,
};

/* high speed support: */
static struct usb_endpoint_descriptor hs_notify_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize =    STATUS_BYTECOUNT,
    .bInterval =        USB_MS_TO_HS_INTERVAL(RNDIS_STATUS_INTERVAL_MS)
};

static struct usb_endpoint_descriptor hs_in_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_IN,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize = 512,
};

static struct usb_endpoint_descriptor hs_out_desc = {
    .bLength =        USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =    USB_DT_ENDPOINT,

    .bEndpointAddress =    USB_DIR_OUT,
    .bmAttributes =        USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize = 512,
};

static struct usb_descriptor_header *eth_hs_function[] = {
    (struct usb_descriptor_header *) &rndis_iad_descriptor,

    /* control interface matches ACM, not Ethernet */
    (struct usb_descriptor_header *) &rndis_control_intf,
    (struct usb_descriptor_header *) &header_desc,
    (struct usb_descriptor_header *) &call_mgmt_descriptor,
    (struct usb_descriptor_header *) &rndis_acm_descriptor,
    (struct usb_descriptor_header *) &rndis_union_desc,
    (struct usb_descriptor_header *) &hs_notify_desc,

    /* data interface has no altsetting */
    (struct usb_descriptor_header *) &rndis_data_intf,
    (struct usb_descriptor_header *) &hs_in_desc,
    (struct usb_descriptor_header *) &hs_out_desc,
    NULL,
};

/* string descriptors: */

static struct usb_string rndis_string_defs[] = {
    [0].s = "RNDIS Communications Control",
    [1].s = "RNDIS Ethernet Data",
    [2].s = "RNDIS",
    {  } /* end of list */
};

static struct usb_gadget_strings rndis_string_table = {
    .language =        0x0409,    /* en-us */
    .strings =        rndis_string_defs,
};

static struct usb_gadget_strings *rndis_strings[] = {
    &rndis_string_table,
    NULL,
};

/*-------------------------------------------------------------------------*/

static void rndis_response_available(void *_rndis)
{
    struct f_rndis *rndis = _rndis;
    struct usb_request *req = rndis->notify_req;
    u32 *data = req->buf;
    int status;

    rndis->notify_count++;
    if (rndis->notify_count != 1)
        return;

    /* Send RNDIS RESPONSE_AVAILABLE notification; a
     * USB_CDC_NOTIFY_RESPONSE_AVAILABLE "should" work too
     *
     * This is the only notification defined by RNDIS.
     */
    data[0] = 0x1;
    data[1] = 0x0;

    status = usb_ep_queue(rndis->notify, req);
    if (status) {
        rndis->notify_count--;
        printf("%s: usb_ep_queue fail %d\n", __func__, status);
    }
}

static void rndis_response_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct f_rndis *rndis = req->context;
    int status = req->status;

    switch (status) {
        case 0:
            if (ep != rndis->notify)
                break;

            /* handle multiple pending RNDIS_RESPONSE_AVAILABLE
            * notifications by resending until we're done
            */
            rndis->notify_count--;
            if(rndis->notify_count == 0);
                break;

            status = usb_ep_queue(rndis->notify, req);
            if (status) {
                rndis->notify_count--;
                printf("%s: usb_ep_queue fail %d\n", __func__, status);
            }
            break;

        case -ECONNRESET:
        case -ESHUTDOWN:
            /* connection gone */
            rndis->notify_count = 0;
            break;
        default:
            printf("RNDIS %s response error %d, %d/%d\n",
                ep->name, status, req->actual, req->length);
    }
}

static void rndis_command_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct f_rndis *rndis = req->context;
    int status;

    status = rndis_msg_parser(rndis->params, (u8 *) req->buf);
    if (status < 0)
        printf("RNDIS command error %d, %d/%d\n", status, req->actual, req->length);
}

static int rndis_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
    struct f_rndis *rndis = func_to_rndis(f);
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request    *req = cdev->req;
    int value = -EOPNOTSUPP;
    u16 w_index = ctrl->wIndex;
    u16 w_value = ctrl->wValue;
    u16 w_length = ctrl->wLength;

    /* composite driver infrastructure handles everything except
     * CDC class messages; interface activation uses set_alt().
     */
    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {

    /* RNDIS uses the CDC command encapsulation mechanism to implement
     * an RPC scheme, with much getting/setting of attributes by OID.
     */
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
            | USB_CDC_SEND_ENCAPSULATED_COMMAND:
        if (w_value || w_index != rndis->ctrl_id)
            goto invalid;
        /* read the request; process it later */
        value = w_length;
        req->complete = rndis_command_complete;
        req->context = rndis;

        /* later, rndis_response_available() sends a notification */
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
            | USB_CDC_GET_ENCAPSULATED_RESPONSE:
        if (w_value || w_index != rndis->ctrl_id)
            goto invalid;
        else {
            u8 *buf;
            u32 n;

            /* return the result */
            buf = rndis_get_next_response(rndis->params, &n);
            if (buf) {
                memcpy(req->buf, buf, n);
                req->complete = rndis_response_complete;
                req->context = rndis;
                rndis_free_response(rndis->params, buf);
                value = n;
            }
            /* else stalls ... spec says to avoid that */
        }
        break;

    default:
invalid:
        printf("invalid control req%02x.%02x v%04x i%04x l%d\n", ctrl->bRequestType, ctrl->bRequest, w_value, w_index, w_length);
    }

    /* respond with data transfer or status phase? */
    if (value >= 0) {
        req->zero = (value < w_length);
        req->length = value;
        value = usb_ep_queue(cdev->gadget->ep0, req);
        if (value < 0)
            printf( "rndis response on err %d\n", value);
    }

    /* device either stalls (value < 0) or reports success */
    return value;
}


/*-------------------------------------------------------------------------*/
/*
 * This isn't quite the same mechanism as CDC Ethernet, since the
 * notification scheme passes less data, but the same set of link
 * states must be tested.  A key difference is that altsettings are
 * not used to tell whether the link should send packets or not.
 */

static void rndis_notify_connect(struct eth_dev *dev)
{
    struct f_rndis *rndis = func_to_rndis(&dev->func);
    struct usb_composite_dev *cdev = dev->func.config->cdev;

    rndis_set_param_medium(rndis->params, RNDIS_MEDIUM_802_3,
                gether_bitrate(cdev->gadget) / 100);
    rndis_signal_connect(rndis->params);
}

static void rndis_notify_disconnect(struct eth_dev *dev)
{
    struct f_rndis *rndis = func_to_rndis(&dev->func);

    rndis_set_param_medium(rndis->params, RNDIS_MEDIUM_802_3, 0);
    rndis_signal_disconnect(rndis->params);
}

int gether_connect(struct eth_dev *dev)
{
    int ret = 0;

    dev->in_ep->driver_data = dev;
    ret = usb_ep_enable(dev->in_ep);
    if (ret != 0) {
        printf("%s: usb_ep_enable fail %d\n", __func__, ret);
        return ret;
    }

    dev->out_ep->driver_data = dev;
    ret = usb_ep_enable(dev->out_ep);
    if (ret != 0) {
        printf("%s: usb_ep_enable fail %d\n", __func__, ret);
        usb_ep_disable(dev->in_ep);
        return ret;
    }

    dev->is_connect = true;
    if (netif_is_up(&dev->netif)) {
        rndis_notify_connect(dev);
        netif_set_link_up(&dev->netif);
        ueth_start_rx_queue(dev);
    } else {
        rndis_notify_disconnect(dev);
    }

    return 0;
}

void gether_disconnect(struct eth_dev *dev)
{
    netif_set_link_down(&dev->netif);

    /* disable endpoints, forcing (synchronous) completion
    * of all pending i/o.  then free the request objects
    * and forget about the endpoints.
    */
    usb_ep_disable(dev->in_ep);
    dev->in_ep->desc = NULL;

    usb_ep_disable(dev->out_ep);
    dev->out_ep->desc = NULL;

    dev->is_connect = false;
}

static int rndis_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
    int ret;
    struct f_rndis *rndis = func_to_rndis(f);
    struct eth_dev *dev = &rndis->dev;
    struct usb_composite_dev *cdev = f->config->cdev;

    /* we know alt == 0 */
    if (intf == rndis->ctrl_id) {
        usb_ep_disable(rndis->notify);

        if (!rndis->notify->desc) {
            if (config_ep_by_speed(cdev->gadget, f, rndis->notify))
                return -EINVAL;
        }
        usb_ep_enable(rndis->notify);

    } else if (intf == rndis->data_id) {
        if (dev->in_ep->enabled) {
            gether_disconnect(&rndis->dev);
        }

        if (!dev->in_ep->desc || !dev->out_ep->desc) {
            if (config_ep_by_speed(cdev->gadget, f, dev->in_ep)
                    || config_ep_by_speed(cdev->gadget, f, dev->out_ep)) {
                dev->in_ep->desc = NULL;
                dev->out_ep->desc = NULL;
                return -EINVAL;
            }
        }

        /* Avoid ZLPs; they can be troublesome. */
        dev->is_zlp_ok = false;
        dev->cdc_filter = 0;
        ret = gether_connect(dev);
        if (ret)
            return ret;

        rndis_set_param_filter(rndis->params, &dev->cdc_filter);
    } else {
        return -EINVAL;
    }

    return 0;
}

static void rndis_disable(struct usb_function *f)
{
    struct f_rndis *rndis = func_to_rndis(f);

    if (!rndis->notify->enabled)
        return;

    rndis_uninit(rndis->params);
    gether_disconnect(&rndis->dev);

    usb_ep_disable(rndis->notify);
    rndis->notify->desc = NULL;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *rndis_req_alloc(struct usb_ep *ep, unsigned len)
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

static void rndis_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        free(req->buf);
        usb_ep_free_request(ep, req);
    }
}

static int rndis_bind(struct usb_configuration *c, struct usb_function *f)
{
    struct usb_composite_dev *cdev = c->cdev;
    struct f_rndis *rndis = func_to_rndis(f);
    struct eth_dev *eth_dev = &rndis->dev;
    struct usb_string    *us;
    struct usb_ep       *ep;
    struct usb_request *req;
    err_t err = 0;
    int i;
    int status;

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

    /* Padding up to RX_EXTRA handles minor disagreements with host.
    * Normally we use the USB "terminate on short read" convention;
    * so allow up to (N*maxpacket), since that memory is normally
    * already allocated.  Some hardware doesn't deal well with short
    * reads (e.g. DMA must be N*maxpacket), so for now don't trim a
    * byte off the end (to force hardware errors on overflow).
    *
    * RNDIS uses internal framing, and explicitly allows senders to
    * pad to end-of-packet.  That's potentially nice for speed, but
    * means receivers can't recover lost synch on their own (because
    * new packets don't only start after a short RX).
    */
    eth_dev->max_packet_size = sizeof(struct ethhdr) + eth_dev->param->mtu + RX_EXTRA + eth_dev->header_len;

    rndis->params->priv_dev = eth_dev;
    rndis_set_param_medium(rndis->params, RNDIS_MEDIUM_802_3, 0);
    rndis_set_param_vendor(rndis->params, rndis->vendorID, rndis->manufacturer);

    us = usb_gstrings_attach(cdev, rndis_strings, ARRAY_SIZE(rndis_string_defs));
    if (IS_ERR(us))
        return PTR_ERR(us);

    rndis_control_intf.iInterface = us[0].id;
    rndis_data_intf.iInterface = us[1].id;
    rndis_iad_descriptor.iFunction = us[2].id;

    /* allocate instance-specific interface IDs */
    status = usb_interface_id(c, f);
    if (status < 0)
        return status;
    rndis->ctrl_id = status;
    rndis_iad_descriptor.bFirstInterface = status;

    rndis_control_intf.bInterfaceNumber = status;
    rndis_union_desc.bMasterInterface0 = status;

    status = usb_interface_id(c, f);
    if (status < 0)
        return status;
    rndis->data_id = status;

    rndis_data_intf.bInterfaceNumber = status;
    rndis_union_desc.bSlaveInterface0 = status;

    status = -ENODEV;

    /* allocate instance-specific endpoints */
    ep = usb_ep_autoconfig(cdev->gadget, &fs_in_desc);
    if (!ep)
        return status;
    rndis->dev.in_ep = ep;

    ep = usb_ep_autoconfig(cdev->gadget, &fs_out_desc);
    if (!ep)
        return status;
    rndis->dev.out_ep = ep;

    /* NOTE:  a status/notification endpoint is, strictly speaking,
     * optional.  We don't treat it that way though!  It's simpler,
     * and some newer profiles don't treat it as optional.
     */
    ep = usb_ep_autoconfig(cdev->gadget, &fs_notify_desc);
    if (!ep)
        return status;
    rndis->notify = ep;

    status = -ENOMEM;
    for (i = 0; i < DEFAULT_QLEN; i++) {
        req = rndis_req_alloc(rndis->dev.in_ep, eth_dev->max_packet_size);
        if (!req)
            goto fail_tx_reqs;
        list_add(&req->list, &eth_dev->tx_reqs);
    }

    for (i = 0; i < DEFAULT_QLEN; i++) {
        req = rndis_req_alloc(rndis->dev.out_ep, eth_dev->max_packet_size);
        if (!req)
            goto fail_rx_reqs;
        list_add(&req->list, &eth_dev->rx_reqs);
    }

    /* allocate notification request and buffer */
    rndis->notify_req = usb_ep_alloc_request(ep);
    if (!rndis->notify_req)
        goto fail_notify_req;
    rndis->notify_req->buf = malloc(STATUS_BYTECOUNT);
    if (!rndis->notify_req->buf)
        goto fail_notify_req;
    rndis->notify_req->length = STATUS_BYTECOUNT;
    rndis->notify_req->context = rndis;
    rndis->notify_req->complete = rndis_response_complete;

    /* support all relevant hardware speeds... we expect that when
     * hardware is dual speed, all bulk-capable endpoints work at
     * both speeds
     */
    hs_in_desc.bEndpointAddress = fs_in_desc.bEndpointAddress;
    hs_out_desc.bEndpointAddress = fs_out_desc.bEndpointAddress;
    hs_notify_desc.bEndpointAddress = fs_notify_desc.bEndpointAddress;

    status = usb_assign_descriptors(f, eth_fs_function, eth_hs_function);
    if (status)
        goto fail_notify_req;

    ipaddr.addr = inet_addr(eth_dev->param->ipaddr);
    netmask.addr = inet_addr(eth_dev->param->netmask);
    gw.addr = inet_addr(eth_dev->param->gw);
    err = netifapi_netif_add(&eth_dev->netif, &ipaddr, &netmask, &gw, eth_dev, rndis_netif_device_init, tcpip_input);
    if(err){
        printf("%s: netifapi_netif_add fail\n", __func__);
        status = -ENOMEM;
        goto fail_free_descs;
    }

    eth_dev->rx_running = true;
    thread_waiter_init(&eth_dev->rx_waiter);
    eth_dev->rx_thread = thread_create("rx_thread", 4096, ueth_rx_thread, eth_dev);

    return 0;

fail_free_descs:
    usb_free_all_descriptors(f);

fail_notify_req:
    if (rndis->notify_req) {
        if (rndis->notify_req->buf)
            free(rndis->notify_req->buf);
        usb_ep_free_request(rndis->notify, rndis->notify_req);
    }

fail_rx_reqs:
    while (!list_empty(&eth_dev->rx_reqs)) {
        req = list_first_entry(&eth_dev->rx_reqs, struct usb_request, list);
        list_del(&req->list);
        rndis_req_free(eth_dev->out_ep, req);
    }

fail_tx_reqs:
    while (!list_empty(&eth_dev->tx_reqs)) {
        req = list_first_entry(&eth_dev->tx_reqs, struct usb_request, list);
        list_del(&req->list);
        rndis_req_free(eth_dev->in_ep, req);
    }

    printf( "%s: can't bind, err %d\n", f->name, status);

    return status;
}

static void rndis_unbind(struct usb_configuration *c, struct usb_function *f)
{
    struct f_rndis *rndis = func_to_rndis(f);
    struct eth_dev *eth_dev = &rndis->dev;
    struct usb_request *req;

    eth_dev->rx_running = 0;
    thread_waiter_wakeup(&eth_dev->rx_waiter);
    thread_join(eth_dev->rx_thread, NULL);

    netifapi_netif_remove(&eth_dev->netif);

    free(rndis->notify_req->buf);
    usb_ep_free_request(rndis->notify, rndis->notify_req);

    while (!list_empty(&eth_dev->rx_complete_reqs)) {
        req = list_first_entry(&eth_dev->rx_complete_reqs, struct usb_request, list);
        list_del(&req->list);
        rndis_req_free(eth_dev->out_ep, req);
    }

    while (!list_empty(&eth_dev->rx_reqs)) {
        req = list_first_entry(&eth_dev->rx_reqs, struct usb_request, list);
        list_del(&req->list);
        rndis_req_free(eth_dev->out_ep, req);
    }

    while (!list_empty(&eth_dev->tx_reqs)) {
        req = list_first_entry(&eth_dev->tx_reqs, struct usb_request, list);
        list_del(&req->list);
        rndis_req_free(eth_dev->in_ep, req);
    }

    usb_free_all_descriptors(f);
}

void rndis_add_header(void *buf, u32 data_len)
{
    struct rndis_packet_msg_type *header = buf;

    memset(header, 0, sizeof(struct rndis_packet_msg_type));
    header->MessageType = RNDIS_MSG_PACKET;
    header->MessageLength = sizeof(*header) + data_len;
    header->DataOffset = 36;
    header->DataLength = data_len;
}

void *rndis_rm_hdr(void *buf, u32 buf_len, u32 *data_len)
{
    /* tmp points to a struct rndis_packet_msg_type */
    u32 offset;
    u32 *tmp = buf;
    u32 pack_head = get_unaligned_le32(tmp++);
    /* MessageType */
    if (RNDIS_MSG_PACKET != pack_head){
        return NULL;
    }

    /* MessageLength */
    tmp++;

    /* DataOffset */
    offset = get_unaligned_le32(tmp++) + 8;
    if (buf_len <= offset){
        return NULL;
    }

    /* DataLength */
    *data_len = get_unaligned_le32(tmp++);
    if (buf_len - offset < *data_len){
        return NULL;
    }

    return buf + offset;
}

struct usb_function *rndis_device_alloc(struct usb_rndis_param *param)
{
    struct f_rndis *rndis;
    struct rndis_params *rndis_params;

    assert(param);
    rndis = malloc(sizeof(*rndis));
    if (!rndis){
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    memset(rndis, 0, sizeof(*rndis));

    rndis->dev.param = param;

    //rndis->vendorID
    //rndis->manufacturer

    spin_lock_init(&rndis->dev.req_lock);
    INIT_LIST_HEAD(&rndis->dev.tx_reqs);
    INIT_LIST_HEAD(&rndis->dev.rx_reqs);
    INIT_LIST_HEAD(&rndis->dev.rx_complete_reqs);

    /* RNDIS has special (and complex) framing */
    rndis->dev.header_len = sizeof(struct rndis_packet_msg_type);
    rndis->dev.wrap = rndis_add_header;
    rndis->dev.unwrap = rndis_rm_hdr;

    rndis->dev.func.name = "rndis";
    rndis->dev.func.bind = rndis_bind;
    rndis->dev.func.unbind = rndis_unbind;
    rndis->dev.func.set_alt = rndis_set_alt;
    rndis->dev.func.setup = rndis_setup;
    rndis->dev.func.disable = rndis_disable;

    rndis_params = rndis_register(rndis_response_available, rndis);
    if (IS_ERR(rndis_params)) {
        free(rndis);
        return ERR_CAST(rndis_params);
    }

    rndis->params = rndis_params;
    return &rndis->dev.func;
}

void rndis_device_free(struct usb_function *f)
{
    struct f_rndis *rndis = func_to_rndis(f);

    rndis_deregister(rndis->params);
    free(rndis);
}