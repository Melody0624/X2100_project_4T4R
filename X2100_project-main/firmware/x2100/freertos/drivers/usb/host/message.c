#include <ctype.h>

#include "hcd.h"	/* for usbcore internals */
#include "usb.h"

void usb_api_blocking_completion(struct urb *urb)
{
	thread_waiter_wakeup((thread_waiter_t *)urb->context);
}

// Starts urb and waits for completion or timeout
// note that this call is NOT interruptible, while
// many device driver i/o requests should be interruptible
int usb_start_wait_urb(struct urb *urb, int timeout, int* actual_length)
{ 
	thread_waiter_t	done;
	uint32_t timeout_ms;
	int			status;

	thread_waiter_init(&done); 	
	urb->context = &done;
	urb->actual_length = 0;
	status = usb_submit_urb(urb);

	if (status == 0) {
		if (timeout > 0)
			timeout_ms = timeout;
		else
			timeout_ms = THREAD_COND_TIMEOUT_NO_LIMIT_MS;

		if(thread_waiter_wait_timeout(&done, timeout_ms)) {
			usb_unlink_urb(urb);
			thread_waiter_wait(&done);
		}

		status = urb->status;
		/* note:  HCDs return ETIMEDOUT for other reasons too */
		if (status == -ECONNRESET) {
			if (urb->actual_length > 0)
				status = 0;
			else
				status = -ETIMEDOUT;
		}
	}

	if (actual_length)
		*actual_length = urb->actual_length;
	usb_free_urb(urb);
	return status;
}

/*-------------------------------------------------------------------*/
// returns status (negative) or length (positive)
static int usb_internal_control_msg(struct usb_device *usb_dev,
				    unsigned int pipe, 
				    struct usb_ctrlrequest *cmd,
				    void *data, int len, int timeout)
{
	struct urb *urb;
	int retv;
	int length;

	urb = usb_alloc_urb(0);
	if (!urb)
		return -ENOMEM;
  
	usb_fill_control_urb(urb, usb_dev, pipe, (unsigned char *)cmd, data,
			     len, usb_api_blocking_completion, NULL);

	retv = usb_start_wait_urb(urb, timeout, &length);
	if (retv < 0)
		return retv;
	else
		return length;
}

/**
 *	usb_control_msg - Builds a control urb, sends it off and waits for completion
 *	@dev: pointer to the usb device to send the message to
 *	@pipe: endpoint "pipe" to send the message to
 *	@request: USB message request value
 *	@requesttype: USB message request type value
 *	@value: USB message value
 *	@index: USB message index value
 *	@data: pointer to the data to send
 *	@size: length in bytes of the data to send
 *	@timeout: time in msecs to wait for the message to complete before
 *		timing out (if 0 the wait is forever)
 *	Context: !in_interrupt ()
 *
 *	This function sends a simple control message to a specified endpoint
 *	and waits for the message to complete, or timeout.
 *	
 *	If successful, it returns the number of bytes transferred, otherwise a negative error number.
 *
 *	Don't use this function from within an interrupt context, like a
 *	bottom half handler.  If you need an asynchronous message, or need to send
 *	a message from within interrupt context, use usb_submit_urb()
 *      If a thread in your driver uses this call, make sure your disconnect()
 *      method can wait for it to complete.  Since you don't have a handle on
 *      the URB used, you can't cancel the request.
 */
int usb_control_msg(struct usb_device *dev, unsigned int pipe, u8 request, u8 requesttype,
			 u16 value, u16 index, void *data, u16 size, int timeout)
{
	struct usb_ctrlrequest *dr;
	int ret;

	dr = cache_align_malloc(sizeof(struct usb_ctrlrequest));
	if (!dr)
		return -ENOMEM;

	dr->bRequestType= requesttype;
	dr->bRequest = request;
	dr->wValue = value;
	dr->wIndex = index;
	dr->wLength = size;

	ret = usb_internal_control_msg(dev, pipe, dr, data, size, timeout);

	free(dr);

	return ret;
}

/**
 * usb_control_msg_send - Builds a control "send" message, sends it off and waits for completion
 * @dev: pointer to the usb device to send the message to
 * @endpoint: endpoint to send the message to
 * @request: USB message request value
 * @requesttype: USB message request type value
 * @value: USB message value
 * @index: USB message index value
 * @driver_data: pointer to the data to send
 * @size: length in bytes of the data to send
 * @timeout: time in msecs to wait for the message to complete before timing
 *	out (if 0 the wait is forever)
 * @memflags: the flags for memory allocation for buffers
 *
 * Context: !in_interrupt ()
 *
 * This function sends a control message to a specified endpoint that is not
 * expected to fill in a response (i.e. a "send message") and waits for the
 * message to complete, or timeout.
 *
 * Do not use this function from within an interrupt context. If you need
 * an asynchronous message, or need to send a message from within interrupt
 * context, use usb_submit_urb(). If a thread in your driver uses this call,
 * make sure your disconnect() method can wait for it to complete. Since you
 * don't have a handle on the URB used, you can't cancel the request.
 *
 * The data pointer can be made to a reference on the stack, or anywhere else,
 * as it will not be modified at all.  This does not have the restriction that
 * usb_control_msg() has where the data pointer must be to dynamically allocated
 * memory (i.e. memory that can be successfully DMAed to a device).
 *
 * Return: If successful, 0 is returned, Otherwise, a negative error number.
 */
