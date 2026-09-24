/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _USB_VIDEO_H_
#define _USB_VIDEO_H_

#include <le_byteshift.h>
#include "../usb.h"
#include <usb/host_uvc.h>
/* --------------------------------------------------------------------------
 * UVC constants
 */

#define UVC_TERM_INPUT			0x0000
#define UVC_TERM_OUTPUT			0x8000
#define UVC_TERM_DIRECTION(term)	((term)->type & 0x8000)

#define UVC_ENTITY_TYPE(entity)		((entity)->type & 0x7fff)
#define UVC_ENTITY_IS_UNIT(entity)	(((entity)->type & 0xff00) == 0)
#define UVC_ENTITY_IS_TERM(entity)	(((entity)->type & 0xff00) != 0)
#define UVC_ENTITY_IS_ITERM(entity) \
	(UVC_ENTITY_IS_TERM(entity) && \
	((entity)->type & 0x8000) == UVC_TERM_INPUT)
#define UVC_ENTITY_IS_OTERM(entity) \
	(UVC_ENTITY_IS_TERM(entity) && \
	((entity)->type & 0x8000) == UVC_TERM_OUTPUT)

#define UVC_EXT_GPIO_UNIT		0x7ffe
#define UVC_EXT_GPIO_UNIT_ID		0x100

/* Data types for UVC control data */
#define UVC_CTRL_DATA_TYPE_RAW		0
#define UVC_CTRL_DATA_TYPE_SIGNED	1
#define UVC_CTRL_DATA_TYPE_UNSIGNED	2
#define UVC_CTRL_DATA_TYPE_BOOLEAN	3
#define UVC_CTRL_DATA_TYPE_ENUM		4
#define UVC_CTRL_DATA_TYPE_BITMASK	5

/* Control flags */
#define UVC_CTRL_FLAG_SET_CUR		(1 << 0)
#define UVC_CTRL_FLAG_GET_CUR		(1 << 1)
#define UVC_CTRL_FLAG_GET_MIN		(1 << 2)
#define UVC_CTRL_FLAG_GET_MAX		(1 << 3)
#define UVC_CTRL_FLAG_GET_RES		(1 << 4)
#define UVC_CTRL_FLAG_GET_DEF		(1 << 5)
/* Control should be saved at suspend and restored at resume. */
#define UVC_CTRL_FLAG_RESTORE		(1 << 6)
/* Control can be updated by the camera. */
#define UVC_CTRL_FLAG_AUTO_UPDATE	(1 << 7)
/* Control supports asynchronous reporting */
#define UVC_CTRL_FLAG_ASYNCHRONOUS	(1 << 8)

#define UVC_CTRL_FLAG_GET_RANGE \
	(UVC_CTRL_FLAG_GET_CUR | UVC_CTRL_FLAG_GET_MIN | \
	 UVC_CTRL_FLAG_GET_MAX | UVC_CTRL_FLAG_GET_RES | \
	 UVC_CTRL_FLAG_GET_DEF)

/* ------------------------------------------------------------------------
 * Driver specific constants.
 */

/* Number of isochronous URBs. */
#define UVC_URBS		5
/* Maximum number of packets per URB. */
#define UVC_MAX_PACKETS		32

#define UVC_CTRL_CONTROL_TIMEOUT	5000
#define UVC_CTRL_STREAMING_TIMEOUT	5000

/* Format flags */
#define UVC_FMT_FLAG_COMPRESSED		0x00000001

/* ------------------------------------------------------------------------
 * Structures.
 */

struct uvc_device;

struct uvc_entity {
	struct list_head list;		/* Entity as part of a UVC device. */
	struct list_head chain;		/* Entity as part of a video device chain. */

	/*
	 * Entities exposed by the UVC device use IDs 0-255, extra entities
	 * implemented by the driver (such as the GPIO entity) use IDs 256 and
	 * up.
	 */
	u16 id;
	u16 type;
	char name[64];

	union {
		struct {
			u16 wObjectiveFocalLengthMin;
			u16 wObjectiveFocalLengthMax;
			u16 wOcularFocalLength;
			u8  bControlSize;
			u8  *bmControls;
		} camera;

		struct {
			u8  bControlSize;
			u8  *bmControls;
			u8  bTransportModeSize;
			u8  *bmTransportModes;
		} media;

		struct {
		} output;

		struct {
			u16 wMaxMultiplier;
			u8  bControlSize;
			u8  *bmControls;
			u8  bmVideoStandards;
		} processing;

		struct {
		} selector;

		struct {
			u8  bNumControls;
			u8  bControlSize;
			u8  *bmControls;
			u8  *bmControlsType;
		} extension;

		struct {
			u8  bControlSize;
			u8  *bmControls;
			struct gpio_desc *gpio_privacy;
			int irq;
		} gpio;
	};

	u8 bNrInPins;
	u8 *baSourceID;
};

struct uvc_streaming_header {
	u8 bNumFormats;
	u8 bEndpointAddress;
	u8 bTerminalLink;
	u8 bControlSize;
	u8 *bmaControls;
	/* The following fields are used by input headers only. */
	u8 bmInfo;
	u8 bStillCaptureMethod;
	u8 bTriggerSupport;
	u8 bTriggerUsage;
};

#define UVC_QUEUE_INITIALIZED		(1 << 0)
#define UVC_QUEUE_CANCELING			(1 << 1)
#define UVC_QUEUE_DISCONNECTED		(1 << 2)
#define UVC_QUEUE_DROP_CORRUPTED	(1 << 3)

