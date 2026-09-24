#ifndef _GADGET_UVC_UAC1_H_
#define _GADGET_UVC_UAC1_H_

#include <common.h>
#include "gadget_common.h"
#include "uvc.h"
#include "uac.h"

#ifdef CONFIG_USB_GADGET_UVC_UAC1

#define UVC_ENTITY_INTERFACE_ID                0x00
#define UVC_ENTITY_CAMERA_TERMINAL_ID        0x01
#define UVC_ENTITY_PROCESS_UNIT_ID            0x02
#define UVC_ENTITY_OUTPUT_TERMINAL_ID        0x03
struct uvc_frame_config
{
    unsigned int width;
    unsigned int height;
    unsigned int fps_num;
    const unsigned int *frame_fps;
};

struct uvc_format_config
{
    unsigned int fcc;
    unsigned int bpp;
    unsigned int frames_num;
    const struct uvc_frame_config *frames;
};

struct uvc_device_config
{
    unsigned int format_num;
    unsigned int camera_feature_config;
    unsigned int camera_param_config;
    const struct uvc_format_config *formats;
};

struct uvc_control_request
{
    void* buf;
    unsigned int buf_actual;
    unsigned char request;
    unsigned int request_len;
    unsigned char entity_id;
    unsigned char control_selector;
    unsigned char event_out;
};

typedef void (*format_callback_t)(const struct uvc_video_format *format);
typedef int (*stream_callback_t)(int enable);
typedef int (*fparam_callback_t)(const struct uvc_control_request *req);

struct uvc_callback
{
    format_callback_t format_cb;
    stream_callback_t stream_cb;
    connect_callback_t connect_cb;
    fparam_callback_t param_cb;
};


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
 uvc uac1 init 
    param:
        <id>  Manufacturer's device ID 
        <config> uvc device config pointer
        <config_count> uvc device config count
        <uac1_params> USB uac1 param
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern void gadget_uvc_uac1_init(const struct gadget_id *id, const struct uvc_device_config *uvc_config,
        const struct uvc_callback* uvc_callback, const struct uac1_params *uac1_params);

/* uvc uac1 device cleanup */
extern void gadget_uvc_uac1_cleanup(void);

/*
 UVC write data
    param:
        <buf>  Buffer for writing data 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uvc_write(struct uvc_buffer *buf, uint8_t block, uint32_t timeout_ms);

/*
   Get UVC Connection Status 
    param:
        <uvc_id> uvc index
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_uvc_get_connect_status(void);

/*
   Waiting for UVC link 
    param:
        <uvc_id> uvc index
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uvc_wait_connect(uint32_t timeout_ms);

/*
   Waiting for UVC stream on 
    param:
        <uvc_id> uvc index
        <timeout_ms> Block timeout
        <format> UVC video format
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uvc_wait_stream(uint32_t timeout_ms, struct uvc_video_format *format);

/*
   Get UAC1 Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_uac1_get_connect_status(void);

/*
   Waiting for UAC1 link 
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

#endif /* _GADGET_UVC_UAC1_H_ */