int usb_control_msg_send(struct usb_device *dev, u8 endpoint, u8 request,
			 u8 requesttype, u16 value, u16 index,
			 const void *driver_data, u16 size, int timeout)
{
	unsigned int pipe = usb_sndctrlpipe(dev, endpoint);
	int ret;
	u8 *data = NULL;

	if (size) {
		data = cache_align_malloc(size);
		if (!data)
			return -ENOMEM;
		memcpy(data, driver_data, size);
	}

	ret = usb_control_msg(dev, pipe, request, requesttype, value, index,
			      data, size, timeout);
	free(data);

	if (ret < 0)
		return ret;

	return 0;
}

/**
 * usb_control_msg_recv - Builds a control "receive" message, sends it off and waits for completion
 * @dev: pointer to the usb device to send the message to
 * @endpoint: endpoint to send the message to
 * @request: USB message request value
 * @requesttype: USB message request type value
 * @value: USB message value
 * @index: USB message index value
 * @driver_data: pointer to the data to be filled in by the message
 * @size: length in bytes of the data to be received
 * @timeout: time in msecs to wait for the message to complete before timing
 *	out (if 0 the wait is forever)
 * @memflags: the flags for memory allocation for buffers
 *
 * Context: !in_interrupt ()
 *
 * This function sends a control message to a specified endpoint that is
 * expected to fill in a response (i.e. a "receive message") and waits for the
 * message to complete, or timeout.
 *
 * Do not use this function from within an interrupt context. If you need
 * an asynchronous message, or need to send a message from within interrupt
 * context, use usb_submit_urb(). If a thread in your driver uses this call,
 * make sure your disconnect() method can wait for it to complete. Since you
 * don't have a handle on the URB used, you can't cancel the request.
 *
 * The data pointer can be made to a reference on the stack, or anywhere else
 * that can be successfully written to.  This function does not have the
 * restriction that usb_control_msg() has where the data pointer must be to
 * dynamically allocated memory (i.e. memory that can be successfully DMAed to a
 * device).
 *
 * The "whole" message must be properly received from the device in order for
 * this function to be successful.  If a device returns less than the expected
 * amount of data, then the function will fail.  Do not use this for messages
 * where a variable amount of data might be returned.
 *
 * Return: If successful, 0 is returned, Otherwise, a negative error number.
 */
int usb_control_msg_recv(struct usb_device *dev, u8 endpoint, u8 request,
			 u8 requesttype, u16 value, u16 index,
			 void *driver_data, u16 size, int timeout)
{
	unsigned int pipe = usb_rcvctrlpipe(dev, endpoint);
	int ret;
	u8 *data;

	if (!size || !driver_data)
		return -EINVAL;

	data = cache_align_malloc(size);
	if (!data)
		return -ENOMEM;

	ret = usb_control_msg(dev, pipe, request, requesttype, value, index,
			      data, size, timeout);

	if (ret < 0)
		goto exit;

	if (ret == size) {
		memcpy(driver_data, data, size);
		ret = 0;
	} else {
		ret = -EIO;
	}

exit:
	free(data);
	return ret;
}

/**
 * usb_interrupt_msg - Builds an interrupt urb, sends it off and waits for completion
 * @usb_dev: pointer to the usb device to send the message to
 * @pipe: endpoint "pipe" to send the message to
 * @data: pointer to the data to send
 * @len: length in bytes of the data to send
 * @actual_length: pointer to a location to put the actual length transferred
 *	in bytes
 * @timeout: time in msecs to wait for the message to complete before
 *	timing out (if 0 the wait is forever)
 *
 * Context: task context, might sleep.
 *
 * This function sends a simple interrupt message to a specified endpoint and
 * waits for the message to complete, or timeout.
 *
 * Don't use this function from within an interrupt context. If you need
 * an asynchronous message, or need to send a message from within interrupt
 * context, use usb_submit_urb() If a thread in your driver uses this call,
 * make sure your disconnect() method can wait for it to complete. Since you
 * don't have a handle on the URB used, you can't cancel the request.
 *
 * Return:
 * If successful, 0. Otherwise a negative error number. The number of actual
 * bytes transferred will be stored in the @actual_length parameter.
 */
int usb_interrupt_msg(struct usb_device *usb_dev, unsigned int pipe,
		      void *data, int len, int *actual_length, int timeout)
{
	return usb_bulk_msg(usb_dev, pipe, data, len, actual_length, timeout);
}