struct uvc_video_queue {
	spinlock_t lock;
	thread_waiter_t waiter;
	thread_waiter_t exit_waiter;
	unsigned int used;

	unsigned int frame_nums;
	unsigned int frame_size;

	struct list_head irqqueue;
	struct list_head ready_queue;

	unsigned int flags;
	unsigned int buf_used;
};

struct uvc_video_chain {
	struct uvc_device *dev;
	struct list_head list;

	struct list_head entities;		/* All entities */
	struct uvc_entity *processing;		/* Processing unit */
	struct uvc_entity *selector;		/* Selector unit */
};

/**
 * struct uvc_urb - URB context management structure
 *
 * @urb: the URB described by this context structure
 * @stream: UVC streaming context
 * @buffer: memory storage for the URB
 * @dma: Allocated DMA handle
 * @sgt: sgt_table with the urb locations in memory
 * @work: work queue entry for asynchronous decode
 */
struct uvc_urb {
	struct list_head queue;
	struct urb *urb;
	struct uvc_streaming *stream;
	int decode_packet_index;

	char *buffer;
};

struct uvc_streaming {
	struct list_head list;
	struct uvc_device *dev;
	struct uvc_video_chain *chain;

	struct usb_interface *intf;
	int intfnum;
	u16 maxpsize;

	struct uvc_streaming_header header;
	enum uvc_stream_type type;

	unsigned int nformats;
	struct uvc_host_format *formats;

	struct uvc_streaming_control ctrl;
	const struct uvc_host_format *cur_format;
	const struct uvc_host_frame *cur_frame;
	u32 cur_interval;

	/*
	 * Protect access to ctrl, cur_format, cur_frame and hardware video
	 * probe control.
	 */
	struct mutex mutex;

	/* Buffers queue. */
	struct uvc_video_queue queue;
	int (*decode)(struct uvc_urb *uvc_urb, struct uvc_buffer *buf);

	/* Context data used by the bulk completion handler. */
	struct {
		u8 header[256];
		unsigned int header_size;
		int skip_payload;
		u32 payload_size;
		u32 max_payload_size;
	} bulk;

	struct uvc_urb uvc_urb[UVC_URBS];

	spinlock_t wait_queue_lock;
	struct list_head wait_queue;

	unsigned int urb_size;

	u8 last_fid;

	bool stream_on;
};

#define for_each_uvc_urb(uvc_urb, uvc_streaming) \
	for ((uvc_urb) = &(uvc_streaming)->uvc_urb[0]; \
	     (uvc_urb) < &(uvc_streaming)->uvc_urb[UVC_URBS]; \
	     ++(uvc_urb))

static inline u32 uvc_urb_index(const struct uvc_urb *uvc_urb)
{
	return uvc_urb - &uvc_urb->stream->uvc_urb[0];
}

struct uvc_device {
	struct usb_device *udev;
	struct usb_interface *intf;
	int intfnum;
	char name[32];

	/* Video control interface */
	u16 uvc_version;
	u32 clock_frequency;

	struct list_head entities;
	struct list_head chains;

	/* Video Streaming interfaces */
	unsigned int active_streams;
	struct list_head streams;

	/* Status Interrupt Endpoint */
	struct usb_host_endpoint *int_ep;
	struct urb *int_urb;
	struct uvc_status *status;

	u8 minor;				/* uvc minor number */
	bool opened;				/* someone has this acm's device open */
	bool exiting;
	unsigned int used;

	uvc_notify_callback_t notify_callback;
};

extern unsigned int uvc_no_drop_param;
extern unsigned int uvc_timeout_param;
/* --------------------------------------------------------------------------
 * Internal functions.
 */

/* Core driver */
extern struct uvc_device *uvc_table[UVC_MINORS];
extern struct mutex device_lock[UVC_MINORS];
extern thread_cond_t free_cond[UVC_MINORS];

void update_devices_bit(void);

struct uvc_entity *uvc_entity_by_id(struct uvc_device *dev, int id);

void uvc_dev_free(struct uvc_device *dev);
void uvc_delete(struct uvc_device *dev);

int uvc_video_start_streaming(struct uvc_streaming *stream);
void uvc_video_stop_streaming(struct uvc_streaming *stream);

void uvc_video_retry_complete(struct uvc_streaming *stream);

/* Video */
int uvc_video_init(struct uvc_streaming *stream);
int uvc_probe_video(struct uvc_streaming *stream,
	struct uvc_streaming_control *probe);

/* Video buffers queue management. */
int uvc_queue_init(struct uvc_video_queue *queue, int drop_corrupted);
void uvc_queue_cancel(struct uvc_video_queue *queue, int disconnect);
struct uvc_buffer *uvc_queue_next_buffer(struct uvc_video_queue *queue,
		struct uvc_buffer *buf);
struct uvc_buffer *uvc_queue_get_current_buffer(struct uvc_video_queue *queue);

/* Status */
int uvc_status_init(struct uvc_device *dev);
void uvc_status_unregister(struct uvc_device *dev);
void uvc_status_cleanup(struct uvc_device *dev);
int uvc_status_start(struct uvc_device *dev);
void uvc_status_stop(struct uvc_device *dev);

/* Utility functions */
struct usb_host_endpoint *uvc_find_endpoint(struct usb_host_interface *alts,
					    u8 epaddr);
u16 uvc_endpoint_max_bpi(struct usb_device *dev, struct usb_host_endpoint *ep);

#endif
