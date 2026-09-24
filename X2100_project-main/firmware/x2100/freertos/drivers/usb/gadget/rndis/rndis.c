/*
 * RNDIS MSG parser
 *
 * Authors:    Benedikt Spranger, Pengutronix
 *        Robert Schwebel, Pengutronix
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              version 2, as published by the Free Software Foundation.
 *
 *        This software was originally developed in conformance with
 *        Microsoft's Remote NDIS Specification License Agreement.
 *
 * 03/12/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 *        Fixed message length bug in init_response
 *
 * 03/25/2004 Kai-Uwe Bloem <linux-development@auerswald.de>
 *        Fixed rndis_rm_hdr length bug.
 *
 * Copyright (C) 2004 by David Brownell
 *        updates to merge with Linux 2.6, better match RNDIS spec
 */

#include <os.h>
#include <assert.h>
#include <little_things.h>
#include <le_byteshift.h>
#include <errno.h>

#include "f_rndis.h"
#include "u_ether.h"

// #define RNDIS_DEBUG

#ifdef RNDIS_DEBUG
#define RNDIS_DBG(...)  printf(__VA_ARGS__)
#else
#define RNDIS_DBG(...)
#endif

/* The driver for your USB chip needs to support ep0 OUT to work with
 * RNDIS, plus all three CDC Ethernet endpoints (interrupt not optional).
 *
 * Windows hosts need an INF file like Documentation/usb/linux.inf
 * and will be happier if you provide the host_addr module parameter.
 */

/* Driver Version */
static const u32 rndis_driver_version = 1;

/* supported OIDs */
static const u32 oid_supported_list[] =
{
    /* the general stuff */
    RNDIS_OID_GEN_SUPPORTED_LIST,
    RNDIS_OID_GEN_HARDWARE_STATUS,
    RNDIS_OID_GEN_MEDIA_SUPPORTED,
    RNDIS_OID_GEN_MEDIA_IN_USE,
    RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE,
    RNDIS_OID_GEN_LINK_SPEED,
    RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE,
    RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE,
    RNDIS_OID_GEN_VENDOR_ID,
    RNDIS_OID_GEN_VENDOR_DESCRIPTION,
    RNDIS_OID_GEN_VENDOR_DRIVER_VERSION,
    RNDIS_OID_GEN_CURRENT_PACKET_FILTER,
    RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE,
    RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
    RNDIS_OID_GEN_PHYSICAL_MEDIUM,

    /* the statistical stuff */
    RNDIS_OID_GEN_XMIT_OK,
    RNDIS_OID_GEN_RCV_OK,
    RNDIS_OID_GEN_XMIT_ERROR,
    RNDIS_OID_GEN_RCV_ERROR,
    RNDIS_OID_GEN_RCV_NO_BUFFER,

    /* mandatory 802.3 */
    /* the general stuff */
    RNDIS_OID_802_3_PERMANENT_ADDRESS,
    RNDIS_OID_802_3_CURRENT_ADDRESS,
    RNDIS_OID_802_3_MULTICAST_LIST,
    RNDIS_OID_802_3_MAC_OPTIONS,
    RNDIS_OID_802_3_MAXIMUM_LIST_SIZE,

    /* the statistical stuff */
    RNDIS_OID_802_3_RCV_ERROR_ALIGNMENT,
    RNDIS_OID_802_3_XMIT_ONE_COLLISION,
    RNDIS_OID_802_3_XMIT_MORE_COLLISIONS,
};

void rndis_free_response(struct rndis_params *params, u8 *buf)
{
    rndis_resp_t *r, *n;

    spin_lock(&params->resp_lock);
    list_for_each_entry_safe(r, n, &params->resp_queue, list) {
        if (r->buf == buf) {
            list_del(&r->list);
            free(r);
        }
    }
    spin_unlock(&params->resp_lock);
}

