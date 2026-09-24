#ifndef _GADGET_UVC_SERIAL_H_
#define _GADGET_UVC_SERIAL_H_

#include <common.h>
#include "gadget_common.h"
#include "uvc.h"

#ifdef CONFIG_USB_GADGET_UVC_CDC

#define UVC_ENTITY_INTERFACE_ID                0x00
#define UVC_ENTITY_CAMERA_TERMINAL_ID        0x01
#define UVC_ENTITY_PROCESS_UNIT_ID            0x02
#define UVC_ENTITY_OUTPUT_TERMINAL_ID        0x03

#define USB_CDC_1_STOP_BITS            0
#define USB_CDC_1_5_STOP_BITS            1
#define USB_CDC_2_STOP_BITS            2

#define USB_CDC_NO_PARITY            0
#define USB_CDC_ODD_PARITY            1
#define USB_CDC_EVEN_PARITY            2
#define USB_CDC_MARK_PARITY            3
#define USB_CDC_SPACE_PARITY            4

struct usb_cdc_serial_param {
    u32    dwDTERate;       /* Baud rate */
    u8    bCharFormat;      /* Stop bit */
    u8    bParityType;      /* Check mode */
    u8    bDataBits;        /* Data bits */
};

/* serial param change callback */
typedef void (*serial_param_callback_t)(struct usb_cdc_serial_param *p);

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

struct uvc_cdc_callback {
    struct uvc_callback *uvc_cb;
    connect_callback_t serial_connect_cb;
    serial_param_callback_t serial_param_cb;
};

/*
 uvc serial device init 
    param:
        <id>  Manufacturer's device ID 
        <uvc_config> UVC device config
        <cdc_param> USB CDC serial param
        <callback>  uvc cdc callback
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern void gadget_uvc_serial_init(const struct gadget_id *id, const struct uvc_device_config *uvc_config,
        const struct usb_cdc_serial_param *cdc_param, const struct uvc_cdc_callback* callback);

/* uvc serial device cleanup */
extern void gadget_uvc_serial_cleanup(void);

/*
 uvc write data
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

/* sent serial break event */
extern int gadget_serial_break_ctl(int duration);

/*
  Waiting for serial data to be sent from the write buffer 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_flush_chars(uint32_t timeout_ms);

/*
  Clear serial write buffer data
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_clean_write(void);

/*
 serial read data
    param:
        <buf>  Buffer for reading data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_serial_read(uint8_t *buf, uint32_t count, uint8_t block, uint32_t timeout_ms);

/*
 serial write data
    param:
        <buf>  Buffer for writing data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_serial_write(const uint8_t *buf, uint32_t count, uint32_t block, uint32_t timeout_ms);

/*
   Get Serial Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_serial_get_connect_status(void);

/*
   Waiting for serial link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_wait_connect(uint32_t timeout_ms);

/*
   Get Serial Param 
    param:
        <cdc_param> serial param pointer
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_serial_get_param(struct usb_cdc_serial_param *cdc_param);

#endif

#endif /* _GADGET_UVC_SERIAL_H_ */
