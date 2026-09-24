#include <common.h>
#include <os.h>
#include <assert.h>
#include <little_things.h>
#include <le_byteshift.h>
#include <driver/cache.h>

#include "uvc.h"
#include "uvc_video.h"

/* --------------------------------------------------------------------------
 * Video codecs
 */

static int uvc_video_encode_header(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	struct uvc_device *uvc = container_of(video, struct uvc_device, video);
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	int pos = 2;

	data[1] = UVC_STREAM_EOH | video->fid;

	if (buf->timestamp) {
		u32 pts;
		u32 sof, stc;

		pts = buf->timestamp * (CONFIG_EXTAL_CLOCK / 1000000);

		data[1] |= UVC_STREAM_PTS;
		put_unaligned_le32(pts, &data[pos]);
		pos += 4;

		sof = usb_gadget_frame_number(cdev->gadget) & 0x7FF;
		stc = systick_get_time_us() * (CONFIG_EXTAL_CLOCK / 1000000);

		data[1] |= UVC_STREAM_SCR;
		put_unaligned_le32(stc, &data[pos]);
		put_unaligned_le16(sof, &data[pos+4]);
		pos += 6;
	}

	data[0] = pos;

	if (buf->length - buf->bytesused <= len - pos)
		data[1] |= UVC_STREAM_EOF;

	return pos;
}

static int uvc_video_encode_data(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	unsigned int nbytes;
	const void *mem;

	/* Copy video data to the USB buffer. */
	mem = buf->mem + buf->bytesused;
	nbytes = min((unsigned int)len, buf->length - buf->bytesused);

	memcpy(data, mem, nbytes);
	buf->bytesused += nbytes;

	return nbytes;
}

static void uvc_video_encode_bulk(struct usb_request *req, struct uvc_video *video, struct uvc_buffer *buf)
{
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add a header at the beginning of the payload. */
	if (video->payload_size == 0) {
		ret = uvc_video_encode_header(video, buf, mem, len);
		video->payload_size += ret;
		mem += ret;
		len -= ret;
	}

	/* Process video data. */
	len = min((int)(video->max_payload_size - video->payload_size), len);
	ret = uvc_video_encode_data(video, buf, mem, len);

	video->payload_size += ret;
	len -= ret;

	req->length = video->req_size - len;
	req->zero = video->payload_size == video->max_payload_size;

	if (buf->bytesused == buf->length) {
		buf->state = UVC_BUF_STATE_DONE;
		list_del(&buf->queue);
		video->queue_count--;

		if (buf->complete)
			buf->complete(buf);

		video->fid ^= UVC_STREAM_FID;
		video->payload_size = 0;

		thread_waiter_wakeup(&video->uvc->write_wait);
	}

	if (video->payload_size == video->max_payload_size)
		video->payload_size = 0;
}

static void uvc_video_encode_isoc(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add the header. */
	ret = uvc_video_encode_header(video, buf, mem, len);
	mem += ret;
	len -= ret;

	/* Process video data. */
	ret = uvc_video_encode_data(video, buf, mem, len);
	len -= ret;

	req->length = video->req_size - len;

	if (buf->bytesused == buf->length) {
		buf->state = UVC_BUF_STATE_DONE;
		list_del(&buf->queue);
		video->queue_count--;

		if (buf->complete)
			buf->complete(buf);

		video->fid ^= UVC_STREAM_FID;
		thread_waiter_wakeup(&video->uvc->write_wait);
	}
}

/*
 * Cancel the video buffers queue.
 *
 * Cancelling the queue marks all buffers on the irq queue as erroneous,
 * wakes them up and removes them from the queue.
 */
static void uvcg_queue_cancel(struct uvc_video *video)
{
	struct uvc_buffer *buf;

	while (!list_empty(&video->queue)) {
		buf = list_first_entry(&video->queue, struct uvc_buffer, queue);
		list_del(&buf->queue);

		buf->state = UVC_BUF_STATE_ERROR;
		video->queue_count--;

		if (buf->complete)
			buf->complete(buf);
	}

	INIT_LIST_HEAD(&video->queue);
	thread_waiter_wakeup(&video->uvc->write_wait);
}

/* --------------------------------------------------------------------------
 * Request handling
 */

static int uvcg_video_ep_queue(struct uvc_video *video, struct usb_request *req)
{
	int ret;

	ret = usb_ep_queue(video->ep, req);
	if (ret < 0) {
		printf("%s: Failed to queue request (%d).\n", __func__, ret);

		/* Isochronous endpoints can't be halted. */
		if (usb_endpoint_xfer_bulk(video->ep->desc))
			usb_ep_set_halt(video->ep);
	}

	return ret;
}