u8 *rndis_get_next_response(struct rndis_params *params, u32 *length)
{
    rndis_resp_t *r, *n;
    if (!length) return NULL;

    spin_lock(&params->resp_lock);
    list_for_each_entry_safe(r, n, &params->resp_queue, list) {
        if (!r->send) {
            r->send = 1;
            *length = r->length;
            spin_unlock(&params->resp_lock);
            return r->buf;
        }
    }

    spin_unlock(&params->resp_lock);
    return NULL;;
}

static rndis_resp_t *rndis_add_response(struct rndis_params *params, u32 length)
{
    rndis_resp_t *r;

    /* NOTE: this gets copied into ether.c USB_BUFSIZ bytes ... */
    r = malloc(sizeof(rndis_resp_t) + length);
    if (!r) return NULL;

    r->buf = (u8 *)(r + 1);
    r->length = length;
    r->send = 0;

    spin_lock(&params->resp_lock);
    list_add_tail(&r->list, &params->resp_queue);
    spin_unlock(&params->resp_lock);

    return r;
}

/* NDIS Functions */
static int gen_ndis_query_resp(struct rndis_params *params, u32 OID, u8 *buf,
                   unsigned buf_len, rndis_resp_t *r)
{
    int retval = -EPERM;
    u32 length = 4;    /* usually */
    u32 *outbuf;
    int i, count;
    rndis_query_cmplt_type *resp;
    struct eth_device_stats *stats;
    struct usb_rndis_param *priv_param = params->priv_dev->param;

    if (!r) return -ENOMEM;
    resp = (rndis_query_cmplt_type *)r->buf;

    if (!resp) return -ENOMEM;

    if (buf_len) {
        RNDIS_DBG("query OID %08x value, len %d:\n", OID, buf_len);
        for (i = 0; i < buf_len; i += 16) {
            RNDIS_DBG("%03d: %08x %08x %08x %08x\n", i,
                get_unaligned_le32(&buf[i]),
                get_unaligned_le32(&buf[i + 4]),
                get_unaligned_le32(&buf[i + 8]),
                get_unaligned_le32(&buf[i + 12]));
        }
    }

    stats = &params->priv_dev->stats;

    /* response goes here, right after the header */
    outbuf = (u32 *)&resp[1];
    resp->InformationBufferOffset = (16);

    switch (OID) {

    /* general oids (table 4-1) */

    /* mandatory */
    case RNDIS_OID_GEN_SUPPORTED_LIST:
        RNDIS_DBG("%s: RNDIS_OID_GEN_SUPPORTED_LIST\n", __func__);
        length = sizeof(oid_supported_list);
        count  = length / sizeof(u32);
        for (i = 0; i < count; i++)
            outbuf[i] = oid_supported_list[i];
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_HARDWARE_STATUS:
        RNDIS_DBG("%s: RNDIS_OID_GEN_HARDWARE_STATUS\n", __func__);
        /* Bogus question!
         * Hardware must be ready to receive high level protocols.
         * BTW:
         * reddite ergo quae sunt Caesaris Caesari
         * et quae sunt Dei Deo!
         */
        *outbuf = (0);
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_MEDIA_SUPPORTED:
        RNDIS_DBG("%s: RNDIS_OID_GEN_MEDIA_SUPPORTED\n", __func__);
        *outbuf = params->medium;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_MEDIA_IN_USE:
        RNDIS_DBG("%s: RNDIS_OID_GEN_MEDIA_IN_USE\n", __func__);
        /* one medium, one transport... (maybe you do it better) */
        *outbuf = params->medium;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE:
        RNDIS_DBG("%s: RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE\n", __func__);
        *outbuf = priv_param->mtu;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_LINK_SPEED:
        RNDIS_DBG("%s: RNDIS_OID_GEN_LINK_SPEED\n", __func__);
        if (params->media_state == RNDIS_MEDIA_STATE_DISCONNECTED)
            *outbuf = 0;
        else
            *outbuf = params->speed;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE:
        RNDIS_DBG("%s: RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE\n", __func__);
        *outbuf = priv_param->mtu;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE:
        RNDIS_DBG("%s: RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE\n", __func__);
        *outbuf = priv_param->mtu;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_VENDOR_ID:
        RNDIS_DBG("%s: RNDIS_OID_GEN_VENDOR_ID\n", __func__);
        *outbuf = params->vendorID;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_VENDOR_DESCRIPTION:
        RNDIS_DBG("%s: RNDIS_OID_GEN_VENDOR_DESCRIPTION\n", __func__);
        if (params->vendorDescr) {
            length = strlen(params->vendorDescr);
            memcpy(outbuf, params->vendorDescr, length);
        } else {
            outbuf[0] = 0;
        }
        retval = 0;
        break;

    case RNDIS_OID_GEN_VENDOR_DRIVER_VERSION:
        RNDIS_DBG("%s: RNDIS_OID_GEN_VENDOR_DRIVER_VERSION\n", __func__);
        /* Created as LE */
        *outbuf = rndis_driver_version;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_CURRENT_PACKET_FILTER:
        RNDIS_DBG("%s: RNDIS_OID_GEN_CURRENT_PACKET_FILTER\n", __func__);
        *outbuf = *params->filter;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE:
        RNDIS_DBG("%s: RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE\n", __func__);
        *outbuf = RNDIS_MAX_TOTAL_SIZE;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_MEDIA_CONNECT_STATUS:
        RNDIS_DBG("%s: RNDIS_OID_GEN_MEDIA_CONNECT_STATUS\n", __func__);
        *outbuf = params->media_state;
        retval = 0;
        break;

    case RNDIS_OID_GEN_PHYSICAL_MEDIUM:
        RNDIS_DBG("%s: RNDIS_OID_GEN_PHYSICAL_MEDIUM\n", __func__);
        *outbuf = 0;
        retval = 0;
        break;

    /* The RNDIS specification is incomplete/wrong.   Some versions
     * of MS-Windows expect OIDs that aren't specified there.  Other
     * versions emit undefined RNDIS messages. DOCUMENT ALL THESE!
     */
    case RNDIS_OID_GEN_MAC_OPTIONS:        /* from WinME */
        RNDIS_DBG("%s: RNDIS_OID_GEN_MAC_OPTIONS\n", __func__);
        *outbuf = RNDIS_MAC_OPTION_RECEIVE_SERIALIZED | RNDIS_MAC_OPTION_FULL_DUPLEX;
        retval = 0;
        break;

    /* statistics OIDs (table 4-2) */

    /* mandatory */
    case RNDIS_OID_GEN_XMIT_OK:
        RNDIS_DBG("%s: RNDIS_OID_GEN_XMIT_OK\n", __func__);
        *outbuf = stats->tx_packets - stats->tx_errors - stats->tx_dropped;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_RCV_OK:
        RNDIS_DBG("%s: RNDIS_OID_GEN_RCV_OK\n", __func__);
        *outbuf = stats->rx_packets - stats->rx_errors - stats->rx_dropped;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_XMIT_ERROR:
        RNDIS_DBG("%s: RNDIS_OID_GEN_XMIT_ERROR\n", __func__);
        *outbuf = stats->tx_errors;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_RCV_ERROR:
        RNDIS_DBG("%s: RNDIS_OID_GEN_RCV_ERROR\n", __func__);
        *outbuf = stats->rx_errors;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_GEN_RCV_NO_BUFFER:
        RNDIS_DBG("%s: RNDIS_OID_GEN_RCV_NO_BUFFER\n", __func__);
        *outbuf = stats->rx_dropped;
        retval = 0;
        break;

    /* ieee802.3 OIDs (table 4-3) */

    /* mandatory */
    case RNDIS_OID_802_3_PERMANENT_ADDRESS:
        RNDIS_DBG("%s: RNDIS_OID_802_3_PERMANENT_ADDRESS\n", __func__);
        length = ETH_ALEN;
        memcpy(outbuf, priv_param->host_mac, length);
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_802_3_CURRENT_ADDRESS:
        RNDIS_DBG("%s: RNDIS_OID_802_3_CURRENT_ADDRESS\n", __func__);
        length = ETH_ALEN;
        memcpy(outbuf, priv_param->host_mac, length);
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_802_3_MULTICAST_LIST:
        RNDIS_DBG("%s: RNDIS_OID_802_3_MULTICAST_LIST\n", __func__);
        /* Multicast base address only */
        *outbuf = 0xE0000000;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_802_3_MAXIMUM_LIST_SIZE:
        RNDIS_DBG("%s: RNDIS_OID_802_3_MAXIMUM_LIST_SIZE\n", __func__);
        /* Multicast base address only */
        *outbuf = 1;
        retval = 0;
        break;

    case RNDIS_OID_802_3_MAC_OPTIONS:
        RNDIS_DBG("%s: RNDIS_OID_802_3_MAC_OPTIONS\n", __func__);
        *outbuf = 0;
        retval = 0;
        break;

    /* ieee802.3 statistics OIDs (table 4-4) */

    /* mandatory */
    case RNDIS_OID_802_3_RCV_ERROR_ALIGNMENT:
        RNDIS_DBG("%s: RNDIS_OID_802_3_RCV_ERROR_ALIGNMENT\n", __func__);
        *outbuf = 0; //rx_frame_errors
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_802_3_XMIT_ONE_COLLISION:
        RNDIS_DBG("%s: RNDIS_OID_802_3_XMIT_ONE_COLLISION\n", __func__);
        *outbuf = 0;
        retval = 0;
        break;

    /* mandatory */
    case RNDIS_OID_802_3_XMIT_MORE_COLLISIONS:
        RNDIS_DBG("%s: RNDIS_OID_802_3_XMIT_MORE_COLLISIONS\n", __func__);
        *outbuf = 0;
        retval = 0;
        break;

    default:
        printf("%s: query unknown OID 0x%08X\n", __func__, OID);
    }

    if (retval < 0)
        length = 0;

    resp->InformationBufferLength = length;
    r->length = length + sizeof(*resp);
    resp->MessageLength = r->length;
    return retval;
}

