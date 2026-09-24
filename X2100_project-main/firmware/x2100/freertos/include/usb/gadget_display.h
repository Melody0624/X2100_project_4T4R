#ifndef _GADGET_DISPLAY_H_
#define _GADGET_DISPLAY_H_

#include "gadget_common.h"

#ifdef CONFIG_USB_GADGET_DISPLAY

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

struct display_buffer {
    const u8 *buf;
    u32 length; /* buf size */
    u32 actual; /* buf actual size */
    void *private_data;
};

/*
 display device init 
    param:
        <id>  Manufacturer's device ID 
        <edid> Extended display identification data
        <des>  Manufacturer specific descriptor 
        <connect_cb>  USB connection and disconnection callback functions 
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int gadget_display_init(const struct gadget_id *id, struct display_edid *edid, struct display_specific_des *des, connect_callback_t connect_cb);

/* display device cleanup */
extern void gadget_display_cleanup(void);

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