/**
 * usb_bulk_msg - Builds a bulk urb, sends it off and waits for completion
 * @usb_dev: pointer to the usb device to send the message to
 * @pipe: endpoint "pipe" to send the message to
 * @data: pointer to the data to send
 * @len: length in bytes of the data to send
 * @actual_length: pointer to a location to put the actual length transferred
 *	in bytes
 * @timeout: time in msecs to wait for the message to complete before
 *	timing out (if 0 the wait is forever)
 *
 * Context: task context, might sleep.
 *
 * This function sends a simple bulk message to a specified endpoint
 * and waits for the message to complete, or timeout.
 *
 * Don't use this function from within an interrupt context. If you need
 * an asynchronous message, or need to send a message from within interrupt
 * context, use usb_submit_urb() If a thread in your driver uses this call,
 * make sure your disconnect() method can wait for it to complete. Since you
 * don't have a handle on the URB used, you can't cancel the request.
 *
 * Because there is no usb_interrupt_msg() and no USBDEVFS_INTERRUPT ioctl,
 * users are forced to abuse this routine by using it to submit URBs for
 * interrupt endpoints.  We will take the liberty of creating an interrupt URB
 * (with the default interval) if the target is an interrupt endpoint.
 *
 * Return:
 * If successful, 0. Otherwise a negative error number. The number of actual
 * bytes transferred will be stored in the @actual_length parameter.
 *
 */
int usb_bulk_msg(struct usb_device *usb_dev, unsigned int pipe,
		 void *data, int len, int *actual_length, int timeout)
{
	struct urb *urb;
	struct usb_host_endpoint *ep;

	ep = usb_pipe_endpoint(usb_dev, pipe);
	if (!ep || len < 0)
		return -EINVAL;

	urb = usb_alloc_urb(0);
	if (!urb)
		return -ENOMEM;

	if ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
			USB_ENDPOINT_XFER_INT) {
		pipe = (pipe & ~(3 << 30)) | (PIPE_INTERRUPT << 30);
		usb_fill_int_urb(urb, usb_dev, pipe, data, len,
				usb_api_blocking_completion, NULL,
				ep->desc.bInterval);
	} else
		usb_fill_bulk_urb(urb, usb_dev, pipe, data, len,
				usb_api_blocking_completion, NULL);

	return usb_start_wait_urb(urb, timeout, actual_length);
}

/*-------------------------------------------------------------------*/

/**
 * usb_get_descriptor - issues a generic GET_DESCRIPTOR request
 * @dev: the device whose descriptor is being retrieved
 * @type: the descriptor type (USB_DT_*)
 * @index: the number of the descriptor
 * @buf: where to put the descriptor
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Gets a USB descriptor.  Convenience functions exist to simplify
 * getting some types of descriptors.  Use
 * usb_get_string() or usb_string() for USB_DT_STRING.
 * Device (USB_DT_DEVICE) and configuration descriptors (USB_DT_CONFIG)
 * are part of the device structure.
 * In addition to a number of USB-standard descriptors, some
 * devices also use class-specific or vendor-specific descriptors.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_descriptor(struct usb_device *dev, unsigned char type, unsigned char index, void *buf, int size)
{
	int i;
	int result;
	
	memset(buf,0,size);	// Make sure we parse really received data

	for (i = 0; i < 3; ++i) {
		/* retry on length 0 or stall; some devices are flakey */
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
				(type << 8) + index, 0, buf, size,
				USB_CTRL_GET_TIMEOUT);
		if (result == 0 || result == -EPIPE)
			continue;
		if (result > 1 && ((u8 *)buf)[1] != type) {
			result = -EPROTO;
			continue;
		}
		break;
	}
	return result;
}

/**
 * usb_get_string - gets a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @langid: code for language chosen (from string descriptor zero)
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 *
 * Retrieves a string, encoded using UTF-16LE (Unicode, 16 bits per character,
 * in little-endian byte order).
 * The usb_string() function will often be a convenient way to turn
 * these strings into kernel-printable form.
 *
 * Strings may be referenced in device, configuration, interface, or other
 * descriptors, and could also be used in vendor-specific ways.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_string(struct usb_device *dev, unsigned short langid,
		unsigned char index, void *buf, int size)
{
	int i;
	int result;

	for (i = 0; i < 3; ++i) {
		/* retry on length 0 or stall; some devices are flakey */
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
			(USB_DT_STRING << 8) + index, langid, buf, size,
			USB_CTRL_GET_TIMEOUT);
		if (!(result == 0 || result == -EPIPE))
			break;
	}
	return result;
}

static void usb_try_string_workarounds(unsigned char *buf, int *length)
{
	int newlength, oldlength = *length;

	for (newlength = 2; newlength + 1 < oldlength; newlength += 2)
		if (!isprint(buf[newlength]) || buf[newlength + 1])
			break;

	if (newlength > 2) {
		buf[0] = newlength;
		*length = newlength;
	}
}