static int gen_ndis_set_resp(struct rndis_params *params, u32 OID,
                 u8 *buf, u32 buf_len, rndis_resp_t *r)
{
    rndis_set_cmplt_type *resp;
    int i, retval = -EPERM;

    if (!r)
        return -ENOMEM;
    resp = (rndis_set_cmplt_type *)r->buf;
    if (!resp)
        return -ENOMEM;

    if (buf_len) {
        RNDIS_DBG("set OID %08x value, len %d:\n", OID, buf_len);
        for (i = 0; i < buf_len; i += 16) {
            RNDIS_DBG("%03d: %08x %08x %08x %08x\n", i,
                get_unaligned_le32(&buf[i]),
                get_unaligned_le32(&buf[i + 4]),
                get_unaligned_le32(&buf[i + 8]),
                get_unaligned_le32(&buf[i + 12]));
        }
    }

    switch (OID) {
    case RNDIS_OID_GEN_CURRENT_PACKET_FILTER:

        /* these NDIS_PACKET_TYPE_* bitflags are shared with
         * cdc_filter; it's not RNDIS-specific
         * NDIS_PACKET_TYPE_x == USB_CDC_PACKET_TYPE_x for x in:
         *    PROMISCUOUS, DIRECTED,
         *    MULTICAST, ALL_MULTICAST, BROADCAST
         */
        *params->filter = (u16)get_unaligned_le32(buf);
        RNDIS_DBG("%s: RNDIS_OID_GEN_CURRENT_PACKET_FILTER %08x\n", __func__, *params->filter);

        /* this call has a significant side effect:  it's
         * what makes the packet flow start and stop, like
         * activating the CDC Ethernet altsetting.
         */
        retval = 0;
        if (*params->filter) {
            params->state = RNDIS_DATA_INITIALIZED;
            netif_set_link_up(&params->priv_dev->netif);
        } else {
            params->state = RNDIS_INITIALIZED;
            netif_set_link_down(&params->priv_dev->netif);
        }
        break;

    case RNDIS_OID_802_3_MULTICAST_LIST:
        /* I think we can ignore this */
        RNDIS_DBG("%s: RNDIS_OID_802_3_MULTICAST_LIST\n", __func__);
        retval = 0;
        break;

    default:
        printf("%s: set unknown OID 0x%08X, size %d\n", __func__, OID, buf_len);
    }

    return retval;
}

