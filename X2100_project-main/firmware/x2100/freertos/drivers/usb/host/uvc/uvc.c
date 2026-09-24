#include <usb/host_uvc.h>

#include "uvcvideo.h"


static u32 devices_bit;
static uvc_device_callback_t device_callback;

struct uvc_device *uvc_table[UVC_MINORS];
struct mutex device_lock[UVC_MINORS];
thread_cond_t free_cond[UVC_MINORS];

#define UVC_READY(dev)	(dev && dev->udev && dev->opened && !dev->exiting)

void usb_host_uvc_register_callback(uvc_device_callback_t callback)
{
	device_callback = callback;
}

u32 usb_host_uvc_get_devices_bit(void)
{
	return devices_bit;
}

void update_devices_bit(void)
{
	int i;
	struct uvc_device *dev;
	u32 dev_bit = 0;
	uvc_device_callback_t callback = device_callback;

	for (i = 0; i < UVC_MINORS; i++) {
		dev = uvc_table[i];
		if (dev && dev->udev)
			dev_bit |= (1 << i);
	}

	devices_bit = dev_bit;
	if (callback)
		callback(dev_bit);
}

int usb_host_uvc_open(u8 id, uvc_notify_callback_t callback)
{
	int ret = 0;
	struct uvc_device *dev = NULL;

	if (id >= UVC_MINORS) {
		printf("%s: id(%d) >= UVC_MINORS\n", __func__, id);
		return -ENODEV;
	}

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!dev || !dev->udev) {
		printf("%s: no uvc(%d) devices\n", __func__, id);
		ret = -ENODEV;
		goto err_out;
	}

	if (dev->opened) {
		printf("%s: uvc(%d) devices busy\n", __func__, id);
		ret = -EBUSY;
		goto err_out;
	}

	if (callback) {
		ret = uvc_status_start(dev);
		if (ret < 0) {
			printf("%s: uvc(%d) uvc_status_start fail %d\n", __func__, id, ret);
			goto err_out;
		}
	}

	dev->opened = true;
	dev->notify_callback = callback;

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_close(u8 id)
{
	int ret = 0;
	struct uvc_device *dev;

	if (id >= UVC_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!dev || !dev->opened || dev->exiting) {
		ret = -EINVAL;
		goto err_out;
	}

	dev->exiting = true;

	while (dev->used)
		thread_cond_wait(&free_cond[id], &device_lock[id]);

	dev->exiting = false;
	dev->opened = false;

	if (dev->udev) {
		uvc_status_stop(dev);
	} else {
		assert(dev->minor == id);
		uvc_table[dev->minor] = NULL;
		uvc_dev_free(dev);
	}

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_get_video_num(u8 id)
{
	struct uvc_device *dev;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto err_out;
	}

	ret = dev->active_streams;
err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_get_video(u8 id, u8 video_id, struct uvc_host_video *video)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	int num = 0;
	int ret = -ENXIO;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (video == NULL)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto err_out;
	}

	list_for_each_entry(stream, &dev->streams, list) {
		if (stream->chain) {
			if (num == video_id) {
				video->stream = stream;
				video->type = stream->type;
				video->nformats = stream->nformats;
				video->formats = stream->formats;
				ret = 0;
				break;
			}
			num++;
		}
	}

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

/*
 * Find the frame interval closest to the requested frame interval for the
 * given frame format and size. This should be done by the device as part of
 * the Video Probe and Commit negotiation, but some hardware don't implement
 * that feature.
 */
static u32 uvc_try_frame_interval(const struct uvc_host_frame *frame, u32 interval)
{
	unsigned int i;

	if (frame->bFrameIntervalType) {
		u32 best = -1, dist;

		for (i = 0; i < frame->bFrameIntervalType; ++i) {
			dist = interval > frame->dwFrameInterval[i]
			     ? interval - frame->dwFrameInterval[i]
			     : frame->dwFrameInterval[i] - interval;

			if (dist > best)
				break;

			best = dist;
		}

		interval = frame->dwFrameInterval[i-1];
	} else {
		const u32 min = frame->dwFrameInterval[0];
		const u32 max = frame->dwFrameInterval[1];
		const u32 step = frame->dwFrameInterval[2];

		interval = min + (interval - min + step/2) / step * step;
		if (interval > max)
			interval = max;
	}

	return interval;
}