static int usb_string_sub(struct usb_device *dev, unsigned int langid,
		unsigned int index, unsigned char *buf)
{
	int rc;

	/* Try to read the string descriptor by asking for the maximum
	 * possible number of bytes */
	rc = usb_get_string(dev, langid, index, buf, 255);

	/* If that failed try to read the descriptor length, then
	 * ask for just that many bytes */
	if (rc < 2) {
		rc = usb_get_string(dev, langid, index, buf, 2);
		if (rc == 2)
			rc = usb_get_string(dev, langid, index, buf, buf[0]);
	}

	if (rc >= 2) {
		if (!buf[0] && !buf[1])
			usb_try_string_workarounds(buf, &rc);

		/* There might be extra junk at the end of the descriptor */
		if (buf[0] < rc)
			rc = buf[0];

		rc = rc - (rc & 1); /* force a multiple of two */
	}

	if (rc < 2)
		rc = (rc < 0 ? rc : -EINVAL);

	return rc;
}

/**
 * usb_string - returns ISO 8859-1 version of a string descriptor
 * @dev: the device whose string descriptor is being retrieved
 * @index: the number of the descriptor
 * @buf: where to put the string
 * @size: how big is "buf"?
 * Context: !in_interrupt ()
 * 
 * This converts the UTF-16LE encoded strings returned by devices, from
 * usb_get_string_descriptor(), to null-terminated ISO-8859-1 encoded ones
 * that are more usable in most kernel contexts.  Note that all characters
 * in the chosen descriptor that can't be encoded using ISO-8859-1
 * are converted to the question mark ("?") character, and this function
 * chooses strings in the first language supported by the device.
 *
 * The ASCII (or, redundantly, "US-ASCII") character set is the seven-bit
 * subset of ISO 8859-1. ISO-8859-1 is the eight-bit subset of Unicode,
 * and is appropriate for use many uses of English and several other
 * Western European languages.  (But it doesn't include the "Euro" symbol.)
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns length of the string (>= 0) or usb_control_msg status (< 0).
 */
int usb_string(struct usb_device *dev, int index, char *buf, size_t size)
{
	unsigned char *tbuf;
	int err;
	unsigned int u, idx;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;
	if (size <= 0 || !buf || !index)
		return -EINVAL;
	buf[0] = 0;
	tbuf = cache_align_malloc(256);
	if (!tbuf)
		return -ENOMEM;

	/* get langid for strings if it's not yet known */
	if (!dev->have_langid) {
		err = usb_string_sub(dev, 0, 0, tbuf);
		if (err < 0) {
			printf("string descriptor 0 read error: %d\n", err);
			goto errout;
		} else if (err < 4) {
			printf("string descriptor 0 too short\n");
			err = -EINVAL;
			goto errout;
		} else {
			dev->have_langid = -1;
			dev->string_langid = tbuf[2] | (tbuf[3]<< 8);
		}
	}
	
	err = usb_string_sub(dev, dev->string_langid, index, tbuf);
	if (err < 0)
		goto errout;

	size--;		/* leave room for trailing NULL char in output buffer */
	for (idx = 0, u = 2; u < err; u += 2) {
		if (idx >= size)
			break;
		if (tbuf[u+1])			/* high byte */
			buf[idx++] = '?';  /* non ISO-8859-1 character */
		else
			buf[idx++] = tbuf[u];
	}
	buf[idx] = 0;
	err = idx;

 errout:
	free(tbuf);
	return err;
}

/*
 * usb_get_device_descriptor - (re)reads the device descriptor (usbcore)
 * @dev: the device whose device descriptor is being updated
 * @size: how much of the descriptor to read
 * Context: !in_interrupt ()
 *
 * Updates the copy of the device descriptor stored in the device structure,
 * which dedicates space for this purpose.  Note that several fields are
 * converted to the host CPU's byte order:  the USB version (bcdUSB), and
 * vendors product and version fields (idVendor, idProduct, and bcdDevice).
 * That lets device drivers compare against non-byteswapped constants.
 *
 * Not exported, only for use by the core.  If drivers really want to read
 * the device descriptor directly, they can call usb_get_descriptor() with
 * type = USB_DT_DEVICE and index = 0.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_device_descriptor(struct usb_device *dev, unsigned int size)
{
	struct usb_device_descriptor *desc;
	int ret;

	if (size > sizeof(*desc))
		return -EINVAL;
	desc = cache_align_malloc(sizeof(*desc));
	if (!desc)
		return -ENOMEM;

	ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, desc, size);
	if (ret >= 0) 
		memcpy(&dev->descriptor, desc, size);
	free(desc);
	return ret;
}

/**
 * usb_get_status - issues a GET_STATUS call
 * @dev: the device whose status is being checked
 * @type: USB_RECIP_*; for device, interface, or endpoint
 * @target: zero (for device), else interface or endpoint number
 * @data: pointer to two bytes of bitmap data
 * Context: !in_interrupt ()
 *
 * Returns device, interface, or endpoint status.  Normally only of
 * interest to see if the device is self powered, or has enabled the
 * remote wakeup facility; or whether a bulk or interrupt endpoint
 * is halted ("stalled").
 *
 * Bits in these status bitmaps are set using the SET_FEATURE request,
 * and cleared using the CLEAR_FEATURE request.  The usb_clear_halt()
 * function should be used to clear halt ("stall") status.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns the number of bytes received on success, or else the status code
 * returned by the underlying usb_control_msg() call.
 */