/*
 * Response Functions
 */
static int rndis_init_response(struct rndis_params *params, rndis_init_msg_type *buf)
{
    rndis_init_cmplt_type *resp;
    rndis_resp_t *r;

    r = rndis_add_response(params, sizeof(rndis_init_cmplt_type));
    if (!r)
        return -ENOMEM;

    resp = (rndis_init_cmplt_type *)r->buf;

    resp->MessageType = RNDIS_MSG_INIT_C;
    resp->MessageLength = 52;

    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    resp->Status = RNDIS_STATUS_SUCCESS;
    resp->MajorVersion = RNDIS_MAJOR_VERSION;
    resp->MinorVersion = RNDIS_MINOR_VERSION;
    resp->DeviceFlags = RNDIS_DF_CONNECTIONLESS;
    resp->Medium = RNDIS_MEDIUM_802_3;

    resp->MaxPacketsPerTransfer = 1;
    resp->MaxTransferSize = params->priv_dev->max_packet_size;
    resp->PacketAlignmentFactor = 0;
    resp->AFListOffset = 0;
    resp->AFListSize = 0;
    params->resp_avail(params->v);

    return 0;
}

static int rndis_query_response(struct rndis_params *params,
                rndis_query_msg_type *buf)
{
    rndis_query_cmplt_type *resp;
    rndis_resp_t *r;

    /*
     * we need more memory:
     * gen_ndis_query_resp expects enough space for
     * rndis_query_cmplt_type followed by data.
     * oid_supported_list is the largest data reply
     */
    r = rndis_add_response(params,
        sizeof(oid_supported_list) + sizeof(rndis_query_cmplt_type));
    if (!r)
        return -ENOMEM;
    resp = (rndis_query_cmplt_type *)r->buf;

    resp->MessageType = RNDIS_MSG_QUERY_C;
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */

    if (gen_ndis_query_resp(params, buf->OID,
            buf->InformationBufferOffset + 8 + (u8 *)buf,
            buf->InformationBufferLength, r)) {
        /* OID not supported */
        resp->Status = RNDIS_STATUS_NOT_SUPPORTED;
        resp->MessageLength = sizeof(*resp);
        resp->InformationBufferLength = 0;
        resp->InformationBufferOffset = 0;
    } else
        resp->Status = RNDIS_STATUS_SUCCESS;

    params->resp_avail(params->v);
    return 0;
}

