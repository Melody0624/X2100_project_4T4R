#include "usb.h"
#include "hcd.h"

/**
 * usb_init_urb - initializes a urb so that it can be used by a USB driver
 * @urb: pointer to the urb to initialize
 *
 * Initializes a urb so that the USB subsystem can use it properly.
 *
 * If a urb is created with a call to usb_alloc_urb() it is not
 * necessary to call this function.  Only use this if you allocate the
 * space for a struct urb on your own.  If you call this function, be
 * careful when freeing the memory for your urb that it is no longer in
 * use by the USB core.
 *
 * Only use this function if you _really_ understand what you are doing.
 */
void usb_init_urb(struct urb *urb)
{
	if (urb) {
		memset(urb, 0, sizeof(*urb));
		INIT_LIST_HEAD(&urb->urb_list_entry);
		spin_lock_init(&urb->lock);
	}
}

/**
 * usb_alloc_urb - creates a new urb for a USB driver to use
 * @iso_packets: number of iso packets for this urb
 *
 * Creates an urb for the USB driver to use, initializes a few internal
 * structures, incrementes the usage counter, and returns a pointer to it.
 *
 * If no memory is available, NULL is returned.
 *
 * If the driver want to use this urb for interrupt, control, or bulk
 * endpoints, pass '0' as the number of iso packets.
 *
 * The driver must call usb_free_urb() when it is finished with the urb.
 */
struct urb *usb_alloc_urb(int iso_packets)
{
	struct urb *urb;

	urb = (struct urb *)malloc(sizeof(struct urb) + 
		iso_packets * sizeof(struct usb_iso_packet_descriptor));
	if (!urb) {
		printf("alloc_urb: malloc failed");
		return NULL;
	}
	usb_init_urb(urb);
	return urb;
}

/**
 * usb_free_urb - frees the memory used by a urb when all users of it are finished
 * @urb: pointer to the urb to free, may be NULL
 *
 * Must be called when a user of a urb is finished with it.  When the last user
 * of the urb calls this function, the memory of the urb is freed.
 *
 * Note: The transfer buffer associated with the urb is not freed, that must be
 * done elsewhere.
 */
void usb_free_urb(struct urb *urb)
{
	if (urb) {
		if (urb->kref)
			panic("ERROR: usb urb reference counter non-zero %d\n", urb->kref);

		free(urb);
	}
}

/**
 * usb_get_urb - increments the reference count of the urb
 * @urb: pointer to the urb to modify, may be NULL
 *
 * This must be  called whenever a urb is transferred from a device driver to a
 * host controller driver.  This allows proper reference counting to happen
 * for urbs.
 *
 * A pointer to the urb with the incremented reference counter is returned.
 */
void usb_get_urb(struct urb *urb)
{
	if (urb)
		urb->kref++;
}

void usb_put_urb(struct urb *urb)
{
	if (urb)
		urb->kref--;
}
/*-------------------------------------------------------------------*/

int usb_submit_urb(struct urb *urb)
{
	int			pipe, temp, max;
	struct usb_device	*dev;
	struct usb_host_endpoint	*ep;
	int			is_out;

	if (!urb || urb->hcpriv || !urb->complete)
		return -EINVAL;

	if (!(dev = urb->dev) ||
	    (dev->state < USB_STATE_DEFAULT) ||
	    (!dev->bus) || (dev->devnum <= 0))
		return -ENODEV;

	/* For now, get the endpoint from the pipe.  Eventually drivers
	 * will be required to set urb->ep directly and we will eliminate
	 * urb->pipe.
	 */
	ep = usb_pipe_endpoint(dev, urb->pipe);
	if (!ep)
		return -ENOENT;

	urb->ep = ep;
	urb->status = -EINPROGRESS;
	urb->actual_length = 0;

	/* Lots of sanity checks, so HCDs can rely on clean data
	 * and don't need to duplicate tests
	 */
	pipe = urb->pipe;
	temp = usb_pipetype (pipe);
	is_out = usb_pipeout (pipe);

	if (!usb_pipecontrol (pipe) && dev->state < USB_STATE_CONFIGURED)
		return -ENODEV;

	/* FIXME there should be a sharable lock protecting us against
	 * config/altsetting changes and disconnects, kicking in here.
	 * (here == before maxpacket, and eventually endpoint type,
	 * checks get made.)
	 */

	max = usb_maxpacket (dev, pipe, is_out);
	if (max <= 0)
		return -EMSGSIZE;

	/* periodic transfers limit size per frame/uframe,
	 * but drivers only control those sizes for ISO.
	 * while we're checking, initialize return status.
	 */
	if (temp == PIPE_ISOCHRONOUS) {
		int	n, len;

		/* "high bandwidth" mode, 1-3 packets/uframe? */
		if (dev->speed == USB_SPEED_HIGH)
			max *= usb_endpoint_maxp_mult(&ep->desc);

		if (urb->number_of_packets <= 0)		    
			return -EINVAL;
		for (n = 0; n < urb->number_of_packets; n++) {
			len = urb->iso_frame_desc [n].length;
			if (len < 0 || len > max) 
				return -EMSGSIZE;
			urb->iso_frame_desc [n].status = -EXDEV;
			urb->iso_frame_desc [n].actual_length = 0;
		}
	}

	/* the I/O buffer must be mapped/unmapped, except when length=0 */
	if (urb->transfer_buffer_length < 0)
		return -EMSGSIZE;

	/*
	 * Force periodic transfer intervals to be legal values that are
	 * a power of two (so HCDs don't need to).
	 *
	 * FIXME want bus->{intr,iso}_sched_horizon values here.  Each HC
	 * supports different values... this uses EHCI/UHCI defaults (and
	 * EHCI can use smaller non-default values).
	 */
	switch (temp) {
	case PIPE_ISOCHRONOUS:
	case PIPE_INTERRUPT:
		/* too small? */
		if (urb->interval <= 0)
			return -EINVAL;
		/* too big? */
		switch (dev->speed) {
		case USB_SPEED_HIGH:	/* units are microframes */
			// NOTE usb handles 2^15
			if (urb->interval > (1024 * 8))
				urb->interval = 1024 * 8;
			temp = 1024 * 8;
			break;
		case USB_SPEED_FULL:	/* units are frames/msec */
		case USB_SPEED_LOW:
			if (temp == PIPE_INTERRUPT) {
				if (urb->interval > 255)
					return -EINVAL;
				// NOTE ohci only handles up to 32
				temp = 128;
			} else {
				if (urb->interval > 1024)
					urb->interval = 1024;
				// NOTE usb and ohci handle up to 2^15
				temp = 1024;
			}
			break;
		default:
			return -EINVAL;
		}
		/* power of two? */
		while (temp > urb->interval)
			temp >>= 1;
		urb->interval = temp;
	}

	return usb_hcd_submit_urb (urb);
}