int usb_host_uvc_set_format(u8 id, const struct uvc_host_video *video, u8 format_index, u8 frame_index, u32 interval)
{
	int i;
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	struct uvc_video_queue *queue;
	const struct uvc_host_format *format;
	const struct uvc_host_frame *frame;
	unsigned int maxIntervalIndex;
	struct uvc_streaming_control probe;
	unsigned long flags;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream || format_index >= video->nformats || frame_index >= video->formats->nframes)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto err_out;
	}

	stream = video->stream;
	queue = &stream->queue;

	usb_spin_lock_irqsave(&queue->lock, flags);
	if (queue->flags & UVC_QUEUE_INITIALIZED)
		ret = -EBUSY;

	if (queue->flags & UVC_QUEUE_DISCONNECTED)
		ret = -ENODEV;
	usb_spin_unlock_irqrestore(&queue->lock, flags);
	if (ret < 0)
		goto err_out;

	format = &stream->formats[format_index];
	frame = &format->frames[frame_index];

	/*
		* Clamp the frame interval to the boundaries. A zero
		* bFrameIntervalType value indicates a continuous frame
		* interval range, with dwFrameInterval[0] storing the minimum
		* value and dwFrameInterval[1] storing the maximum value.
		*/
	maxIntervalIndex = frame->bFrameIntervalType ? frame->bFrameIntervalType - 1 : 1;
	interval = clamp(interval, frame->dwFrameInterval[0],
			frame->dwFrameInterval[maxIntervalIndex]);

	/* Set the format index, frame index and frame interval. */
	memset(&probe, 0, sizeof(struct uvc_streaming_control));
	probe.bmHint = 1;	/* dwFrameInterval */
	probe.bFormatIndex = stream->formats[format_index].index;
	probe.bFrameIndex = stream->formats[format_index].frames[frame_index].bFrameIndex;
	probe.dwFrameInterval = uvc_try_frame_interval(frame, interval);

	mutex_lock(&stream->mutex);
	/* Probe the device. */
	ret = uvc_probe_video(stream, &probe);
	mutex_unlock(&stream->mutex);
	if (ret < 0)
		goto err_out;

	/*
	 * After the probe, update fmt with the values returned from
	 * negotiation with the device. Some devices return invalid bFormatIndex
	 * and bFrameIndex values, in which case we can only assume they have
	 * accepted the requested format as-is.
	 */
	for (i = 0; i < stream->nformats; ++i) {
		if (probe.bFormatIndex == stream->formats[i].index) {
			format = &stream->formats[i];
			break;
		}
	}

	if (i == stream->nformats)
		printf("Unknown bFormatIndex %u, using default\n", probe.bFormatIndex);

	for (i = 0; i < format->nframes; ++i) {
		if (probe.bFrameIndex == format->frames[i].bFrameIndex) {
			frame = &format->frames[i];
			break;
		}
	}

	if (i == format->nframes)
		printf("Unknown bFrameIndex %u, using default\n", probe.bFrameIndex);

	mutex_lock(&stream->mutex);
	stream->ctrl = probe;
	stream->cur_format = format;
	stream->cur_frame = frame;
	stream->cur_interval = probe.dwFrameInterval;
	mutex_unlock(&stream->mutex);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_get_format(u8 id, const struct uvc_host_video *video, struct uvc_video_format *video_format)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream || !video_format)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto err_out;
	}

	stream = video->stream;

	mutex_lock(&stream->mutex);

	video_format->fcc = stream->cur_format->fcc;
	video_format->width = stream->cur_frame->wWidth;
	video_format->height = stream->cur_frame->wHeight;
	video_format->bpp = stream->cur_format->bpp;
	video_format->fps = 10000000 / stream->cur_interval;

	mutex_unlock(&stream->mutex);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_request_buffer(u8 id, const struct uvc_host_video *video, u32 frame_nums, u32 frame_size)
{
	int i;
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	struct uvc_video_queue *queue;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream || !frame_nums || !frame_size)
		return -EINVAL;

	if (video->type != UVC_STREAM_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto dev_out;
	}

	stream = video->stream;
	queue = &stream->queue;
	usb_spin_lock_irqsave(&queue->lock, flags);
	if (queue->flags & UVC_QUEUE_INITIALIZED) {
		ret = -EBUSY;
		goto queue_out;
	}

	if (queue->flags & UVC_QUEUE_CANCELING) {
		ret = -ESRCH;
		goto queue_out;
	}

	if (queue->flags & UVC_QUEUE_DISCONNECTED) {
		ret = -ENODEV;
		goto queue_out;
	}

	queue->buf_used = 0;

	for (i = 0; i < frame_nums; i++) {
		buf = malloc(sizeof(struct uvc_buffer));
		if (!buf) {
			ret = -ENOMEM;
			goto buf_out;
		}

		buf->mem = cache_align_malloc(frame_size);
		if (!buf->mem) {
			free(buf);
			ret = -ENOMEM;
			goto buf_out;
		}

		buf->error = 0;
		buf->bytesused = 0;
		buf->length = frame_size;
		buf->state = UVC_BUF_STATE_QUEUED;
		list_add_tail(&buf->queue, &queue->irqqueue);
	}

	queue->frame_nums = frame_nums;
	queue->frame_size = frame_size;
	queue->flags |= UVC_QUEUE_INITIALIZED;
	usb_spin_unlock_irqrestore(&queue->lock, flags);

	dev->used++;
	mutex_unlock(&device_lock[id]);
	return 0;