static int rndis_set_response(struct rndis_params *params,
                  rndis_set_msg_type *buf)
{
    u32 BufLength, BufOffset;
    rndis_set_cmplt_type *resp;
    rndis_resp_t *r;

    BufLength = buf->InformationBufferLength;
    BufOffset = buf->InformationBufferOffset;
    if ((BufLength > RNDIS_MAX_TOTAL_SIZE) ||
        (BufOffset > RNDIS_MAX_TOTAL_SIZE) ||
        (BufOffset + 8 >= RNDIS_MAX_TOTAL_SIZE))
            return -EINVAL;

    r = rndis_add_response(params, sizeof(rndis_set_cmplt_type));
    if (!r)
        return -ENOMEM;
    resp = (rndis_set_cmplt_type *)r->buf;

    RNDIS_DBG("%s: Length: %d\n", __func__, BufLength);
    RNDIS_DBG("%s: Offset: %d\n", __func__, BufOffset);
    RNDIS_DBG("%s: InfoBuffer: ", __func__);

    for (int i = 0; i < BufLength; i++) {
        RNDIS_DBG("%02x ", *(((u8 *) buf) + i + 8 + BufOffset));
    }
    RNDIS_DBG("\n");

    resp->MessageType = RNDIS_MSG_SET_C;
    resp->MessageLength = 16;
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    if (gen_ndis_set_resp(params, buf->OID,
            ((u8 *)buf) + 8 + BufOffset, BufLength, r))
        resp->Status = RNDIS_STATUS_NOT_SUPPORTED;
    else
        resp->Status = RNDIS_STATUS_SUCCESS;

    params->resp_avail(params->v);
    return 0;
}