/*-------------------------------------------------------------------*/

/**
 * usb_unlink_urb - abort/cancel a transfer request for an endpoint
 * @urb: pointer to urb describing a previously submitted request,
 *	may be NULL
 *
 * This routine cancels an in-progress request.  URBs complete only once
 * per submission, and may be canceled only once per submission.
 * Successful cancellation means termination of @urb will be expedited
 * and the completion handler will be called with a status code
 * indicating that the request has been canceled (rather than any other
 * code).
 *
 * This request is always asynchronous.  Success is indicated by
 * returning -EINPROGRESS, at which time the URB will probably not yet
 * have been given back to the device driver.  When it is eventually
 * called, the completion function will see @urb->status == -ECONNRESET.
 * Failure is indicated by usb_unlink_urb() returning any other value.
 * Unlinking will fail when @urb is not currently "linked" (i.e., it was
 * never submitted, or it was unlinked before, or the hardware is already
 * finished with it), even if the completion handler has not yet run.
 *
 * Unlinking and Endpoint Queues:
 *
 * [The behaviors and guarantees described below do not apply to virtual
 * root hubs but only to endpoint queues for physical USB devices.]
 *
 * Host Controller Drivers (HCDs) place all the URBs for a particular
 * endpoint in a queue.  Normally the queue advances as the controller
 * hardware processes each request.  But when an URB terminates with an
 * error its queue generally stops (see below), at least until that URB's
 * completion routine returns.  It is guaranteed that a stopped queue
 * will not restart until all its unlinked URBs have been fully retired,
 * with their completion routines run, even if that's not until some time
 * after the original completion handler returns.  The same behavior and
 * guarantee apply when an URB terminates because it was unlinked.
 *
 * Bulk and interrupt endpoint queues are guaranteed to stop whenever an
 * URB terminates with any sort of error, including -ECONNRESET, -ENOENT,
 * and -EREMOTEIO.  Control endpoint queues behave the same way except
 * that they are not guaranteed to stop for -EREMOTEIO errors.  Queues
 * for isochronous endpoints are treated differently, because they must
 * advance at fixed rates.  Such queues do not stop when an URB
 * encounters an error or is unlinked.  An unlinked isochronous URB may
 * leave a gap in the stream of packets; it is undefined whether such
 * gaps can be filled in.
 *
 * Note that early termination of an URB because a short packet was
 * received will generate a -EREMOTEIO error if and only if the
 * URB_SHORT_NOT_OK flag is set.  By setting this flag, USB device
 * drivers can build deep queues for large or complex bulk transfers
 * and clean them up reliably after any sort of aborted transfer by
 * unlinking all pending URBs at the first fault.
 *
 * When a control URB terminates with an error other than -EREMOTEIO, it
 * is quite likely that the status stage of the transfer will not take
 * place.
 */
int usb_unlink_urb(struct urb *urb)
{
	if (!urb)
		return -EINVAL;
	if (!urb->dev)
		return -ENODEV;
	if (!urb->ep)
		return -EIDRM;
	return usb_hcd_unlink_urb(urb, -ECONNRESET);
}

/**
 * usb_kill_urb - cancel a transfer request and wait for it to finish
 * @urb: pointer to URB describing a previously submitted request,
 *	may be NULL
 *
 * This routine cancels an in-progress request.  It is guaranteed that
 * upon return all completion handlers will have finished and the URB
 * will be totally idle and available for reuse.  These features make
 * this an ideal way to stop I/O in a disconnect() callback or close()
 * function.  If the request has not already finished or been unlinked
 * the completion handler will see urb->status == -ENOENT.
 *
 * While the routine is running, attempts to resubmit the URB will fail
 * with error -EPERM.  Thus even if the URB's completion handler always
 * tries to resubmit, it will not succeed and the URB will become idle.
 *
 * This routine may not be used in an interrupt context (such as a bottom
 * half or a completion handler), or when holding a spinlock, or in other
 * situations where the caller can't schedule().
 */
void usb_kill_urb(struct urb *urb)
{
	if (!(urb && urb->dev && urb->ep))
		return;
	usb_spin_lock_irq(&urb->lock);
	++urb->reject;
	usb_spin_unlock_irq(&urb->lock);

	usb_hcd_unlink_urb(urb, -ENOENT);

	while (urb->use_count)
		thread_waiter_wait(&usb_kill_urb_queue);	

	usb_spin_lock_irq(&urb->lock);
	--urb->reject;
	usb_spin_unlock_irq(&urb->lock);
}
