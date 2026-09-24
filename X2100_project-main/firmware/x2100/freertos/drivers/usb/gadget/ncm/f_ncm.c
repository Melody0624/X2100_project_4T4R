// SPDX-License-Identifier: GPL-2.0+
/*
 * f_ncm.c -- USB CDC Network (NCM) link function driver
 *
 * Copyright (C) 2010 Nokia Corporation
 * Contact: Yauheni Kaliuta <yauheni.kaliuta@nokia.com>
 *
 * The driver borrows from f_ecm.c which is:
 *
 * Copyright (C) 2003-2005,2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 */

#include <os.h>
#include <assert.h>
#include <common.h>
#include <crc32.h>
#include <driver/hrtimer.h>

#include <usb/ch9.h>
#include <usb/cdc.h>
#include "../composite.h"

#include "f_ncm.h"
#include "u_ether.h"

/*
 * This function is a "CDC Network Control Model" (CDC NCM) Ethernet link.
 * NCM is intended to be used with high-speed network attachments.
 */

#define DEFAULT_QLEN            10

#define NCM_NDP_HDR_CRC         0x01000000

enum ncm_notify_state {
    NCM_NOTIFY_NONE,        /* don't notify */
    NCM_NOTIFY_CONNECT,     /* issue CONNECT next */
    NCM_NOTIFY_SPEED,       /* issue SPEED_CHANGE next */
};

struct f_ncm {
    struct eth_dev      dev;
    u8                  ctrl_id, data_id;
    char                ethaddr[14];

    struct usb_ep       *notify;
    struct usb_request  *notify_req;
    u8                  notify_state;
    int                 notify_count;
    bool                is_open;

    const struct ndp_parser_opts    *parser_opts;
    bool                is_crc;
    u32                 ndp_sign;

    unsigned long       flags;
    spinlock_t          lock;

    volatile bool       tx_in_used;
    void                *tx_ndp, *tx_data;
    u32                 tx_ndp_size, tx_data_size;
    u32                 tx_ndp_len, tx_data_len;
    u16                 ndp_dgram_count;
    struct hrtimer      task_timer;
};

static inline struct f_ncm *func_to_ncm(struct usb_function *f)
{
    return container_of(f, struct f_ncm, dev.func);
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

#define CRC32_POLYNOMIAL 0xEDB88320

static u32 crc32_le(u32 crc, unsigned char const *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];

        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/*-------------------------------------------------------------------------*/

#define NTB_DEFAULT_IN_SIZE     16384
#define NTB_OUT_SIZE            16384

#define TX_MAX_NUM_DPE          32
#define TX_TIMEOUT_USECS        300

#define FORMATS_SUPPORTED       (USB_CDC_NCM_NTB16_SUPPORTED |  \
                                USB_CDC_NCM_NTB32_SUPPORTED)

static struct usb_cdc_ncm_ntb_parameters ntb_parameters = {
    .wLength                = sizeof(ntb_parameters),
    .bmNtbFormatsSupported  = FORMATS_SUPPORTED,
    .dwNtbInMaxSize         = NTB_DEFAULT_IN_SIZE,
    .wNdpInDivisor          = 4,
    .wNdpInPayloadRemainder = 0,
    .wNdpInAlignment        = 4,

    .dwNtbOutMaxSize        = NTB_OUT_SIZE,
    .wNdpOutDivisor         = 4,
    .wNdpOutPayloadRemainder = 0,
    .wNdpOutAlignment       = 4,
};

#define NCM_STATUS_INTERVAL_MS  32
#define NCM_STATUS_BYTECOUNT    16    /* 8 byte header + data */

static struct usb_interface_assoc_descriptor ncm_iad_desc = {
    .bLength            =    sizeof ncm_iad_desc,
    .bDescriptorType    =    USB_DT_INTERFACE_ASSOCIATION,

    /* .bFirstInterface =    DYNAMIC, */
    .bInterfaceCount    =    2,    /* control + data */
    .bFunctionClass     =    USB_CLASS_COMM,
    .bFunctionSubClass  =    USB_CDC_SUBCLASS_NCM,
    .bFunctionProtocol  =    USB_CDC_PROTO_NONE,
    /* .iFunction =        DYNAMIC */
};

/* interface descriptor: */

static struct usb_interface_descriptor ncm_control_intf = {
    .bLength            =    sizeof ncm_control_intf,
    .bDescriptorType    =    USB_DT_INTERFACE,

    /* .bInterfaceNumber = DYNAMIC */
    .bNumEndpoints      =    1,
    .bInterfaceClass    =    USB_CLASS_COMM,
    .bInterfaceSubClass =    USB_CDC_SUBCLASS_NCM,
    .bInterfaceProtocol =    USB_CDC_PROTO_NONE,
    /* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc ncm_header_desc = {
    .bLength            =    sizeof ncm_header_desc,
    .bDescriptorType    =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType =    USB_CDC_HEADER_TYPE,

    .bcdCDC             =    0x0110,
};

static struct usb_cdc_union_desc ncm_union_desc = {
    .bLength                =    sizeof(ncm_union_desc),
    .bDescriptorType        =    USB_DT_CS_INTERFACE,
    .bDescriptorSubType     =    USB_CDC_UNION_TYPE,
    /* .bMasterInterface0   =    DYNAMIC */
    /* .bSlaveInterface0    =    DYNAMIC */
};

static struct usb_cdc_ether_desc ecm_desc = {
    .bLength                =   sizeof ecm_desc,
    .bDescriptorType        =   USB_DT_CS_INTERFACE,
    .bDescriptorSubType     =   USB_CDC_ETHERNET_TYPE,

    /* this descriptor actually adds value, surprise! */
    /* .iMACAddress = DYNAMIC */
    .bmEthernetStatistics   =   0, /* no statistics */
    .wMaxSegmentSize        =   ETH_FRAME_LEN,
    .wNumberMCFilters       =   0,
    .bNumberPowerFilters    =   0,
};

#define NCAPS   (USB_CDC_NCM_NCAP_ETH_FILTER | USB_CDC_NCM_NCAP_CRC_MODE)

static struct usb_cdc_ncm_desc ncm_desc = {
    .bLength            =   sizeof ncm_desc,
    .bDescriptorType    =   USB_DT_CS_INTERFACE,
    .bDescriptorSubType =   USB_CDC_NCM_TYPE,

    .bcdNcmVersion      =   0x0100,
    /* can process SetEthernetPacketFilter */
    .bmNetworkCapabilities = NCAPS,
};

/* the default data interface has no endpoints ... */