static int rndis_reset_response(struct rndis_params *params,
                rndis_reset_msg_type *buf)
{
    rndis_reset_cmplt_type *resp;
    rndis_resp_t *r;
    u8 *xbuf;
    u32 length;

    /* drain the response queue */
    while ((xbuf = rndis_get_next_response(params, &length)))
        rndis_free_response(params, xbuf);

    r = rndis_add_response(params, sizeof(rndis_reset_cmplt_type));
    if (!r)
        return -ENOMEM;
    resp = (rndis_reset_cmplt_type *)r->buf;

    resp->MessageType = RNDIS_MSG_RESET_C;
    resp->MessageLength = 16;
    resp->Status = RNDIS_STATUS_SUCCESS;
    /* resent information */
    resp->AddressingReset = 1;
    params->resp_avail(params->v);

    return 0;
}

static int rndis_keepalive_response(struct rndis_params *params,
                    rndis_keepalive_msg_type *buf)
{
    rndis_keepalive_cmplt_type *resp;
    rndis_resp_t *r;

    /* host "should" check only in RNDIS_DATA_INITIALIZED state */

    r = rndis_add_response(params, sizeof(rndis_keepalive_cmplt_type));
    if (!r)
        return -ENOMEM;
    resp = (rndis_keepalive_cmplt_type *)r->buf;

    resp->MessageType = RNDIS_MSG_KEEPALIVE_C;
    resp->MessageLength = 16;
    resp->RequestID = buf->RequestID; /* Still LE in msg buffer */
    resp->Status = RNDIS_STATUS_SUCCESS;

    params->resp_avail(params->v);
    return 0;
}


/*
 * Device to Host Comunication
 */
static int rndis_indicate_status_msg(struct rndis_params *params, u32 status)
{
    rndis_indicate_status_msg_type *resp;
    rndis_resp_t *r;

    if (params->state == RNDIS_UNINITIALIZED)
        return -EPERM;

    r = rndis_add_response(params, sizeof(rndis_indicate_status_msg_type));
    if (!r)
        return -ENOMEM;
    resp = (rndis_indicate_status_msg_type *)r->buf;

    resp->MessageType = RNDIS_MSG_INDICATE;
    resp->MessageLength = 20;
    resp->Status = status;
    resp->StatusBufferLength = 0;
    resp->StatusBufferOffset = 0;

    params->resp_avail(params->v);
    return 0;
}

int rndis_signal_connect(struct rndis_params *params)
{
    params->media_state = RNDIS_MEDIA_STATE_CONNECTED;
    return rndis_indicate_status_msg(params, RNDIS_STATUS_MEDIA_CONNECT);
}

int rndis_signal_disconnect(struct rndis_params *params)
{
    params->media_state = RNDIS_MEDIA_STATE_DISCONNECTED;
    return rndis_indicate_status_msg(params, RNDIS_STATUS_MEDIA_DISCONNECT);
}

