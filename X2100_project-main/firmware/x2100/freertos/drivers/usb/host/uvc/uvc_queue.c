#include "uvcvideo.h"

int uvc_queue_init(struct uvc_video_queue *queue, int drop_corrupted)
{
	spin_lock_init(&queue->lock);
	thread_waiter_init(&queue->waiter);
	thread_waiter_init(&queue->exit_waiter);
	queue->used = 0;

	queue->frame_nums = 0;
	queue->frame_size = 0;

	INIT_LIST_HEAD(&queue->irqqueue);
	INIT_LIST_HEAD(&queue->ready_queue);
	queue->flags = drop_corrupted ? UVC_QUEUE_DROP_CORRUPTED : 0;

	return 0;
}

/*
 * Cancel the video buffers queue.
 *
 * Cancelling the queue marks all buffers on the irq queue as erroneous,
 * wakes them up and removes them from the queue.
 *
 * If the disconnect parameter is set, further calls to uvc_queue_buffer will
 * fail with -ENODEV.
 *
 * This function acquires the irq spinlock and can be called from interrupt
 * context.
 */
void uvc_queue_cancel(struct uvc_video_queue *queue, int disconnect)
{
	unsigned long flags;

	usb_spin_lock_irqsave(&queue->lock, flags);
	while (!list_empty(&queue->irqqueue)) {
		struct uvc_buffer *buf = list_first_entry(&queue->irqqueue, struct uvc_buffer, queue);
		buf->error = 0;
		buf->bytesused = 0;
		buf->state = UVC_BUF_STATE_ERROR;
		list_move_tail(&buf->queue, &queue->ready_queue);
		thread_waiter_wakeup(&queue->waiter);
	}

	/*
	 * This must be protected by the lock spinlock to avoid race
	 * conditions between uvc_buffer_queue and the disconnection event that
	 * could result in an interruptible wait in uvc_dequeue_buffer. Do not
	 * blindly replace this logic by checking for the UVC_QUEUE_DISCONNECTED
	 * state outside the queue code.
	 */
	if (disconnect)
		queue->flags |= UVC_QUEUE_DISCONNECTED;

	usb_spin_unlock_irqrestore(&queue->lock, flags);
}

/*
 * uvc_queue_get_current_buffer: Obtain the current working output buffer
 *
 * Buffers may span multiple packets, and even URBs, therefore the active buffer
 * remains on the queue until the EOF marker.
 */
static struct uvc_buffer *
__uvc_queue_get_current_buffer(struct uvc_video_queue *queue)
{
	if (list_empty(&queue->irqqueue))
		return NULL;

	return list_first_entry(&queue->irqqueue, struct uvc_buffer, queue);
}

struct uvc_buffer *uvc_queue_get_current_buffer(struct uvc_video_queue *queue)
{
	struct uvc_buffer *nextbuf;
	unsigned long flags;

	usb_spin_lock_irqsave(&queue->lock, flags);
	nextbuf = __uvc_queue_get_current_buffer(queue);
	usb_spin_unlock_irqrestore(&queue->lock, flags);

	return nextbuf;
}

struct uvc_buffer *uvc_queue_next_buffer(struct uvc_video_queue *queue,
		struct uvc_buffer *buf)
{
	struct uvc_buffer *nextbuf;
	unsigned long flags;

	usb_spin_lock_irqsave(&queue->lock, flags);
	list_del(&buf->queue);
	if (buf->error && (queue->flags & UVC_QUEUE_DROP_CORRUPTED)
			&& !(queue->flags & UVC_QUEUE_DISCONNECTED)) {
		buf->error = 0;
		buf->bytesused = 0;
		buf->state = UVC_BUF_STATE_QUEUED;
		list_add_tail(&buf->queue, &queue->irqqueue);
	} else {
		buf->state = buf->error ? UVC_BUF_STATE_ERROR : UVC_BUF_STATE_DONE;
		list_add_tail(&buf->queue, &queue->ready_queue);
		thread_waiter_wakeup(&queue->waiter);
	}

	nextbuf = __uvc_queue_get_current_buffer(queue);
	usb_spin_unlock_irqrestore(&queue->lock, flags);

	return nextbuf;
}
