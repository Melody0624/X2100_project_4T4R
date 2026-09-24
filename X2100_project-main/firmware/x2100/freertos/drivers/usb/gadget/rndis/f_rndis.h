/*
 * RNDIS    Definitions for Remote NDIS
 *
 * Authors:    Benedikt Spranger, Pengutronix
 *        Robert Schwebel, Pengutronix
 *
 *        This program is free software; you can redistribute it and/or
 *        modify it under the terms of the GNU General Public License
 *        version 2, as published by the Free Software Foundation.
 *
 *        This software was originally developed in conformance with
 *        Microsoft's Remote NDIS Specification License Agreement.
 */

#ifndef _F_RNDIS_H_
#define _F_RNDIS_H_

#include <driver/cache.h>
#include "rndis.h"
#include "../composite.h"

#define RNDIS_MAXIMUM_FRAME_SIZE    1518
#define RNDIS_MAX_TOTAL_SIZE        1558

typedef struct rndis_init_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    MajorVersion;
    u32    MinorVersion;
    u32    MaxTransferSize;
} rndis_init_msg_type;

typedef struct rndis_init_cmplt_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    Status;
    u32    MajorVersion;
    u32    MinorVersion;
    u32    DeviceFlags;
    u32    Medium;
    u32    MaxPacketsPerTransfer;
    u32    MaxTransferSize;
    u32    PacketAlignmentFactor;
    u32    AFListOffset;
    u32    AFListSize;
} rndis_init_cmplt_type;

typedef struct rndis_halt_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
} rndis_halt_msg_type;

typedef struct rndis_query_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    OID;
    u32    InformationBufferLength;
    u32    InformationBufferOffset;
    u32    DeviceVcHandle;
} rndis_query_msg_type;

typedef struct rndis_query_cmplt_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    Status;
    u32    InformationBufferLength;
    u32    InformationBufferOffset;
} rndis_query_cmplt_type;

typedef struct rndis_set_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    OID;
    u32    InformationBufferLength;
    u32    InformationBufferOffset;
    u32    DeviceVcHandle;
} rndis_set_msg_type;

typedef struct rndis_set_cmplt_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    Status;
} rndis_set_cmplt_type;

typedef struct rndis_reset_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    Reserved;
} rndis_reset_msg_type;

typedef struct rndis_reset_cmplt_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    Status;
    u32    AddressingReset;
} rndis_reset_cmplt_type;

typedef struct rndis_indicate_status_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    Status;
    u32    StatusBufferLength;
    u32    StatusBufferOffset;
} rndis_indicate_status_msg_type;

typedef struct rndis_keepalive_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
} rndis_keepalive_msg_type;

typedef struct rndis_keepalive_cmplt_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    RequestID;
    u32    Status;
} rndis_keepalive_cmplt_type;

struct rndis_packet_msg_type
{
    u32    MessageType;
    u32    MessageLength;
    u32    DataOffset;
    u32    DataLength;
    u32    OOBDataOffset;
    u32    OOBDataLength;
    u32    NumOOBDataElements;
    u32    PerPacketInfoOffset;
    u32    PerPacketInfoLength;
    u32    VcHandle;
    u32    Reserved;
} __attribute__ ((packed));

struct rndis_config_parameter
{
    u32    ParameterNameOffset;
    u32    ParameterNameLength;
    u32    ParameterType;
    u32    ParameterValueOffset;
    u32    ParameterValueLength;
};

/* implementation specific */
enum rndis_state
{
    RNDIS_UNINITIALIZED,
    RNDIS_INITIALIZED,
    RNDIS_DATA_INITIALIZED,
};

typedef struct rndis_resp_t
{
    struct list_head    list;
    u8            *buf;
    u32            length;
    int            send;
} rndis_resp_t;

typedef struct rndis_params {
    enum rndis_state    state;
    u32            medium;
    u32            speed;
    u32            media_state;

    u16            *filter;
    struct eth_dev *priv_dev;

    u32            vendorID;
    const char        *vendorDescr;
    void            (*resp_avail)(void *v);
    void            *v;
    struct list_head    resp_queue;
    spinlock_t        resp_lock;
} rndis_params;


/* RNDIS Message parser and other useless functions */
int  rndis_msg_parser(struct rndis_params *params, u8 *buf);
struct rndis_params *rndis_register(void (*resp_avail)(void *v), void *v);
void rndis_deregister(struct rndis_params *params);
int  rndis_set_param_filter(struct rndis_params *params, u16 *cdc_filter);
int  rndis_set_param_vendor(struct rndis_params *params, u32 vendorID,
                const char *vendorDescr);
int  rndis_set_param_medium(struct rndis_params *params, u32 medium,
                 u32 speed);

u8   *rndis_get_next_response(struct rndis_params *params, u32 *length);
void rndis_free_response(struct rndis_params *params, u8 *buf);

void rndis_uninit(struct rndis_params *params);
int  rndis_signal_connect(struct rndis_params *params);
int  rndis_signal_disconnect(struct rndis_params *params);

#endif  /* _F_RNDIS_H_ */