static struct usb_interface_descriptor ncm_data_nop_intf = {
    .bLength            =   sizeof ncm_data_nop_intf,
    .bDescriptorType    =   USB_DT_INTERFACE,

    .bInterfaceNumber   =   1,
    .bAlternateSetting  =   0,
    .bNumEndpoints      =   0,
    .bInterfaceClass    =   USB_CLASS_CDC_DATA,
    .bInterfaceSubClass =   0,
    .bInterfaceProtocol =   USB_CDC_NCM_PROTO_NTB,
    /* .iInterface = DYNAMIC */
};

static struct usb_interface_descriptor ncm_data_intf = {
    .bLength            =   sizeof ncm_data_intf,
    .bDescriptorType    =   USB_DT_INTERFACE,

    .bInterfaceNumber   =   1,
    .bAlternateSetting  =   1,
    .bNumEndpoints      =   2,
    .bInterfaceClass    =   USB_CLASS_CDC_DATA,
    .bInterfaceSubClass =   0,
    .bInterfaceProtocol =   USB_CDC_NCM_PROTO_NTB,
    /* .iInterface = DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor ncm_fs_notify_desc = {
    .bLength            =   USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    =   USB_DT_ENDPOINT,

    .bEndpointAddress   =   USB_DIR_IN,
    .bmAttributes       =   USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize     =   NCM_STATUS_BYTECOUNT,
    .bInterval          =   NCM_STATUS_INTERVAL_MS,
};

static struct usb_endpoint_descriptor ncm_fs_in_desc = {
    .bLength            =   USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    =   USB_DT_ENDPOINT,

    .bEndpointAddress   =   USB_DIR_IN,
    .bmAttributes       =   USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor ncm_fs_out_desc = {
    .bLength            =   USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    =   USB_DT_ENDPOINT,

    .bEndpointAddress   =   USB_DIR_OUT,
    .bmAttributes       =   USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *ncm_fs_function[] = {
    (struct usb_descriptor_header *) &ncm_iad_desc,
    /* CDC NCM control descriptors */
    (struct usb_descriptor_header *) &ncm_control_intf,
    (struct usb_descriptor_header *) &ncm_header_desc,
    (struct usb_descriptor_header *) &ncm_union_desc,
    (struct usb_descriptor_header *) &ecm_desc,
    (struct usb_descriptor_header *) &ncm_desc,
    (struct usb_descriptor_header *) &ncm_fs_notify_desc,
    /* data interface, altsettings 0 and 1 */
    (struct usb_descriptor_header *) &ncm_data_nop_intf,
    (struct usb_descriptor_header *) &ncm_data_intf,
    (struct usb_descriptor_header *) &ncm_fs_in_desc,
    (struct usb_descriptor_header *) &ncm_fs_out_desc,
    NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor ncm_hs_notify_desc = {
    .bLength            =    USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    =    USB_DT_ENDPOINT,

    .bEndpointAddress   =    USB_DIR_IN,
    .bmAttributes       =    USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize     =    NCM_STATUS_BYTECOUNT,
    .bInterval          =    USB_MS_TO_HS_INTERVAL(NCM_STATUS_INTERVAL_MS),
};

static struct usb_endpoint_descriptor ncm_hs_in_desc = {
    .bLength            =    USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    =    USB_DT_ENDPOINT,

    .bEndpointAddress   =    USB_DIR_IN,
    .bmAttributes       =    USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize     =    512,
};

static struct usb_endpoint_descriptor ncm_hs_out_desc = {
    .bLength            =    USB_DT_ENDPOINT_SIZE,
    .bDescriptorType    =    USB_DT_ENDPOINT,

    .bEndpointAddress   =    USB_DIR_OUT,
    .bmAttributes       =    USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize     =    512,
};

static struct usb_descriptor_header *ncm_hs_function[] = {
    (struct usb_descriptor_header *) &ncm_iad_desc,

    /* CDC NCM control descriptors */
    (struct usb_descriptor_header *) &ncm_control_intf,
    (struct usb_descriptor_header *) &ncm_header_desc,
    (struct usb_descriptor_header *) &ncm_union_desc,
    (struct usb_descriptor_header *) &ecm_desc,
    (struct usb_descriptor_header *) &ncm_desc,
    (struct usb_descriptor_header *) &ncm_hs_notify_desc,

    /* data interface, altsettings 0 and 1 */
    (struct usb_descriptor_header *) &ncm_data_nop_intf,
    (struct usb_descriptor_header *) &ncm_data_intf,
    (struct usb_descriptor_header *) &ncm_hs_in_desc,
    (struct usb_descriptor_header *) &ncm_hs_out_desc,
    NULL,
};

static struct netif *ncm_active_netif;

/* string descriptors: */

#define STRING_CTRL_IDX     0
#define STRING_MAC_IDX      1
#define STRING_DATA_IDX     2
#define STRING_IAD_IDX      3

static struct usb_string ncm_string_defs[] = {
    [STRING_CTRL_IDX].s = "CDC Network Control Model (NCM)",
    [STRING_MAC_IDX].s = "",
    [STRING_DATA_IDX].s = "CDC Network Data",
    [STRING_IAD_IDX].s = "CDC NCM",
    {  } /* end of list */
};

static struct usb_gadget_strings ncm_string_table = {
    .language   =       0x0409,    /* en-us */
    .strings    =       ncm_string_defs,
};

static struct usb_gadget_strings *ncm_strings[] = {
    &ncm_string_table,
    NULL,
};

struct ndp_parser_opts {
    u32        nth_sign;
    u32        ndp_sign;
    unsigned    nth_size;
    unsigned    ndp_size;
    unsigned    dpe_size;
    unsigned    ndplen_align;
    /* sizes in u16 units */
    unsigned    dgram_item_len; /* index or length */
    unsigned    block_length;
    unsigned    ndp_index;
    unsigned    reserved1;
    unsigned    reserved2;
    unsigned    next_ndp_index;
};

#define INIT_NDP16_OPTS {                   \
        .nth_sign = USB_CDC_NCM_NTH16_SIGN, \
        .ndp_sign = USB_CDC_NCM_NDP16_NOCRC_SIGN,       \
        .nth_size = sizeof(struct usb_cdc_ncm_nth16),   \
        .ndp_size = sizeof(struct usb_cdc_ncm_ndp16),   \
        .dpe_size = sizeof(struct usb_cdc_ncm_dpe16),   \
        .ndplen_align = 4,                  \
        .dgram_item_len = 1,                \
        .block_length = 1,                  \
        .ndp_index = 1,                     \
        .reserved1 = 0,                     \
        .reserved2 = 0,                     \
        .next_ndp_index = 1,                \
    }

