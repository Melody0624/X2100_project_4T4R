#ifndef _GADGET_DISPLAY_H_
#define _GADGET_DISPLAY_H_

#include "gadget_common.h"
#include "uac.h"

#ifdef CONFIG_USB_GADGET_DISPLAY_AUDIO_HID
#define DISPLAY_STATUS_ERROR_DATA_FORMAT   0x01
#define DISPLAY_STATUS_ERROR_DECODER       0x02
#define DISPLAY_STATUS_ERROR_MEMORY        0x04
#define DISPLAY_STATUS_INIT_COMPLETE       0x08
#define DISPLAY_STATUS_READY               0x10

#define MINI_CMD_SIZE 2

#define DISPLAY_CMD_COLOR_FORMAT    0x00
#define DISPLAY_CMD_WIDTH_HIGH      0x01
#define DISPLAY_CMD_WIDTH_LOW       0x02
#define DISPLAY_CMD_HEIGHT_HIGH     0x03
#define DISPLAY_CMD_HEIGHT_LOW      0x04
#define DISPLAY_CMD_BACKLIGHT       0x05
#define DISPLAY_CMD_AUTORLX         0x06
#define DISPLAY_CMD_MJPEG           0x11
#define DISPLAY_CMD_H264            0x12
#define DISPLAY_CMD_BLANK_MODE        0x1f

#define DISPLAY_COLOR_FORMAT_RGB565        (1 << 0)
#define DISPLAY_COLOR_FORMAT_XRGB888    (1 << 1)

#define DISPLAY_BLANK_MODE_ON        0x01 /* powered on */
#define DISPLAY_BLANK_MODE_POWERDOWN    0x02 /* powered off */

struct display_edid {
    const u8 *buf;
    u8 size;
};

struct display_specific_des {
    const u8 *buf;
    u8 size;
};

struct display_params {
    struct display_edid edid;
    struct display_specific_des des;
    connect_callback_t connect_cb;
};

struct display_buffer {
    const u8 *buf;
    u32 length; /* buf size */
    u32 actual; /* buf actual size */
    void *private_data;
};

/*
 Get Data Packet 
    param:
        <display_buffer> Get data packet pointer
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
*/
extern int gadget_display_get_buffer(struct display_buffer *display_buffer, uint32_t timeout_ms);

/*
 Put Data Packet 
    param:
        <display_buffer> Put data packet pointer
    return:
        <normal> 0
        <abnormal> Error code 
*/
extern int gadget_display_put_buffer(struct display_buffer *display_buffer);

/*
   Get Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_display_get_connect_status(void);

/*
   Waiting for usb link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_display_wait_connect(uint32_t timeout_ms);

/*
   Get Display Status 
    param:
        <status> Get status
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_display_get_status(u8 *status);

/*
   Set Display Status 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_display_set_status(u8 status);

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


struct hid_report_descriptor {
    uint8_t     subclass;
    uint8_t     protocol;
    unsigned short      report_length;
    unsigned short      report_desc_length;
    const uint8_t     *report_desc;
};

typedef int (*hid_request_callback_t)(void *data, u16 *length, int request);

struct hid_params {
    struct hid_report_descriptor des;
    connect_callback_t connect_cb;
    hid_request_callback_t request_cb;
};

/*
 hid read data
    param:
        <buffer>  Buffer for reading data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Read Bytes 
        <abnormal> Error code 
 */
extern int gadget_hid_read(uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms);

/*
 hid write data
    param:
        <buffer>  Buffer for writing data 
        <count>  Buffer size 
        <block> Is it blocked 
        <timeout_ms> Block timeout
    return:
        <normal> Write Bytes 
        <abnormal> Error code 
 */
extern int gadget_hid_write(const uint8_t *buffer, uint32_t count, uint8_t block, uint32_t timeout_ms);

/*
   Get HID Connection Status 
    return:
        <normal> Connection Status
        <abnormal> Error code 
 */
extern int gadget_hid_get_connect_status(void);

/*
   Waiting for hid link 
    param:
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_hid_wait_connect(uint32_t timeout_ms);

/*
 display device init 
    param:
        <id>  Manufacturer's device ID 
        <display_params>
        <uac1_params>
        <hid_params>
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_display_audio_hid_init(const struct gadget_id *id, struct display_params *display_params,
    struct uac1_params *uac1_params, struct hid_params *hid_params);
 
/* display device cleanup */
extern void gadget_display_audio_hid_cleanup(void);

/*
   Get Backlight Value
    param:
        <status> Get Value
    return:
        <normal> 0
        <abnormal> Error code
 */
extern int gadget_display_get_backlight(u8 *value);

/*
   Set Backlight Value
    return:
        <normal> 0
        <abnormal> Error code
 */
extern int gadget_display_set_backlight(u8 value);

#endif

#endif /* _GADGET_DISPLAY_H_ */