int usb_get_status(struct usb_device *dev, int type, int target, void *data)
{
	int ret;
	u16 *status = cache_align_malloc(sizeof(*status));

	if (!status)
		return -ENOMEM;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		USB_REQ_GET_STATUS, USB_DIR_IN | type, 0, target, status,
		sizeof(*status), USB_CTRL_GET_TIMEOUT);

	*(u16 *)data = *status;
	free(status);
	return ret;
}

/**
 * usb_disable_endpoint -- Disable an endpoint by address
 * @dev: the device whose endpoint is being disabled
 * @epaddr: the endpoint's address.  Endpoint number for output,
 *	endpoint number + USB_DIR_IN for input
 *
 * Deallocates hcd/hardware state for this endpoint ... and nukes all
 * pending urbs.
 *
 * If the HCD hasn't registered a disable() function, this sets the
 * endpoint's maxpacket size to 0 to prevent further submissions.
 */
void usb_disable_endpoint(struct usb_device *dev, unsigned int epaddr)
{
	unsigned int epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	struct usb_host_endpoint *ep;

	if (!dev)
		return;

	if (usb_endpoint_out(epaddr)) {
		ep = dev->ep_out[epnum];
		dev->ep_out[epnum] = NULL;
	} else {
		ep = dev->ep_in[epnum];
		dev->ep_in[epnum] = NULL;
	}

	if (ep && dev->bus) {
		ep->enabled = 0;
		usb_hcd_disable_endpoint(dev, ep);
	}
}

/**
 * usb_reset_endpoint - Reset an endpoint's state.
 * @dev: the device whose endpoint is to be reset
 * @epaddr: the endpoint's address.  Endpoint number for output,
 *	endpoint number + USB_DIR_IN for input
 *
 * Resets any host-side endpoint state such as the toggle bit,
 * sequence number or current window.
 */
void usb_reset_endpoint(struct usb_device *dev, unsigned int epaddr)
{
	unsigned int epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	struct usb_host_endpoint *ep;

	if (usb_endpoint_out(epaddr))
		ep = dev->ep_out[epnum];
	else
		ep = dev->ep_in[epnum];
	if (ep)
		usb_hcd_reset_endpoint(dev, ep);
}

/**
 * usb_clear_halt - tells device to clear endpoint halt/stall condition
 * @dev: device whose endpoint is halted
 * @pipe: endpoint "pipe" being cleared
 * Context: !in_interrupt ()
 *
 * This is used to clear halt conditions for bulk and interrupt endpoints,
 * as reported by URB completion status.  Endpoints that are halted are
 * sometimes referred to as being "stalled".  Such endpoints are unable
 * to transmit or receive data until the halt status is cleared.  Any URBs
 * queued for such an endpoint should normally be unlinked by the driver
 * before clearing the halt condition, as described in sections 5.7.5
 * and 5.8.5 of the USB 2.0 spec.
 *
 * Note that control and isochronous endpoints don't halt, although control
 * endpoints report "protocol stall" (for unsupported requests) using the
 * same status code used to report a true stall.
 *
 * This call is synchronous, and may not be used in an interrupt context.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying usb_control_msg() call.
 */
int usb_clear_halt(struct usb_device *dev, int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);
	
	if (usb_pipein (pipe))
		endp |= USB_DIR_IN;

	/* we don't care if it wasn't halted first. in fact some devices
	 * (like some ibmcam model 1 units) seem to expect hosts to make
	 * this request for iso endpoints, which can't halt!
	 */
	result = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
		USB_ENDPOINT_HALT, endp, NULL, 0,
		USB_CTRL_SET_TIMEOUT);

	/* don't un-halt or force to DATA0 except on success */
	if (result)
		return result;

	/* NOTE:  seems like Microsoft and Apple don't bother verifying
	 * the clear "took", so some devices could lock up if you check...
	 * such as the Hagiwara FlashGate DUAL.  So we won't bother.
	 *
	 * NOTE:  make sure the logic here doesn't diverge much from
	 * the copy in usb-storage, for as long as we need two copies.
	 */

	usb_reset_endpoint(dev, endp);

	return 0;
}

void release_interface(struct usb_interface *intf)
{
	struct usb_interface_cache *intfc =
			altsetting_to_usb_interface_cache(intf->altsetting);

	intfc->ref--;

	if (intf->ref)
		panic("ERROR: usb interface reference counter non-zero %d\n", intf->ref);

	free(intf);
}

/**
 * usb_disable_interface -- Disable all endpoints for an interface
 * @dev: the device whose interface is being disabled
 * @intf: pointer to the interface descriptor
 *
 * Disables all the endpoints for the interface's current altsetting.
 */
void usb_disable_interface(struct usb_device *dev, struct usb_interface *intf)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	for (i = 0; i < alt->desc.bNumEndpoints; ++i) {
		usb_disable_endpoint(dev,
				alt->endpoint[i].desc.bEndpointAddress);
	}
}