void rndis_uninit(struct rndis_params *params)
{
    u8 *buf;
    u32 length;

    if (!params)
        return;
    params->state = RNDIS_UNINITIALIZED;

    /* drain the response queue */
    while ((buf = rndis_get_next_response(params, &length)))
        rndis_free_response(params, buf);
}

/*
 * Message Parser
 */
int rndis_msg_parser(struct rndis_params *params, u8 *buf)
{
    u32 MsgType, MsgLength;
    u32 *tmp;

    if (!buf)
        return -ENOMEM;

    tmp = (u32*)buf;
    MsgType   = get_unaligned_le32(tmp++);
    MsgLength = get_unaligned_le32(tmp++);

    if (!params)
        return -EPERM;

    /* NOTE: RNDIS is *EXTREMELY* chatty ... Windows constantly polls for
     * rx/tx statistics and link status, in addition to KEEPALIVE traffic
     * and normal HC level polling to see if there's any IN traffic.
     */

    /* For USB: responses may take up to 10 seconds */
    switch (MsgType) {
    case RNDIS_MSG_INIT:
        RNDIS_DBG("%s: RNDIS_MSG_INIT\n", __func__);
        params->state = RNDIS_INITIALIZED;
        return rndis_init_response(params, (rndis_init_msg_type *)buf);

    case RNDIS_MSG_HALT:
        RNDIS_DBG("%s: RNDIS_MSG_HALT\n", __func__);
        params->state = RNDIS_UNINITIALIZED;
        netif_set_link_down(&params->priv_dev->netif);
        return 0;

    case RNDIS_MSG_QUERY:
        return rndis_query_response(params, (rndis_query_msg_type *)buf);

    case RNDIS_MSG_SET:
        return rndis_set_response(params, (rndis_set_msg_type *)buf);

    case RNDIS_MSG_RESET:
        RNDIS_DBG("%s: RNDIS_MSG_RESET\n", __func__);
        return rndis_reset_response(params, (rndis_reset_msg_type *)buf);

    case RNDIS_MSG_KEEPALIVE:
        /* For USB: host does this every 5 seconds */
        RNDIS_DBG("%s: RNDIS_MSG_KEEPALIVE\n", __func__);
        return rndis_keepalive_response(params, (rndis_keepalive_msg_type *)buf);

    default:
        /* At least Windows XP emits some undefined RNDIS messages.
         * In one case those messages seemed to relate to the host
         * suspending itself.
         */
        printf("%s: unknown RNDIS message 0x%08x len %d\n", __func__, MsgType, MsgLength);
        break;
    }

    return -EPERM;
}

struct rndis_params *rndis_register(void (*resp_avail)(void *v), void *v)
{
    struct rndis_params *params;

    if (!resp_avail)
        return ERR_PTR(-EINVAL);

    params = malloc(sizeof(struct rndis_params));
    if (!params) {
        return ERR_PTR(-ENOMEM);
    }
    memset(params, 0, sizeof(struct rndis_params));

    params->state = RNDIS_UNINITIALIZED;
    params->media_state = RNDIS_MEDIA_STATE_DISCONNECTED;
    params->resp_avail = resp_avail;
    params->v = v;
    INIT_LIST_HEAD(&(params->resp_queue));
    spin_lock_init(&params->resp_lock);

    return params;
}

void rndis_deregister(struct rndis_params *params)
{
    if (params)
        free(params);
}

int rndis_set_param_filter(struct rndis_params *params,  u16 *cdc_filter)
{
    if (!params)
        return -1;

    params->filter = cdc_filter;
    return 0;
}

int rndis_set_param_vendor(struct rndis_params *params, u32 vendorID,
               const char *vendorDescr)
{
    if (!vendorDescr) return -1;
    if (!params) return -1;

    params->vendorID = vendorID;
    params->vendorDescr = vendorDescr;

    return 0;
}

int rndis_set_param_medium(struct rndis_params *params, u32 medium, u32 speed)
{
    if (!params)
        return -1;

    params->medium = medium;
    params->speed = speed;

    return 0;
}