buf_out:
	while (!list_empty(&queue->irqqueue)) {
		buf = list_first_entry(&queue->irqqueue, struct uvc_buffer, queue);
		list_del(&buf->queue);
		free(buf->mem);
		free(buf);
	}
queue_out:
	usb_spin_unlock_irqrestore(&queue->lock, flags);
dev_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_free_buffer(u8 id, const struct uvc_host_video *video)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	struct uvc_video_queue *queue;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret = 0;
	unsigned int frame_nums = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!dev || !dev->opened) {
		mutex_unlock(&device_lock[id]);
		return -ENODEV;
	}
	dev->used++;
	mutex_unlock(&device_lock[id]);

	stream = video->stream;
	mutex_lock(&stream->mutex);
	ret = stream->stream_on;
	mutex_unlock(&stream->mutex);
	if (ret) {
		ret = -EBUSY;
		goto dev_out;
	}

	queue = &stream->queue;
	usb_spin_lock_irqsave(&queue->lock, flags);
	if (!(queue->flags & UVC_QUEUE_INITIALIZED)) {
		ret = -EINVAL;
		goto queue_out;
	}

	if (queue->flags & UVC_QUEUE_CANCELING) {
		ret = -EBUSY;
		goto queue_out;
	}

	queue->flags |= UVC_QUEUE_CANCELING;
	while (queue->used) {
		thread_waiter_wakeup(&queue->waiter);
		thread_waiter_wait(&queue->exit_waiter);
	}

	while (!list_empty(&queue->irqqueue)) {
		buf = list_first_entry(&queue->irqqueue, struct uvc_buffer, queue);
		list_del(&buf->queue);
		free(buf->mem);
		free(buf);
		frame_nums++;
	}

	while (!list_empty(&queue->ready_queue)) {
		buf = list_first_entry(&queue->ready_queue, struct uvc_buffer, queue);
		list_del(&buf->queue);
		free(buf->mem);
		free(buf);
		frame_nums++;
	}

	assert(frame_nums == queue->frame_nums);
	queue->frame_nums = 0;
	queue->frame_size = 0;
	queue->flags &= ~UVC_QUEUE_CANCELING;
	queue->flags &= ~UVC_QUEUE_INITIALIZED;

queue_out:
	usb_spin_unlock_irqrestore(&queue->lock, flags);
dev_out:
	mutex_lock(&device_lock[id]);
	if (!ret)
		dev->used--;

	dev->used--;
	if (dev->exiting && !dev->used)
		thread_cond_signal(&free_cond[id]);
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_stream_on(u8 id, const struct uvc_host_video *video)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	struct uvc_video_queue *queue;
	unsigned long flags;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto dev_out;
	}

	stream = video->stream;
	mutex_lock(&stream->mutex);

	queue = &stream->queue;
	usb_spin_lock_irqsave(&queue->lock, flags);
	if (!(queue->flags & UVC_QUEUE_INITIALIZED) || (queue->flags & UVC_QUEUE_CANCELING))
		ret = -ESRCH;

	if (queue->flags & UVC_QUEUE_DISCONNECTED)
		ret = -ENODEV;

	usb_spin_unlock_irqrestore(&queue->lock, flags);

	if (!ret)
		ret = uvc_video_start_streaming(stream);

	mutex_unlock(&stream->mutex);