/*
 * usb_disable_device - Disable all the endpoints for a USB device
 * @dev: the device whose endpoints are being disabled
 * @skip_ep0: 0 to disable endpoint 0, 1 to skip it.
 *
 * Disables all the device's endpoints, potentially including endpoint 0.
 * Deallocates hcd/hardware state for the endpoints (nuking all or most
 * pending urbs) and usbcore state for the interfaces, so that usbcore
 * must usb_set_configuration() before any interfaces could be used.
 */
void usb_disable_device(struct usb_device *dev, int skip_ep0)
{
	int i;

	for (i = skip_ep0; i < 16; ++i) {
		usb_disable_endpoint(dev, i);
		usb_disable_endpoint(dev, i + USB_DIR_IN);
	}

	/* getting rid of interfaces will disconnect
	 * any drivers bound to them (a key side effect)
	 */
	if (dev->actconfig) {
		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			struct usb_interface	*interface;

			/* remove this interface */
			interface = dev->actconfig->interface[i];
			free(interface->cur_altsetting->string);
			interface->cur_altsetting->string = NULL;

			if (interface->drvwrap) {
				assert(interface->drvwrap->interface_remove);
				interface->drvwrap->interface_remove(interface, interface->drvwrap->private_data);
				interface->drvwrap = NULL;
			}
		}

		/* Now that the interfaces are unbound, nobody should
		 * try to access them.
		 */
		for (i = 0; i < dev->actconfig->desc.bNumInterfaces; i++) {
			release_interface(dev->actconfig->interface[i]);
			dev->actconfig->interface[i] = NULL;
		}
		dev->actconfig = NULL;
		if (dev->state == USB_STATE_CONFIGURED)
			usb_set_device_state(dev, USB_STATE_ADDRESS);
	}
}


/*
 * usb_enable_endpoint - Enable an endpoint for USB communications
 * @dev: the device whose interface is being enabled
 * @ep: the endpoint
 *
 * Resets the endpoint, and sets dev->ep_{in,out} pointers.
 * For control endpoints, both the input and output sides are handled.
 */