/*
 * I somehow feel that synchronisation won't be easy to achieve here. We have
 * three events that control USB requests submission:
 *
 * - USB request completion: the completion handler will resubmit the request
 *   if a video buffer is available.
 *
 * - USB interface setting selection: in response to a SET_INTERFACE request,
 *   the handler will start streaming if a video buffer is available and if
 *   video is not currently streaming.
 *
 * - V4L2 buffer queueing: the driver will start streaming if video is not
 *   currently streaming.
 *
 * Race conditions between those 3 events might lead to deadlocks or other
 * nasty side effects.
 *
 * The "video currently streaming" condition can't be detected by the irqqueue
 * being empty, as a request can still be in flight. A separate "queue paused"
 * flag is thus needed.
 *
 * The paused flag will be set when we try to retrieve the irqqueue head if the
 * queue is empty, and cleared when we queue a buffer.
 *
 * The USB request completion handler will get the buffer at the irqqueue head
 * under protection of the queue spinlock. If the queue is empty, the streaming
 * paused flag will be set. Right after releasing the spinlock a userspace
 * application can queue a buffer. The flag will then cleared, and the ioctl
 * handler will restart the video stream.
 */
static void uvc_video_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uvc_video *video = req->context;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret;

	usb_spin_lock_irqsave(&video->lock, flags);

	switch (req->status) {
	case 0:
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		printf("%s: VS request cancelled.\n", __func__);
		uvcg_queue_cancel(video);
		goto requeue;

	default:
		printf("%s: VS request completed with status %d.\n", __func__, req->status);
		uvcg_queue_cancel(video);
		goto requeue;
	}

	if (list_empty(&video->queue))
		goto requeue;

	buf =  list_first_entry(&video->queue, struct uvc_buffer, queue);
	buf->state = UVC_BUF_STATE_ACTIVE;

	assert(video->encode != NULL);
	video->encode(req, video, buf);

	ret = uvcg_video_ep_queue(video, req);
	if (ret < 0) {
		uvcg_queue_cancel(video);
		goto requeue;
	}

	usb_spin_unlock_irqrestore(&video->lock, flags);

	return;

requeue:
	list_add_tail(&req->list, &video->req_free);
	usb_spin_unlock_irqrestore(&video->lock, flags);
}

static int uvc_video_free_requests(struct uvc_video *video)
{
	unsigned int i;

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		if (video->req[i]) {
			usb_ep_free_request(video->ep, video->req[i]);
			video->req[i] = NULL;
		}

		if (video->req_buffer[i]) {
			free(video->req_buffer[i]);
			video->req_buffer[i] = NULL;
		}
	}

	INIT_LIST_HEAD(&video->req_free);
	video->req_size = 0;
	return 0;
}

static int uvc_video_alloc_requests(struct uvc_video *video)
{
	unsigned int req_size;
	unsigned int i;
	int ret = -ENOMEM;

	assert(video->req_size == 0);

	req_size = video->ep->maxpacket * (video->ep->mult);

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		video->req_buffer[i] = cache_align_malloc(req_size);
		if (video->req_buffer[i] == NULL)
			goto error;

		video->req[i] = usb_ep_alloc_request(video->ep);
		if (video->req[i] == NULL)
			goto error;

		video->req[i]->buf = video->req_buffer[i];
		video->req[i]->length = 0;
		video->req[i]->complete = uvc_video_complete;
		video->req[i]->context = video;

		list_add_tail(&video->req[i]->list, &video->req_free);
	}

	video->req_size = req_size;

	return 0;

error:
	uvc_video_free_requests(video);
	return ret;
}

/* --------------------------------------------------------------------------
 * Video streaming
 */

/*
 * uvcg_video_pump - Pump video data into the USB requests
 *
 * This function fills the available USB requests (listed in req_free) with
 * video data from the queued buffers.
 */
void uvcg_video_pump(struct uvc_video *video)
{
	struct usb_request *req;
	struct uvc_buffer *buf;
	int ret;

	while (1) {
		if (list_empty(&video->req_free) || list_empty(&video->queue))
			break;

		req = list_first_entry(&video->req_free, struct usb_request, list);
		list_del(&req->list);

		buf =  list_first_entry(&video->queue, struct uvc_buffer, queue);

		buf->state = UVC_BUF_STATE_ACTIVE;
		assert(video->encode);
		video->encode(req, video, buf);

		/* Queue the USB request */
		ret = uvcg_video_ep_queue(video, req);
		if (ret < 0) {
			uvcg_queue_cancel(video);
			list_add_tail(&req->list, &video->req_free);
			break;
		}
	}
}

/*
 * Enable or disable the video stream.
 */
int uvcg_video_enable(struct uvc_video *video, int enable)
{
	unsigned long flags;
	unsigned int i;
	int ret;

	if (video->ep == NULL) {
		printf("Video enable failed, device is uninitialized.\n");
		return -ENODEV;
	}

	usb_spin_lock_irqsave(&video->lock, flags);

	if (!enable) {
		for (i = 0; i < UVC_NUM_REQUESTS; ++i)
			if (video->req[i])
				usb_ep_dequeue(video->ep, video->req[i]);

		uvc_video_free_requests(video);
		usb_spin_unlock_irqrestore(&video->lock, flags);
		return 0;
	}

	if ((ret = uvc_video_alloc_requests(video)) < 0) {
		usb_spin_unlock_irqrestore(&video->lock, flags);
		printf("%s: uvc_video_alloc_requests error %d\n", __func__, ret);
		return ret;
	}

	if (video->max_payload_size) {
		video->encode = uvc_video_encode_bulk;
		video->payload_size = 0;
	} else
		video->encode = uvc_video_encode_isoc;

	uvcg_video_pump(video);
	usb_spin_unlock_irqrestore(&video->lock, flags);

	return 0;
}