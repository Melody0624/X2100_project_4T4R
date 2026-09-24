#ifndef _UVC_GADGET_H_
#define _UVC_GADGET_H_

#include "../composite.h"
#include <usb/gadget_uvc_n_uac1.h>
#include "uvc_video.h"

#include "../../usb_lock.h"

struct usb_ep;
struct usb_request;
struct uvc_descriptor_header;
struct uvc_device;

/* ------------------------------------------------------------------------
 * Debugging, printing and logging
 */

#define UVC_TRACE_PROBE				(1 << 0)
#define UVC_TRACE_DESCR				(1 << 1)
#define UVC_TRACE_CONTROL			(1 << 2)
#define UVC_TRACE_FORMAT			(1 << 3)
#define UVC_TRACE_CAPTURE			(1 << 4)
#define UVC_TRACE_CALLS				(1 << 5)
#define UVC_TRACE_IOCTL				(1 << 6)
#define UVC_TRACE_FRAME				(1 << 7)
#define UVC_TRACE_SUSPEND			(1 << 8)
#define UVC_TRACE_STATUS			(1 << 9)

#define UVC_WARN_MINMAX				0
#define UVC_WARN_PROBE_DEF			1

/* ------------------------------------------------------------------------
 * Driver specific constants
 */

#define UVC_NUM_REQUESTS			4
#define UVC_MAX_REQUEST_SIZE			64

/* ------------------------------------------------------------------------
 * Structures
 */

/*
 * struct uvc_function_config_frame - Streaming frame parameters
 * @index: Frame index in the UVC descriptors
 * @width: Frame width in pixels
 * @height: Frame height in pixels
 * @num_intervals: Number of entries in the intervals array
 * @intervals: Array of frame intervals
 */
struct uvc_function_config_frame {
	unsigned int index;
	unsigned int width;
	unsigned int height;
	unsigned int num_intervals;
	unsigned int *intervals;
};

/*
 * struct uvc_function_config_format - Streaming format parameters
 * @index: Format index in the UVC descriptors
 * @fcc: V4L2 pixel format
 * @num_frames: Number of entries in the frames array
 * @frames: Array of frame descriptors
 */
struct uvc_function_config_format {
	unsigned int index;
	unsigned int fcc;
	unsigned int bpp;
	unsigned int num_frames;
	struct uvc_function_config_frame *frames;
};

/*
 * struct uvc_function_config_streaming - Streaming interface parameters
 * @intf: Generic interface parameters
 * @ep: Endpoint parameters
 * @num_formats: Number of entries in the formats array
 * @formats: Array of format descriptors
 */
struct uvc_function_config_streaming {
	unsigned int num_formats;
	struct uvc_function_config_format *formats;
};

struct uvc_video {
	struct uvc_device *uvc;
	struct usb_ep *ep;

	struct uvc_video_format format;

	/* Requests */
	unsigned int req_size;
	struct usb_request *req[UVC_NUM_REQUESTS];
	u8 *req_buffer[UVC_NUM_REQUESTS];
	struct list_head req_free;

	void (*encode) (struct usb_request *req, struct uvc_video *video,
			struct uvc_buffer *buf);

	/* Context data used by the completion handler */
	u32 payload_size;
	u32 max_payload_size;

	spinlock_t lock;

	struct list_head queue;
	int queue_count;
	unsigned int fid;
};

enum uvc_state {
	UVC_STATE_DISCONNECTED,
	UVC_STATE_CONNECTED,
	UVC_STATE_STREAMING,
};

struct uvc_device {
	int id;
	enum uvc_state state;
	struct usb_function func;
	struct uvc_video video;

	/* Descriptors */
	struct {
		const struct uvc_descriptor_header * const *fs_control;
		const struct uvc_descriptor_header * const *fs_streaming;
		const struct uvc_descriptor_header * const *hs_streaming;
	} desc;

	struct uvc_callback uvc_callback;
	unsigned int control_intf;
	struct usb_request *control_req;
	void *control_buf;

	unsigned int streaming_intf;

	struct uvc_streaming_control probe;
	struct uvc_streaming_control commit;

	unsigned int					streaming_interval;
	unsigned int					streaming_maxpacket;

	const struct uvc_function_config_streaming *uvc_config_stream;

	struct uvc_control_request setup_request;
	unsigned char setup_interface;

	uint32_t 			ops_busy;
	uint8_t		exit_flag;
	thread_waiter_t	exit_wait;
	thread_waiter_t	connect_wait;
	thread_waiter_t	write_wait;
	thread_waiter_t	stream_wait;
};

static inline struct uvc_device *to_uvc(struct usb_function *f)
{
	return container_of(f, struct uvc_device, func);
}

void uvc_n_free(struct usb_function *f);
struct usb_function *uvc_n_alloc(void);

#endif /* _UVC_GADGET_H_ */