static void
usb_enable_endpoint(struct usb_device *dev, struct usb_host_endpoint *ep)
{
	unsigned int epaddr = ep->desc.bEndpointAddress;
	unsigned int epnum = epaddr & USB_ENDPOINT_NUMBER_MASK;
	int is_control;

	is_control = ((ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
			== USB_ENDPOINT_XFER_CONTROL);
	if (usb_endpoint_out(epaddr) || is_control)
		dev->ep_out[epnum] = ep;
	if (!usb_endpoint_out(epaddr) || is_control)
		dev->ep_in[epnum] = ep;
	ep->enabled = 1;
}

/*
 * usb_enable_interface - Enable all the endpoints for an interface
 * @dev: the device whose interface is being enabled
 * @intf: pointer to the interface descriptor
 *
 * Enables all the endpoints for the interface's current altsetting.
 */
static void usb_enable_interface(struct usb_device *dev,
				 struct usb_interface *intf)
{
	struct usb_host_interface *alt = intf->cur_altsetting;
	int i;

	for (i = 0; i < alt->desc.bNumEndpoints; ++i)
		usb_enable_endpoint(dev, &alt->endpoint[i]);
}

int usb_set_interface(struct usb_device *dev, int interface, int alternate)
{
	struct usb_interface *iface;
	struct usb_host_interface *alt;
	int ret;
	int manual = 0;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	iface = usb_ifnum_to_if(dev, interface);
	if (!iface) {
		return -EINVAL;
	}

	alt = usb_altnum_to_altsetting(iface, alternate);
	if (!alt) {
		printf("devnum %d: selecting invalid altsetting %d", dev->devnum, alternate);
		return -EINVAL;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
				   USB_REQ_SET_INTERFACE, USB_RECIP_INTERFACE,
				   alternate, interface, NULL, 0, 5000);

	/* 9.4.10 says devices don't need this and are free to STALL the
	 * request if the interface only has one alternate setting.
	 */
	if (ret == -EPIPE && iface->num_altsetting == 1) {
		manual = 1;
	} else if (ret < 0)
		return ret;

	/* FIXME drivers shouldn't need to replicate/bugfix the logic here
	 * when they implement async or easily-killable versions of this or
	 * other "should-be-internal" functions (like clear_halt).
	 * should hcd+usbcore postprocess control requests?
	 */

	/* prevent submissions using previous endpoint settings */
	usb_disable_interface(dev, iface);

	if (iface->cur_altsetting == alt)
		manual = 1;
	iface->cur_altsetting = alt;

	/* If the interface only has one altsetting and the device didn't
	 * accept the request (or whenever the old altsetting is the same
	 * as the new one), we attempt to carry out the equivalent action
	 * by manually clearing the HALT feature for each endpoint in the
	 * new altsetting.
	 */
	if (manual) {
		int i;

		for (i = 0; i < alt->desc.bNumEndpoints; i++) {
			unsigned int epaddr =
				alt->endpoint[i].desc.bEndpointAddress;
			unsigned int pipe =
	__create_pipe(dev, USB_ENDPOINT_NUMBER_MASK & epaddr)
	| (usb_endpoint_out(epaddr) ? USB_DIR_OUT : USB_DIR_IN);

			usb_clear_halt(dev, pipe);
		}
	}

	usb_enable_interface(dev, iface);

	return 0;
}

/**
 * usb_reset_configuration - lightweight device reset
 * @dev: the device whose configuration is being reset
 *
 * This issues a standard SET_CONFIGURATION request to the device using
 * the current configuration.  The effect is to reset most USB-related
 * state in the device, including interface altsettings (reset to zero),
 * endpoint halts (cleared), and data (only for bulk and interrupt
 * endpoints).  Other usbcore state is unchanged, including bindings of
 * usb device drivers to interfaces.
 *
 * Because this affects multiple interfaces, avoid using this with composite
 * (multi-interface) devices.  Instead, the driver for each interface may
 * use usb_set_interface() on the interfaces it claims.  Resetting the whole
 * configuration would affect other drivers' interfaces.
 *
 * The caller must own the device lock.
 *
 * Returns zero on success, else a negative error code.
 */
int usb_reset_configuration(struct usb_device *dev)
{
	int			i, retval;
	struct usb_host_config	*config;

	if (dev->state == USB_STATE_SUSPENDED)
		return -EHOSTUNREACH;

	/* caller must have locked the device and must own
	 * the usb bus readlock (so driver bindings are stable);
	 * calls during probe() are fine
	 */

	for (i = 1; i < 16; ++i) {
		usb_disable_endpoint(dev, i);
		usb_disable_endpoint(dev, i + USB_DIR_IN);
	}

	config = dev->actconfig;
	retval = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0,
			config->desc.bConfigurationValue, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (retval < 0) {
		usb_set_device_state(dev, USB_STATE_ADDRESS);
		return retval;
	}

	/* re-init hc/hcd interface/endpoint state */
	for (i = 0; i < config->desc.bNumInterfaces; i++) {
		struct usb_interface *intf = config->interface[i];
		struct usb_host_interface *alt;

		alt = usb_altnum_to_altsetting(intf, 0);

		/* No altsetting 0?  We'll assume the first altsetting.
		 * We could use a GetInterface call, but if a device is
		 * so non-compliant that it doesn't have altsetting 0
		 * then I wouldn't trust its reply anyway.
		 */
		if (!alt)
			alt = &intf->altsetting[0];

		intf->cur_altsetting = alt;
		usb_enable_interface(dev, intf);
	}
	return 0;
}

static struct usb_interface_assoc_descriptor *find_iad(struct usb_device *dev,
						       struct usb_host_config *config,
						       u8 inum)
{
	struct usb_interface_assoc_descriptor *retval = NULL;
	struct usb_interface_assoc_descriptor *intf_assoc;
	int first_intf;
	int last_intf;
	int i;

	for (i = 0; (i < USB_MAXIADS && config->intf_assoc[i]); i++) {
		intf_assoc = config->intf_assoc[i];
		if (intf_assoc->bInterfaceCount == 0)
			continue;

		first_intf = intf_assoc->bFirstInterface;
		last_intf = first_intf + (intf_assoc->bInterfaceCount - 1);
		if (inum >= first_intf && inum <= last_intf) {
			if (!retval)
				retval = intf_assoc;
			else
				printf("devnum %d: Interface #%d referenced"
					" by multiple IADs\n", dev->devnum, inum);
		}
	}

	return retval;
}

/*
 * usb_set_configuration - Makes a particular device setting be current
 * @dev: the device whose configuration is being updated
 * @configuration: the configuration being chosen.
 * Context: !in_interrupt(), caller owns the device lock
 *
 * This is used to enable non-default device modes.  Not all devices
 * use this kind of configurability; many devices only have one
 * configuration.
 *
 * USB device configurations may affect Linux interoperability,
 * power consumption and the functionality available.  For example,
 * the default configuration is limited to using 100mA of bus power,
 * so that when certain device functionality requires more power,
 * and the device is bus powered, that functionality should be in some
 * non-default device configuration.  Other device modes may also be
 * reflected as configuration options, such as whether two ISDN
 * channels are available independently; and choosing between open
 * standard device protocols (like CDC) or proprietary ones.
 *
 * Note that USB has an additional level of device configurability,
 * associated with interfaces.  That configurability is accessed using
 * usb_set_interface().
 *
 * This call is synchronous. The calling context must be able to sleep,
 * must own the device lock, and must not hold the driver model's USB
 * bus rwsem; usb device driver probe() methods cannot use this routine.
 *
 * Returns zero on success, or else the status code returned by the
 * underlying call that failed.  On succesful completion, each interface
 * in the original device configuration has been destroyed, and each one
 * in the new configuration has been probed by all relevant usb device
 * drivers currently known to the kernel.
 */
int usb_set_configuration(struct usb_device *dev, int configuration)
{
	int i, ret;
	struct usb_host_config *cp = NULL;
	struct usb_interface **new_interfaces = NULL;
	int n, nintf;

	if (configuration == -1)
		configuration = 0;
	else {
		for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
			if (dev->config[i].desc.bConfigurationValue ==
					configuration) {
				cp = &dev->config[i];
				break;
			}
		}
	}
	if ((!cp && configuration != 0))
		return -EINVAL;

	/* The USB spec says configuration 0 means unconfigured.
	 * But if a device includes a configuration numbered 0,
	 * we will accept it as a correctly configured state.
	 */
	if (cp && configuration == 0)
		printf("devnum %d: config 0 descriptor??\n", dev->devnum);

	/* Allocate memory for new interfaces before doing anything else,
	 * so that if we run out then nothing will have changed. */
	n = nintf = 0;
	if (cp) {
		nintf = cp->desc.bNumInterfaces;
		new_interfaces = malloc(nintf * sizeof(*new_interfaces));
		if (!new_interfaces) {
			printf("%s: Out of memory", __func__);
			return -ENOMEM;
		}

		for (; n < nintf; ++n) {
			new_interfaces[n] = malloc(sizeof(struct usb_interface));
			if (!new_interfaces[n]) {
				printf("%s: Out of memory", __func__);
				ret = -ENOMEM;
free_interfaces:
				while (--n >= 0)
					free(new_interfaces[n]);
				free(new_interfaces);
				return ret;
			}
		}
	}

	/* if it's already configured, clear out old state first.
	 * getting rid of old interfaces means unbinding their drivers.
	 */
	if (dev->state != USB_STATE_ADDRESS)
		usb_disable_device (dev, 1);	// Skip ep0

	if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			USB_REQ_SET_CONFIGURATION, 0, configuration, 0,
			NULL, 0, USB_CTRL_SET_TIMEOUT)) < 0)
		goto free_interfaces;

	dev->actconfig = cp;
	if (!cp)
		usb_set_device_state(dev, USB_STATE_ADDRESS);
	else {
		usb_set_device_state(dev, USB_STATE_CONFIGURED);

		/* Initialize the new interface structures and the
		 * hc/hcd/usbcore interface/endpoint state.
		 */
		for (i = 0; i < nintf; ++i) {
			struct usb_interface_cache *intfc;
			struct usb_interface *intf;
			struct usb_host_interface *alt;

			cp->interface[i] = intf = new_interfaces[i];
			memset(intf, 0, sizeof(*intf));
			intfc = cp->intf_cache[i];
			intf->altsetting = intfc->altsetting;
			intf->num_altsetting = intfc->num_altsetting;
			intf->intf_assoc = find_iad(dev, cp, i);
			intfc->ref++;
			mutex_init(&intf->mutex);

			alt = usb_altnum_to_altsetting(intf, 0);

			/* No altsetting 0?  We'll assume the first altsetting.
			 * We could use a GetInterface call, but if a device is
			 * so non-compliant that it doesn't have altsetting 0
			 * then I wouldn't trust its reply anyway.
			 */
			if (!alt)
				alt = &intf->altsetting[0];

			intf->cur_altsetting = alt;
			intf->usb_dev = dev;
			usb_enable_interface(dev, intf);
		}
		free(new_interfaces);

		if ((cp->desc.iConfiguration) &&
		    (cp->string == NULL)) {
			cp->string = malloc(256);
			if (cp->string)
				usb_string(dev, cp->desc.iConfiguration, cp->string, 256);
		}

		/* Now that all the interfaces are set up, register them
		 * to trigger binding of drivers to interfaces.  probe()
		 * routines may install different altsettings and may
		 * claim() any interfaces not yet bound.  Many class drivers
		 * need that: CDC, audio, video, etc.
		 */
		for (i = 0; i < nintf; ++i) {
			struct usbdrv_wrap *drvwrap;
			struct usb_interface_descriptor *desc;
			struct usb_interface *intf = cp->interface[i];

			desc = &intf->altsetting [0].desc;
			printf ("devnum %d: adding (config #%d, interface %d)\n",
				dev->devnum, configuration, desc->bInterfaceNumber);

			if (!intf->drvwrap) {
				list_for_each_entry(drvwrap, &usb_driver_list, list) {
					if (usb_interface_match(intf, drvwrap)) {
						assert(drvwrap->interface_probe);
						ret = drvwrap->interface_probe(intf, drvwrap->private_data);
						if (!ret)
							intf->drvwrap = drvwrap;
						break;
					}
				}
			}

			if ((intf->cur_altsetting->desc.iInterface) &&
			    (intf->cur_altsetting->string == NULL)) {
				intf->cur_altsetting->string = malloc(256);
				if (intf->cur_altsetting->string)
					usb_string(dev, intf->cur_altsetting->desc.iInterface,
						   intf->cur_altsetting->string, 256);
			}
		}
	}

	return 0;
}