dev_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_stream_off(u8 id, const struct uvc_host_video *video)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!UVC_READY(dev)) {
		ret = -ENODEV;
		goto err_out;
	}

	stream = video->stream;
	mutex_lock(&stream->mutex);
	uvc_video_stop_streaming(stream);
	mutex_unlock(&stream->mutex);

err_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_get_buffer(u8 id, const struct uvc_host_video *video, struct uvc_buffer **buf_p, u32 timeout_ms)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	struct uvc_video_queue *queue;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream || !buf_p)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!dev || !dev->opened || dev->exiting) {
		mutex_unlock(&device_lock[id]);
		return -ENODEV;
	}
	dev->used++;
	mutex_unlock(&device_lock[id]);

	stream = video->stream;
	queue = &stream->queue;

	usb_spin_lock_irqsave(&queue->lock, flags);

	if (!(queue->flags & UVC_QUEUE_INITIALIZED) || (queue->flags & UVC_QUEUE_CANCELING)) {
		ret = -ESRCH;
		goto queue_out;
	}

	if (list_empty(&queue->ready_queue)) {
		if (queue->flags & UVC_QUEUE_DISCONNECTED) {
			ret = -ENODATA;
			goto queue_out;
		}

		if (!timeout_ms) {
			ret = -ENODATA;
			goto queue_out;
		}

		queue->used++;

		/*
		 * Avoid host uvc driver wakeup causing subsequent usb_host_uvc_get_buffer
		 * does not execute thread_wait_timeout.
		 */
		thread_waiter_init(&queue->waiter);

		usb_spin_unlock_irqrestore(&queue->lock, flags);
		ret = thread_waiter_wait_timeout(&queue->waiter, timeout_ms);
		usb_spin_lock_irqsave(&queue->lock, flags);
		queue->used--;
		if (queue->flags & UVC_QUEUE_CANCELING && !queue->used) {
			thread_waiter_wakeup(&queue->exit_waiter);
			ret = -EINTR;
			goto queue_out;
		}

		if (ret) {
			ret = -ETIMEDOUT;
			goto queue_out;
		}
	}

	if (list_empty(&queue->ready_queue)) {
		ret = -ENODATA;
		goto queue_out;
	}

	buf = list_first_entry(&queue->ready_queue, struct uvc_buffer, queue);
	list_del_init(&buf->queue);
	*buf_p = buf;

queue_out:
	usb_spin_unlock_irqrestore(&queue->lock, flags);
	mutex_lock(&device_lock[id]);
	dev->used--;
	mutex_unlock(&device_lock[id]);
	return ret;
}

int usb_host_uvc_put_buffer(u8 id, const struct uvc_host_video *video, struct uvc_buffer *buf)
{
	struct uvc_device *dev;
	struct uvc_streaming *stream;
	struct uvc_video_queue *queue;
	unsigned long flags;
	int ret = 0;

	if (id >= UVC_MINORS)
		return -ENODEV;

	if (!video || !video->stream || !buf)
		return -EINVAL;

	mutex_lock(&device_lock[id]);
	dev = uvc_table[id];
	if (!dev || !dev->opened) {
		ret = -ENODEV;
		goto dev_out;
	}

	stream = video->stream;
	queue = &stream->queue;

	usb_spin_lock_irqsave(&queue->lock, flags);
	if (!(queue->flags | UVC_QUEUE_INITIALIZED)) {
		ret = -ESRCH;
		goto queue_out;
	}

	buf->error = 0;
	buf->bytesused = 0;
	buf->length = queue->frame_size;

	if (queue->flags & UVC_QUEUE_DISCONNECTED) {
		buf->state = UVC_BUF_STATE_ERROR;
		list_add_tail(&buf->queue, &queue->ready_queue);
	} else {
		buf->state = UVC_BUF_STATE_QUEUED;
		list_add_tail(&buf->queue, &queue->irqqueue);
	}

queue_out:
	usb_spin_unlock_irqrestore(&queue->lock, flags);

	mutex_lock(&stream->mutex);
	uvc_video_retry_complete(stream);
	mutex_unlock(&stream->mutex);

dev_out:
	mutex_unlock(&device_lock[id]);
	return ret;
}