#define INIT_NDP32_OPTS {                   \
        .nth_sign = USB_CDC_NCM_NTH32_SIGN, \
        .ndp_sign = USB_CDC_NCM_NDP32_NOCRC_SIGN,       \
        .nth_size = sizeof(struct usb_cdc_ncm_nth32),   \
        .ndp_size = sizeof(struct usb_cdc_ncm_ndp32),   \
        .dpe_size = sizeof(struct usb_cdc_ncm_dpe32),   \
        .ndplen_align = 8,                  \
        .dgram_item_len = 2,                \
        .block_length = 2,                  \
        .ndp_index = 2,                     \
        .reserved1 = 1,                     \
        .reserved2 = 2,                     \
        .next_ndp_index = 2,                \
    }

static const struct ndp_parser_opts ndp16_opts = INIT_NDP16_OPTS;
static const struct ndp_parser_opts ndp32_opts = INIT_NDP32_OPTS;

static inline void put_ncm(u16 **p, unsigned size, unsigned val)
{
    switch (size) {
    case 1:
        put_unaligned_le16((u16)val, *p);
        break;
    case 2:
        put_unaligned_le32((u32)val, *p);
        break;
    default:
        break;
    }

    *p += size;
}

static unsigned get_ncm(u16 **p, unsigned size)
{
    unsigned tmp = 0;

    switch (size) {
    case 1:
        tmp = get_unaligned_le16(*p);
        break;
    case 2:
        tmp = get_unaligned_le32(*p);
        break;
    default:
        break;
    }

    *p += size;
    return tmp;
}

/*-------------------------------------------------------------------------*/

static inline void ncm_reset_values(struct f_ncm *ncm)
{
    struct eth_dev *dev = &ncm->dev;

    ncm->is_crc = false;
    ncm->parser_opts = &ndp16_opts;
    ncm->ndp_sign = ncm->parser_opts->ndp_sign;

    /* header_len doesn't make sense for ncm, fixed size used */
    dev->cdc_filter = DEFAULT_FILTER;
    dev->fixed_out_len = ntb_parameters.dwNtbOutMaxSize;
    dev->fixed_in_len = ntb_parameters.dwNtbInMaxSize;
}

/*
 * Context: ncm->lock held
 */
static void ncm_do_notify(struct f_ncm *ncm)
{
    struct eth_dev             *dev = &ncm->dev;
    struct usb_request        *req = ncm->notify_req;
    struct usb_cdc_notification    *event;
    struct usb_composite_dev    *cdev = dev->func.config->cdev;
    u32                *data;
    int                status;

    /* notification already in flight? */

    if (ncm->notify_count)
        return;

    event = req->buf;
    switch (ncm->notify_state) {
    case NCM_NOTIFY_NONE:
        return;

    case NCM_NOTIFY_CONNECT:
        event->bNotificationType = USB_CDC_NOTIFY_NETWORK_CONNECTION;
        if (ncm->is_open)
            event->wValue = 1;
        else
            event->wValue = 0;
        event->wLength = 0;
        req->length = sizeof *event;

        ncm->notify_state = NCM_NOTIFY_NONE;
        break;

    case NCM_NOTIFY_SPEED:
        event->bNotificationType = USB_CDC_NOTIFY_SPEED_CHANGE;
        event->wValue = 0;
        event->wLength = 8;
        req->length = NCM_STATUS_BYTECOUNT;

        /* SPEED_CHANGE data is up/down speeds in bits/sec */
        data = req->buf + sizeof *event;
        data[0] = gether_bitrate(cdev->gadget);
        data[1] = data[0];

        ncm->notify_state = NCM_NOTIFY_CONNECT;
        break;
    }
    event->bmRequestType = 0xA1;
    event->wIndex = ncm->ctrl_id;

    ncm->notify_count++;

    /*
     * In double buffering if there is a space in FIFO,
     * completion callback can be called right after the call,
     * so unlocking
     */
    usb_spin_unlock_irqrestore(&ncm->lock, ncm->flags);
    status = usb_ep_queue(ncm->notify, req);
    usb_spin_lock_irqsave(&ncm->lock, ncm->flags);
    if (status < 0) {
        ncm->notify_count--;
    }
}

/*
 * Context: ncm->lock held
 */
static void ncm_notify(struct f_ncm *ncm)
{
    ncm->notify_state = NCM_NOTIFY_SPEED;
    ncm_do_notify(ncm);
}

static void ncm_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
    struct f_ncm            *ncm = req->context;
    unsigned long flags;

    usb_spin_lock_irqsave(&ncm->lock, flags);
    switch (req->status) {
    case 0:
        ncm->notify_count--;
        break;
    case -ECONNRESET:
    case -ESHUTDOWN:
        ncm->notify_count = 0;
        ncm->notify_state = NCM_NOTIFY_NONE;
        break;
    default:
        ncm->notify_count--;
        break;
    }
    ncm_do_notify(ncm);
    usb_spin_unlock_irqrestore(&ncm->lock, flags);
}

static void ncm_ep0out_complete(struct usb_ep *ep, struct usb_request *req)
{
    /* now for SET_NTB_INPUT_SIZE only */
    unsigned            in_size;
    struct usb_function *f = req->context;
    struct f_ncm        *ncm = func_to_ncm(f);

    req->context = NULL;
    if (req->status || req->actual != req->length) {
        NCM_DBG("Bad control-OUT transfer\n");
        goto invalid;
    }

    in_size = get_unaligned_le32(req->buf);
    if (in_size < USB_CDC_NCM_NTB_MIN_IN_SIZE ||
        in_size > ntb_parameters.dwNtbInMaxSize) {
        NCM_DBG("Got wrong INPUT SIZE (%d) from host\n", in_size);
        goto invalid;
    }

    ncm->dev.fixed_in_len = in_size;

    return;
invalid:
    usb_ep_set_halt(ep);
    return;
}

static bool ncm_req_match(struct usb_function *f, const struct usb_ctrlrequest *ctrl, bool config0)
{
    struct f_ncm        *ncm = func_to_ncm(f);
    u16            w_index = ctrl->wIndex;

    if (config0 || (w_index != ncm->ctrl_id))
        return false;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
            | USB_CDC_SET_ETHERNET_PACKET_FILTER:
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_NTB_PARAMETERS:
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_NTB_INPUT_SIZE:
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_SET_NTB_INPUT_SIZE:
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_NTB_FORMAT:
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_SET_NTB_FORMAT:
        break;

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_CRC_MODE:
        break;

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_SET_CRC_MODE:
        break;

    default:
        return false;
    }

    return true;
}

