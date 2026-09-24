#ifndef _GADGET_UAC1_H_
#define _GADGET_UAC1_H_

#include "gadget_common.h"
#include "uac.h"

#ifdef CONFIG_USB_GADGET_UAC1

typedef int (*uac1_feature_callback_t)(u8 type, u8 request, void *buf, u16 len);
typedef int (*uac1_start_callback_t)(u32 rate);
typedef void (*uac1_stop_callback_t)(void);

struct uac1_params {
    /* playback */
    u32 p_chmask;    /* channel mask */
    u32 p_ssize;    /* sample size */
    u32 p_srate_num; /* Supports sample rate number */
    u32 *p_srates;    /* rate in Hz [array] */
    u32 p_srate;    /* default rate*/
    u16 p_feature;  /* Supports feature */
    uac1_feature_callback_t p_feature_callback;
    uac1_start_callback_t start_playback_callback;
    uac1_stop_callback_t stop_playback_callback;

    /* capture */
    u32 c_chmask;    /* channel mask */
    u32 c_ssize;    /* sample size */
    u32 c_srate_num; /* Supports sample rates number */
    u32 *c_srates;    /* rate in Hz [array] */
    u32 c_srate;    /* default rate*/
    u16 c_feature;  /* Supports feature */
    uac1_feature_callback_t c_feature_callback;
    uac1_start_callback_t start_capture_callback;
    uac1_stop_callback_t stop_capture_callback;

    u32 buffer_size_ms;     /* pcm buf size [unit:ms] */
    connect_callback_t connect_cb;
};

/*
 uac1 init 
    param:
        <id>  Manufacturer's device ID 
        <params> USB uac1 param
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uac1_init(const struct gadget_id *id, const struct uac1_params *params);

/* uac1 device cleanup */
extern void gadget_uac1_cleanup(void);

/*
   Get Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_uac1_get_connect_status(void);

/*
   Waiting for usb link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uac1_wait_connect(u32 timeout_ms);

/*
   Waiting for playback start
    param:
        <timeout_ms> Block timeout
        <rate> Sample rate
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uac1_wait_playback_start(u32 timeout_ms, u32 *rate);

/*
   Waiting for capture start 
    param:
        <timeout_ms> Block timeout
        <rate> Sample rate
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uac1_wait_capture_start(u32 timeout_ms, u32 *rate);

/*
 uac1 playback read data
    param:
        <buffer>  Buffer for reading data 
        <len>  Buffer size 
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_uac1_playback_read(u8 *buffer, u32 len);

/*
 uac1 capture write data
    param:
        <buffer>  Buffer for writing data 
        <len>  Buffer size 
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_uac1_capture_write(u8 *buffer, u32 len);

#endif

#endif /* _GADGET_UAC1_H_ */
