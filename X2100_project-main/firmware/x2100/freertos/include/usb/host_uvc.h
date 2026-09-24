#ifndef _HOST_UVC_H_
#define _HOST_UVC_H_

#include <common.h>
#include <usb/uvc.h>

#ifdef CONFIG_USB_HOST_UVC

#define UVC_MINORS        32

enum uvc_stream_type {
    UVC_STREAM_TYPE_VIDEO_CAPTURE        = 1,
    UVC_STREAM_TYPE_VIDEO_OUTPUT         = 2,
};

struct uvc_host_frame {
	u8  bFrameIndex;
	u8  bmCapabilities;
	u16 wWidth;
	u16 wHeight;
	u32 dwMinBitRate;
	u32 dwMaxBitRate;
	u32 dwMaxVideoFrameBufferSize;
	u8  bFrameIntervalType;
	u32 dwDefaultFrameInterval;
	const u32 *dwFrameInterval;
};
struct uvc_host_format {
	u8 type;
	u8 index;
	u8 bpp;
	u32 fcc;
	u32 flags;

	unsigned int nframes;
	const struct uvc_host_frame *frames;
};
struct uvc_host_video {
    void *stream;
    enum uvc_stream_type type;

    unsigned int nformats;
    const struct uvc_host_format *formats;
};

struct uvc_status_streaming {
    u8    button;
} __packed;

struct uvc_status_control {
    u8    bSelector;
    u8    bAttribute;
    u8    bValue[11];
} __packed;

struct uvc_status {
    u8    bStatusType;
    u8    bOriginator;
    u8    bEvent;
    union {
        struct uvc_status_control control;
        struct uvc_status_streaming streaming;
    };
} __packed;

/* usb host uvc supports up to 32 devices, each bit represents a device */
typedef void (*uvc_device_callback_t)(u32 devices_bit);

/* When uvc device is inserted or removed, callback will be triggered to update the state */
extern void usb_host_uvc_register_callback(uvc_device_callback_t callback);
extern u32 usb_host_uvc_get_devices_bit(void);

typedef void (*uvc_notify_callback_t)(u8 id, struct uvc_status *status, int len);

/*
 open uvc device
    param:
        <id> uvc device index
        <callback> uvc notify callback
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_open(u8 id, uvc_notify_callback_t callback);

/*
 close uvc device
    param:
        <id> uvc device index
    return:
        <normal> 0
        <abnormal> Error code 
    note:
        Successfully opened devices must be manually closed 
 */
extern int usb_host_uvc_close(u8 id);

/*
 get video number
    param:
        <id> uvc device index
    return:
        <normal> video number
        <abnormal> Error code 
 */
extern int usb_host_uvc_get_video_num(u8 id);

/*
 get video param
    param:
        <id> uvc device index
        <video_id> uvc video index
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_get_video(u8 id, u8 video_id, struct uvc_host_video *video);

/*
 set video format
    param:
        <id> uvc device index
        <video> video param pointer
        <format_index> format index
        <frame_index> frame index
        <interval> interval
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_set_format(u8 id, const struct uvc_host_video *video, u8 format_index, u8 frame_index, u32 interval);

/*
 get video format
    param:
        <id> uvc device index
        <video> video param pointer
        <video_format> video format param
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_get_format(u8 id, const struct uvc_host_video *video, struct uvc_video_format *video_format);

/*
 request uvc buffer
    param:
        <id> uvc device index
        <video> video param pointer
        <frame_nums> frame nums
        <frame_size> frame size
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_request_buffer(u8 id, const struct uvc_host_video *video, u32 frame_nums, u32 frame_size);

/*
 free uvc buffer
    param:
        <id> uvc device index
        <video> video param pointer
    return:
        <normal> 0
        <abnormal> Error code 
    note:
        Successfully requested must be manually free 
 */
extern int usb_host_uvc_free_buffer(u8 id, const struct uvc_host_video *video);

/*
 uvc stream on
    param:
        <id> uvc device index
        <video> video param pointer
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_stream_on(u8 id, const struct uvc_host_video *video);

/*
 uvc stream off
    param:
        <id> uvc device index
        <video> video param pointer
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_stream_off(u8 id, const struct uvc_host_video *video);

/*
 get uvc buffer
    param:
        <id> uvc device index
        <video> video param pointer
        <buf_p> get uvc buffer pointer
        <timeout_ms> Block timeout
    return:
        <normal> 0
        <abnormal> Error code 
 */
extern int usb_host_uvc_get_buffer(u8 id, const struct uvc_host_video *video, struct uvc_buffer **buf_p, u32 timeout_ms);

/*
 put uvc buffer
    param:
        <id> uvc device index
        <video> video param pointer
        <buf> put uvc buffer pointer
    return:
        <normal> 0
        <abnormal> Error code 
    note:
        Successfully get buffer must be manually put buffer 
 */
extern int usb_host_uvc_put_buffer(u8 id, const struct uvc_host_video *video, struct uvc_buffer *buf);

#endif

#endif /* _HOST_UVC_H_ */
