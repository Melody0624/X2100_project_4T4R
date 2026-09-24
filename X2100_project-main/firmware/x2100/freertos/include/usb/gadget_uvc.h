#ifndef _GADGET_UVC_H_
#define _GADGET_UVC_H_

#include <common.h>
#include "gadget_common.h"
#include "uvc.h"

#ifdef CONFIG_USB_GADGET_UVC

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

/*
 uvc init 
    param:
        <id>  Manufacturer's device ID 
        <config> uvc device config
        <callback> uvc function callback
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern void gadget_uvc_init(const struct gadget_id *id, const struct uvc_device_config *config,
                    const struct uvc_callback* callback);

/* uvc device cleanup */
extern void gadget_uvc_cleanup(void);

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
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_uvc_get_connect_status(void);

/*
   Waiting for UVC link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uvc_wait_connect(uint32_t timeout_ms);

/*
   Waiting for UVC stream on 
    param:
        <timeout_ms> Block timeout
        <format> UVC video format
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_uvc_wait_stream(uint32_t timeout_ms, struct uvc_video_format *format);
#endif

#endif /* _GADGET_UVC_H_ */