static int ncm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
    struct f_ncm        *ncm = func_to_ncm(f);
    struct eth_dev        *dev = &ncm->dev;
    struct usb_composite_dev *cdev = f->config->cdev;
    struct usb_request    *req = cdev->req;
    int            value = -EOPNOTSUPP;
    u16            w_value = ctrl->wValue;
    u16            w_length = ctrl->wLength;

    switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
            | USB_CDC_SET_ETHERNET_PACKET_FILTER:
    {
        /*
         * see 6.2.30: no data, wIndex = interface,
         * wValue = packet filter bitmap
         */
        if (w_length != 0)
            goto invalid;
        NCM_DBG("packet filter %02x\n", w_value);
        /*
         * REVISIT locking of cdc_filter.  This assumes the UDC
         * driver won't have a concurrent packet TX irq running on
         * another CPU; or that if it does, this write is atomic...
         */
        dev->cdc_filter = w_value;
        value = 0;
        break;
    }

    /*
     * and optionally:
     * case USB_CDC_SEND_ENCAPSULATED_COMMAND:
     * case USB_CDC_GET_ENCAPSULATED_RESPONSE:
     * case USB_CDC_SET_ETHERNET_MULTICAST_FILTERS:
     * case USB_CDC_SET_ETHERNET_PM_PATTERN_FILTER:
     * case USB_CDC_GET_ETHERNET_PM_PATTERN_FILTER:
     * case USB_CDC_GET_ETHERNET_STATISTIC:
     */

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_NTB_PARAMETERS:
    {
        if (w_length == 0 || w_value != 0)
            goto invalid;
        value = w_length > sizeof(ntb_parameters) ?
            sizeof(ntb_parameters) : w_length;
        memcpy(req->buf, &ntb_parameters, value);
        NCM_DBG("Host asked NTB parameters\n");
        break;
    }

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_NTB_INPUT_SIZE:
    {
        if (w_length < 4 || w_value != 0)
            goto invalid;
        put_unaligned_le32(dev->fixed_in_len, req->buf);
        value = 4;
        NCM_DBG("Host asked INPUT SIZE, sending %d\n",
             dev->fixed_in_len);
        break;
    }

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_SET_NTB_INPUT_SIZE:
    {
        if (w_length != 4 || w_value != 0)
            goto invalid;
        req->complete = ncm_ep0out_complete;
        req->length = w_length;
        req->context = f;

        value = req->length;
        break;
    }

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_NTB_FORMAT:
    {
        uint16_t format;

        if (w_length < 2 || w_value != 0)
            goto invalid;
        format = (ncm->parser_opts == &ndp16_opts) ? 0x0000 : 0x0001;
        put_unaligned_le16(format, req->buf);
        value = 2;
        NCM_DBG("Host asked NTB FORMAT, sending %d\n", format);
        break;
    }

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_SET_NTB_FORMAT:
    {
        if (w_length != 0)
            goto invalid;
        const struct ndp_parser_opts *org = ncm->parser_opts;
        switch (w_value) {
        case 0x0000:
            ncm->parser_opts = &ndp16_opts;
            NCM_DBG("NCM16 selected\n");
            break;
        case 0x0001:
            ncm->parser_opts = &ndp32_opts;
            NCM_DBG("NCM32 selected\n");
            break;
        default:
            goto invalid;
        }
        value = 0;

        if (ncm->parser_opts != org) {
            u32 new_len = ncm->parser_opts->ndp_size +
                ncm->parser_opts->dpe_size * TX_MAX_NUM_DPE;

            if (ncm->tx_ndp)
                free(ncm->tx_ndp);
            ncm->tx_ndp = malloc(new_len);
            ncm->tx_ndp_size = new_len;
        }
        break;
    }

    case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_GET_CRC_MODE:
    {
        uint16_t is_crc;

        if (w_length < 2 || w_value != 0)
            goto invalid;
        is_crc = ncm->is_crc ? 0x0001 : 0x0000;
        put_unaligned_le16(is_crc, req->buf);
        value = 2;
        NCM_DBG("Host asked CRC MODE, sending %d\n", is_crc);
        break;
    }

    case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
        | USB_CDC_SET_CRC_MODE:
    {
        if (w_length != 0)
            goto invalid;
        switch (w_value) {
        case 0x0000:
            ncm->is_crc = false;
            NCM_DBG("non-CRC mode selected\n");
            break;
        case 0x0001:
            ncm->is_crc = true;
            NCM_DBG("CRC mode selected\n");
            break;
        default:
            goto invalid;
        }
        value = 0;
        break;
    }

    /* and disabled in ncm descriptor: */
    /* case USB_CDC_GET_NET_ADDRESS: */
    /* case USB_CDC_SET_NET_ADDRESS: */
    /* case USB_CDC_GET_MAX_DATAGRAM_SIZE: */
    /* case USB_CDC_SET_MAX_DATAGRAM_SIZE: */

    default:
invalid:
        NCM_DBG("invalid control req%02x.%02x v%04x i%04x l%d\n",
            ctrl->bRequestType, ctrl->bRequest,
            w_value, w_index, w_length);
    }
    ncm->ndp_sign = ncm->parser_opts->ndp_sign |
        (ncm->is_crc ? NCM_NDP_HDR_CRC : 0);

    /* respond with data transfer or status phase? */
    if (value >= 0) {
        NCM_DBG("ncm req %02x.%02x v%04x i%04x l%d\n",
            ctrl->bRequestType, ctrl->bRequest,
            w_value, w_index, w_length);
        req->zero = 0;
        req->length = value;
        value = usb_ep_queue(cdev->gadget->ep0, req);
        if (value < 0)
            NCM_DBG("ncm req %02x.%02x response err %d\n",
                ctrl->bRequestType, ctrl->bRequest, value);
    }

    /* device either stalls (value < 0) or reports success */
    return value;
}

/*-------------------------------------------------------------------------*/

static void ncm_notify_connect(struct eth_dev *dev)
{
    struct f_ncm *ncm = func_to_ncm(&dev->func);

    usb_spin_lock_irqsave(&ncm->lock, ncm->flags);
    ncm->is_open = true;
    ncm_notify(ncm);
    usb_spin_unlock_irqrestore(&ncm->lock, ncm->flags);
}

static void ncm_notify_disconnect(struct eth_dev *dev)
{
    struct f_ncm *ncm = func_to_ncm(&dev->func);

    usb_spin_lock_irqsave(&ncm->lock, ncm->flags);
    ncm->is_open = false;
    ncm_notify(ncm);
    usb_spin_unlock_irqrestore(&ncm->lock, ncm->flags);
}

int gether_connect(struct eth_dev *dev)
{
    int ret = 0;

    if (!dev)
        return -EINVAL;

    dev->in_ep->driver_data = dev;
    ret = usb_ep_enable(dev->in_ep);
    if (ret != 0) {
        NCM_DBG("enable %s --> %d\n", dev->in_ep->name, ret);
        return ret;
    }

    dev->out_ep->driver_data = dev;
    ret = usb_ep_enable(dev->out_ep);
    if (ret != 0) {
        NCM_DBG("enable %s --> %d\n", dev->out_ep->name, ret);
        usb_ep_disable(dev->in_ep);
        return ret;
    }

    dev->is_connect = true;
    if (netif_is_up(&dev->netif)) {
        ncm_notify_connect(dev);
        netif_set_link_up(&dev->netif);
        ueth_start_rx_queue(dev);
    } else {
        ncm_notify_disconnect(dev);
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

static int ncm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
    int ret;
    struct f_ncm        *ncm = func_to_ncm(f);
    struct eth_dev         *dev = &ncm->dev;
    struct usb_composite_dev *cdev = f->config->cdev;

    /* Control interface has only altsetting 0 */
    if (intf == ncm->ctrl_id) {
        if (alt != 0)
            goto fail;

        usb_ep_disable(ncm->notify);

        if (!(ncm->notify->desc)) {
            if (config_ep_by_speed(cdev->gadget, f, ncm->notify))
                goto fail;
        }
        usb_ep_enable(ncm->notify);

    /* Data interface has two altsettings, 0 and 1 */
    } else if (intf == ncm->data_id) {
        if (alt > 1)
            goto fail;

        if (dev->in_ep->enabled) {
            gether_disconnect(dev);
            ncm_reset_values(ncm);
        }

        /*
         * CDC Network only sends data in non-default altsettings.
         * Changing altsettings resets filters, statistics, etc.
         */
        if (alt == 1) {
            if (!ncm->dev.in_ep->desc || !ncm->dev.out_ep->desc) {
                if (config_ep_by_speed(cdev->gadget, f, ncm->dev.in_ep) || \
                    config_ep_by_speed(cdev->gadget, f, ncm->dev.out_ep)) {
                    ncm->dev.in_ep->desc = NULL;
                    ncm->dev.out_ep->desc = NULL;
                    goto fail;
                }
            }

            ncm->dev.cdc_filter = DEFAULT_FILTER;
            ret = gether_connect(dev);
            if (ret < 0)
                return ret;
        }

        usb_spin_lock_irqsave(&ncm->lock, ncm->flags);
        ncm_notify(ncm);
        usb_spin_unlock_irqrestore(&ncm->lock, ncm->flags);
    } else {
        goto fail;
    }

    return 0;
fail:
    return -EINVAL;
}

static int ncm_get_alt(struct usb_function *f, unsigned intf)
{
    struct f_ncm        *ncm = func_to_ncm(f);

    if (intf == ncm->ctrl_id)
        return 0;
    return ncm->dev.in_ep->enabled ? 1 : 0;
}

static void ncm_disable(struct usb_function *f)
{
    struct f_ncm *ncm = func_to_ncm(f);

    if (!ncm->notify->enabled)
        return;

    ncm_active_netif = NULL;
    gether_disconnect(&ncm->dev);

    usb_ep_disable(ncm->notify);
    ncm->notify->desc = NULL;
}

/*-------------------------------------------------------------------------*/

static int ncm_tx_buf_alloc(struct f_ncm *ncm)
{
    struct eth_dev *dev = &ncm->dev;
    const struct ndp_parser_opts *opts = ncm->parser_opts;

    ncm->tx_data_size = dev->fixed_in_len;
    ncm->tx_data = malloc(ncm->tx_data_size);
    if (!ncm->tx_data)
        return -ENOMEM;

    ncm->tx_ndp_size = opts->ndp_size + opts->dpe_size * TX_MAX_NUM_DPE;
    ncm->tx_ndp = malloc(ncm->tx_ndp_size);
    if (!ncm->tx_ndp) {
        free(ncm->tx_data);
        return -ENOMEM;
    }

    return 0;
}

static void ncm_tx_buf_free(struct f_ncm *ncm)
{
    if (ncm->tx_data)
        free(ncm->tx_data);
    if (ncm->tx_ndp)
        free(ncm->tx_ndp);
}

static struct usb_request *ncm_req_alloc(struct usb_ep *ep, unsigned len)
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

static void ncm_req_free(struct usb_ep *ep, struct usb_request *req)
{
    if (ep != NULL && req != NULL) {
        free(req->buf);
        usb_ep_free_request(ep, req);
    }
}

/*-------------------------------------------------------------------------*/

static int package_for_tx(struct f_ncm *ncm, struct usb_request *req)
{
    u16         *ntb_iter;
    unsigned    ndp_pad;
    unsigned    ndp_index;
    unsigned    new_len;
    unsigned    offset = 0;
    void        *buf = req->buf;

    const struct ndp_parser_opts *opts = ncm->parser_opts;
    const int ndp_align = ntb_parameters.wNdpInAlignment;
    const int dgram_idx_len = 2 * 2 * opts->dgram_item_len;

    ncm->tx_in_used = false;

    /* Stop the timer */
    hrtimer_try_to_cancel(&ncm->task_timer);

    ndp_pad = ALIGN(ncm->tx_data_len, ndp_align) - ncm->tx_data_len;
    ndp_index = ncm->tx_data_len + ndp_pad;
    new_len = ndp_index + dgram_idx_len + ncm->tx_ndp_len;

    /* Set the final BlockLength and wNdpIndex */
    ntb_iter = (void *) ncm->tx_data;
    /* Increment pointer to BlockLength */
    ntb_iter += 2 + 1 + 1;
    put_ncm(&ntb_iter, opts->block_length, new_len);
    put_ncm(&ntb_iter, opts->ndp_index, ndp_index);

    /* Set the final NDP wLength */
    new_len = opts->ndp_size + (ncm->ndp_dgram_count * dgram_idx_len);
    ncm->ndp_dgram_count = 0;
    /* Increment from start to wLength */
    ntb_iter = (void *) ncm->tx_ndp;
    ntb_iter += 2;
    put_unaligned_le16(new_len, ntb_iter);

    /* Copy Data. */
    memcpy(buf + offset, ncm->tx_data, ncm->tx_data_len);
    offset = ncm->tx_data_len;

    /* Insert NDP alignment. */
    memset(buf + offset, 0, ndp_pad);
    offset += ndp_pad;

    /* Copy NDP. */
    memcpy(buf + offset, ncm->tx_ndp, ncm->tx_ndp_len);
    offset += ncm->tx_ndp_len;

    /* Insert zero'd datagram. */
    memset(buf + offset, 0, dgram_idx_len);
    offset += dgram_idx_len;

    return offset;
}

int ncm_wrap_ntb(struct usb_request *req, struct pbuf *p)
{
    struct f_ncm *ncm = (struct f_ncm *) req->context;
    struct eth_dev *dev = &ncm->dev;
    const struct ndp_parser_opts *opts = ncm->parser_opts;

    int         ncb_len = 0;
    u16         *ntb_data, *ntb_ndp;
    int         dgram_pad;
    const int   ndp_align = ntb_parameters.wNdpInAlignment;
    const int   div = ntb_parameters.wNdpInDivisor;
    const int   rem = ntb_parameters.wNdpInPayloadRemainder;
    const int   dgram_idx_len = 2 * 2 * opts->dgram_item_len;
    u32         max_size = dev->fixed_in_len;
    u32         crc = 0;
    u32         tot_len = 0;
    int         ret = 0;

    if (p) {
        tot_len = p->tot_len;

        /* Add the CRC if required up front */
        if (ncm->is_crc) {
            crc = ~crc32_le(~0, (const unsigned char *)p->payload, p->tot_len);
            tot_len += sizeof(uint32_t);
        }

        /* If the new skb is too big for the current NCM NTB:
        * set the current stored skb to be sent now,
        * and clear it ready for new data.
        */
        int cur_len = ncm->tx_data_len + div + rem \
                        + ncm->tx_ndp_len + ndp_align + (2 * dgram_idx_len);

        if (ncm->tx_in_used &&
            (ncm->ndp_dgram_count >= TX_MAX_NUM_DPE || cur_len + tot_len > max_size)) {
            ret = package_for_tx(ncm, req);
            if (ret < 0)
                goto err;
        }

        if (!ncm->tx_in_used) {
            ncm->tx_in_used = true;

            ncb_len = opts->nth_size;
            dgram_pad = ALIGN(ncb_len, div) + rem - ncb_len;
            ncb_len += dgram_pad;

            memset(ncm->tx_data, 0, ncm->tx_data_size);
            ntb_data = ncm->tx_data;
            ncm->tx_data_len = ncb_len;

            /* dwSignature */
            put_unaligned_le32(opts->nth_sign, ntb_data);
            ntb_data += 2;
            /* wHeaderLength */
            put_unaligned_le16(opts->nth_size, ntb_data++);

            memset(ncm->tx_ndp, 0, ncm->tx_ndp_size);
            ntb_ndp = ncm->tx_ndp;
            ncm->tx_ndp_len = opts->ndp_size;

            /* dwSignature */
            put_unaligned_le32(ncm->ndp_sign, ntb_ndp);
            ntb_ndp += 2;
            /* There is always a zeroed entry */
            ncm->ndp_dgram_count = 1;

            /* Start the timer. */
            hrtimer_start(&ncm->task_timer, TX_TIMEOUT_USECS);
        }

        ntb_ndp = ncm->tx_ndp + ncm->tx_ndp_len;
        ncm->tx_ndp_len += dgram_idx_len;

        ncb_len = ncm->tx_data_len;
        dgram_pad = ALIGN(ncb_len, div) + rem - ncb_len;
        ncb_len += dgram_pad;

        /* (d)wDatagramIndex */
        put_ncm(&ntb_ndp, opts->dgram_item_len, ncb_len);
        /* (d)wDatagramLength */
        put_ncm(&ntb_ndp, opts->dgram_item_len, tot_len);
        ncm->ndp_dgram_count++;

        ncm->tx_data_len += dgram_pad;
        ntb_data = ncm->tx_data + ncm->tx_data_len;

        while (p) {
            memcpy(ntb_data, p->payload, p->len);
            ncm->tx_data_len += p->len;
            ntb_data = ncm->tx_data + ncm->tx_data_len;
            p = p->next;
        }

        if (ncm->is_crc) {
            put_unaligned_le32(crc, ncm->tx_data + ncm->tx_data_len);
            ncm->tx_data_len += sizeof(uint32_t);
        }
    } else {
        /* hrtimer time out */
        ret = package_for_tx(ncm, req);
        if (ret < 0)
            goto err;
    }

    return ret;
err:
    dev->stats.tx_dropped++;

    if (ncm->tx_data)
        free(ncm->tx_data);
    if (ncm->tx_ndp)
        free(ncm->tx_ndp);

    return ret;
}

int ncm_unwrap_ntb(struct usb_request *req)
{
    struct f_ncm *ncm = (struct f_ncm *) req->context;
    const struct ndp_parser_opts *opts = ncm->parser_opts;

    u16         *tmp = (u16 *)req->buf;
    unsigned    index, index2;
    int         ndp_index;
    unsigned    dg_len, dg_len2;
    unsigned    ndp_len;
    unsigned    block_len;
    unsigned    ntb_max = ntb_parameters.dwNtbOutMaxSize;
    unsigned    frame_max = ecm_desc.wMaxSegmentSize;
    unsigned    crc_len = ncm->is_crc ? sizeof(uint32_t) : 0;
    int         dgram_counter;

    /* dwSignature */
    if (get_unaligned_le32(tmp) != opts->nth_sign)
        goto err;
    tmp += 2;

    /* wHeaderLength */
    if (get_unaligned_le16(tmp++) != opts->nth_size)
        goto err;
    tmp++; /* skip wSequence */

    block_len = get_ncm(&tmp, opts->block_length);
    /* (d)wBlockLength */
    if (block_len > ntb_max)
        goto err;

    ndp_index = get_ncm(&tmp, opts->ndp_index);

    /* Run through all the NDP's in the NTB */
    do {
        /*
         * NCM 3.2
         * dwNdpIndex
         */
        if (((ndp_index % 4) != 0) ||
            (ndp_index < opts->nth_size) ||
            (ndp_index > (block_len - opts->ndp_size)))
            goto err;

        /*
         * walk through NDP
         * dwSignature
         */
        tmp = (void *)(req->buf + ndp_index);
        if (get_unaligned_le32(tmp) != ncm->ndp_sign)
            goto err;
        tmp += 2;

        ndp_len = get_unaligned_le16(tmp++);

        /*
         * NCM 3.3.1
         * wLength
         * entry is 2 items
         * item size is 16/32 bits, opts->dgram_item_len * 2 bytes
         * minimal: struct usb_cdc_ncm_ndpX + normal entry + zero entry
         * Each entry is a dgram index and a dgram length.
         */
        if ((ndp_len < opts->ndp_size + 2 * 2 * (opts->dgram_item_len * 2)) ||
            (ndp_len % opts->ndplen_align != 0))
            goto err;

        tmp += opts->reserved1;
        /* Check for another NDP (d)wNextNdpIndex */
        ndp_index = get_ncm(&tmp, opts->next_ndp_index);
        tmp += opts->reserved2;

        ndp_len -= opts->ndp_size;
        index2 = get_ncm(&tmp, opts->dgram_item_len);
        dg_len2 = get_ncm(&tmp, opts->dgram_item_len);
        dgram_counter = 0;

        do {
            index = index2;
            /* wDatagramIndex[0] */
            if ((index < opts->nth_size) || (index > block_len - opts->dpe_size))
                goto err;

            dg_len = dg_len2;
            /*
             * wDatagramLength[0]
             * ethernet hdr + crc or larger than max frame size
             */
            if ((dg_len < 14 + crc_len) || (dg_len > frame_max))
                goto err;

            if (ncm->is_crc) {
                uint32_t crc, crc2;

                crc = get_unaligned_le32(req->buf + index + dg_len - crc_len);
                crc2 = ~crc32_le(~0, (const unsigned char *)(req->buf + index), dg_len - crc_len);
                if (crc != crc2)
                    goto err;
            }

            index2 = get_ncm(&tmp, opts->dgram_item_len);
            dg_len2 = get_ncm(&tmp, opts->dgram_item_len);

            /* wDatagramIndex[1] */
            if (index2 > block_len - opts->dpe_size)
                goto err;

            /*
             * Copy the data into a new skb.
             * This ensures the truesize is correct
             */
            struct eth_rx_frame *rx_frame = ncm_rx_frame_alloc(dg_len - crc_len);
            if (!rx_frame)
                goto err;

            memcpy(rx_frame->buf, req->buf + index, dg_len - crc_len);
            list_add_tail(&rx_frame->list, &ncm->dev.rx_frames);

            ndp_len -= 2 * (opts->dgram_item_len * 2);

            dgram_counter++;
            if (index2 == 0 || dg_len2 == 0)
                break;
        } while (ndp_len > 2 * (opts->dgram_item_len * 2));
    } while (ndp_index);

    return 0;
err:
    return -1;
}

/*-------------------------------------------------------------------------*/

/* ethernet function driver setup/binding */

static void ncm_tx_timeout(struct hrtimer *data)
{
    struct f_ncm *ncm = container_of(data, struct f_ncm, task_timer);
    struct eth_dev *dev = &ncm->dev;
    if (!dev)
        return;

    struct netif *netif = &dev->netif;
    if (netif && netif->linkoutput) {
        /* This will call directly into u_ether's eth_start_xmit() */
        dev->netif.linkoutput(&dev->netif, NULL);
    }
}

static int ncm_bind(struct usb_configuration *c, struct usb_function *f)
{
    struct usb_composite_dev *cdev = c->cdev;
    struct f_ncm       *ncm = func_to_ncm(f);
    struct eth_dev     *eth_dev = &ncm->dev;
    struct usb_string  *us;
    struct usb_ep      *ep;
    struct usb_request *req;
    int    status, i;

    ip4_addr_t ipaddr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

    us = usb_gstrings_attach(cdev, ncm_strings, ARRAY_SIZE(ncm_string_defs));
    if (IS_ERR(us))
        return PTR_ERR(us);

    ncm_control_intf.iInterface = us[STRING_CTRL_IDX].id;
    ncm_data_nop_intf.iInterface = us[STRING_DATA_IDX].id;
    ncm_data_intf.iInterface = us[STRING_DATA_IDX].id;
    ecm_desc.iMACAddress = us[STRING_MAC_IDX].id;
    ncm_iad_desc.iFunction = us[STRING_IAD_IDX].id;

    /* allocate instance-specific interface IDs */
    status = usb_interface_id(c, f);
    if (status < 0)
        return status;

    ncm->ctrl_id = status;
    ncm_iad_desc.bFirstInterface = status;

    ncm_control_intf.bInterfaceNumber = status;
    ncm_union_desc.bMasterInterface0 = status;

    status = usb_interface_id(c, f);
    if (status < 0)
        return status;
    ncm->data_id = status;

    ncm_data_nop_intf.bInterfaceNumber = status;
    ncm_data_intf.bInterfaceNumber = status;
    ncm_union_desc.bSlaveInterface0 = status;

    status = -ENODEV;

    /* allocate instance-specific endpoints */
    ep = usb_ep_autoconfig(cdev->gadget, &ncm_fs_in_desc);
    if (!ep)
        return status;
    ncm->dev.in_ep = ep;

    ep = usb_ep_autoconfig(cdev->gadget, &ncm_fs_out_desc);
    if (!ep)
        return status;
    ncm->dev.out_ep = ep;

    ep = usb_ep_autoconfig(cdev->gadget, &ncm_fs_notify_desc);
    if (!ep)
        return status;
    ncm->notify = ep;

    ncm_hs_in_desc.bEndpointAddress = ncm_fs_in_desc.bEndpointAddress;
    ncm_hs_out_desc.bEndpointAddress = ncm_fs_out_desc.bEndpointAddress;
    ncm_hs_notify_desc.bEndpointAddress = ncm_fs_notify_desc.bEndpointAddress;

    status = usb_assign_descriptors(f, ncm_fs_function, ncm_hs_function);
    if (status)
        goto fail_free_descs;

    status = -ENOMEM;

    /* allocate tx buffer */
    ncm->tx_in_used = false;
    status = ncm_tx_buf_alloc(ncm);
    if (status)
        goto fail_tx_bufs;

    /* allocate tx/rx request */
    for (i = 0; i < DEFAULT_QLEN; i++) {
        req = ncm_req_alloc(ncm->dev.in_ep, eth_dev->fixed_in_len);
        if (!req)
            goto fail_tx_reqs;
        req->context = ncm;
        list_add(&req->list, &eth_dev->tx_reqs);
    }

    for (i = 0; i < DEFAULT_QLEN; i++) {
        req = ncm_req_alloc(ncm->dev.out_ep, eth_dev->fixed_out_len);
        if (!req)
            goto fail_rx_reqs;
        req->context = ncm;
        list_add(&req->list, &eth_dev->rx_reqs);
    }

    /* allocate notification request and buffer */
    ncm->notify_req = usb_ep_alloc_request(ep);
    if (!ncm->notify_req)
        goto fail_notify_req;
    ncm->notify_req->buf = malloc(NCM_STATUS_BYTECOUNT);
    if (!ncm->notify_req->buf)
        goto fail_notify_req;
    memset(ncm->notify_req->buf, 0, NCM_STATUS_BYTECOUNT);
    ncm->notify_req->length = NCM_STATUS_BYTECOUNT;
    ncm->notify_req->context = ncm;
    ncm->notify_req->complete = ncm_notify_complete;

    ipaddr.addr = inet_addr(eth_dev->param->ipaddr);
    netmask.addr = inet_addr(eth_dev->param->netmask);
    gw.addr = inet_addr(eth_dev->param->gw);

    status = netifapi_netif_add(&eth_dev->netif, &ipaddr, &netmask, &gw, eth_dev, ncm_netif_device_init, tcpip_input);
    if (status) {
        printf("%s: netifapi_netif_add fail\n", __func__);
        status = -ENOMEM;
        goto fail_notify_req;
    }

    ncm_active_netif = &eth_dev->netif;

    hrtimer_init(&ncm->task_timer, ncm_tx_timeout);
    eth_dev->rx_running = true;
    thread_waiter_init(&eth_dev->rx_waiter);
    eth_dev->rx_thread = thread_create("rx_thread", 4096, ueth_rx_thread, eth_dev);
    assert(eth_dev->rx_thread);

    return 0;

fail_notify_req:
    if (ncm->notify_req) {
        free(ncm->notify_req->buf);
        usb_ep_free_request(ncm->notify, ncm->notify_req);
    }

fail_rx_reqs:
    while (!list_empty(&eth_dev->rx_reqs)) {
        req = list_first_entry(&eth_dev->rx_reqs, struct usb_request, list);
        list_del(&req->list);
        ncm_req_free(eth_dev->out_ep, req);
    }

fail_tx_reqs:
    while (!list_empty(&eth_dev->tx_reqs)) {
        req = list_first_entry(&eth_dev->tx_reqs, struct usb_request, list);
        list_del(&req->list);
        ncm_req_free(eth_dev->in_ep, req);
    }

fail_tx_bufs:
    ncm_tx_buf_free(ncm);

fail_free_descs:
    usb_free_all_descriptors(f);

    printf("%s: can't bind, err %d\n", f->name, status);

    return status;
}

static void ncm_unbind(struct usb_configuration *c, struct usb_function *f)
{
    struct f_ncm *ncm = func_to_ncm(f);
    struct eth_dev *eth_dev = &ncm->dev;
    struct usb_request *req;
    struct eth_rx_frame *rx_frame;

    hrtimer_cancel(&ncm->task_timer);

    eth_dev->rx_running = false;
    thread_waiter_wakeup(&eth_dev->rx_waiter);
    thread_join(eth_dev->rx_thread, NULL);

    ncm_active_netif = NULL;
    netifapi_netif_remove(&eth_dev->netif);

    free(ncm->notify_req->buf);
    usb_ep_free_request(ncm->notify, ncm->notify_req);

    while (!list_empty(&eth_dev->rx_complete_reqs)) {
        req = list_first_entry(&eth_dev->rx_complete_reqs, struct usb_request, list);
        list_del(&req->list);
        ncm_req_free(eth_dev->out_ep, req);
    }

    while (!list_empty(&eth_dev->rx_reqs)) {
        req = list_first_entry(&eth_dev->rx_reqs, struct usb_request, list);
        list_del(&req->list);
        ncm_req_free(eth_dev->out_ep, req);
    }

    while (!list_empty(&eth_dev->tx_reqs)) {
        req = list_first_entry(&eth_dev->tx_reqs, struct usb_request, list);
        list_del(&req->list);
        ncm_req_free(eth_dev->in_ep, req);
    }

    while (!list_empty(&eth_dev->rx_frames)) {
        rx_frame = list_first_entry(&eth_dev->rx_frames, struct eth_rx_frame, list);
        list_del(&rx_frame->list);
        ncm_rx_frame_free(rx_frame);
    }

    ncm_tx_buf_free(ncm);

    usb_free_all_descriptors(f);
}

struct usb_function *ncm_device_alloc(struct usb_ncm_param *param)
{
    struct f_ncm        *ncm;

    assert(param);
    ncm = malloc(sizeof(*ncm));
    if (!ncm){
        printf("%s: out of memory\n", __func__);
        return ERR_PTR(-ENOMEM);
    }
    memset(ncm, 0, sizeof(*ncm));

    ncm_active_netif = NULL;

    ncm->dev.param = param;

    /* export host's Ethernet address in CDC format */
    snprintf(ncm->ethaddr, sizeof(ncm->ethaddr), "%02x%02x%02x%02x%02x%02x",
        param->host_mac[0], param->host_mac[1], param->host_mac[2],
        param->host_mac[3], param->host_mac[4], param->host_mac[5]);
    ncm_string_defs[STRING_MAC_IDX].s = ncm->ethaddr;

    ncm_reset_values(ncm);

    spin_lock_init(&ncm->lock);
    spin_lock_init(&ncm->dev.req_lock);
    spin_lock_init(&ncm->dev.xmit_lock);
    INIT_LIST_HEAD(&ncm->dev.tx_reqs);
    INIT_LIST_HEAD(&ncm->dev.rx_reqs);
    INIT_LIST_HEAD(&ncm->dev.rx_complete_reqs);
    INIT_LIST_HEAD(&ncm->dev.rx_frames);

    /* NCM has special (and complex) framing */
    ncm->dev.wrap = ncm_wrap_ntb;
    ncm->dev.unwrap = ncm_unwrap_ntb;

    ncm->dev.func.name = "cdc_network";
    ncm->dev.func.bind = ncm_bind;
    ncm->dev.func.unbind = ncm_unbind;
    ncm->dev.func.set_alt = ncm_set_alt;
    ncm->dev.func.get_alt = ncm_get_alt;
    ncm->dev.func.req_match = ncm_req_match;
    ncm->dev.func.setup = ncm_setup;
    ncm->dev.func.disable = ncm_disable;

    return &ncm->dev.func;
}

void ncm_device_free(struct usb_function *f)
{
    struct f_ncm *ncm = func_to_ncm(f);
    free(ncm);
}

struct netif *ncm_get_active_netif(void)
{
    return ncm_active_netif;
}
