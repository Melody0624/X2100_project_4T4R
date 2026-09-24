#include "os.h"

#include "storage.h"
#include "transport.h"
#include "protocol.h"
#include "scsiglue.h"

/* This is the completion handler which will wake us up when an URB
 * completes.
 */
static void usb_stor_blocking_completion(struct urb *urb)
{
	thread_waiter_t *urb_done_ptr = (thread_waiter_t *)urb->context;

	US_DEBUGP("%s called\n", __FUNCTION__);

	thread_waiter_wakeup(urb_done_ptr);
}

/* This is the common part of the URB message submission code
 *
 * All URBs from the usb-storage driver involved in handling a queued scsi
 * command _must_ pass through this function (or something like it) for the
 * abort mechanisms to work properly.
 */
static int usb_stor_msg_common(struct us_data *us, int timeout)
{
	struct thread_waiter urb_done;
	int status;

	/* don't submit URBs during disconnect processing */
	if (us->flags & US_FLIDX_DISCONNECTING)
		return -EIO;

	/* set up data structures for the wakeup system */
	thread_waiter_init(&urb_done);

	/* fill the common fields in the URB */
	us->current_urb->context = &urb_done;
	us->current_urb->transfer_flags = 0;

	if (us->current_urb->transfer_buffer == us->iobuf) {
		us->current_urb->transfer_dma = us->iobuf_dma;
		us->current_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	} else if (us->current_urb->transfer_buffer == us->cr) {
		us->current_urb->transfer_dma = us->cr_dma;
		us->current_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	/* submit the URB */
	status = usb_submit_urb(us->current_urb);
	if (status) {
		/* something went wrong */
		return status;
	}

	/* wait for the completion of the URB */
	status = thread_waiter_wait_timeout(&urb_done, timeout ? : OS_TIMEOUT_NOT_LIMIT_MS);

	/* clean up the timeout timer */
	if (status) {
		US_DEBUGP("Timeout -- cancelling URB\n");
		usb_kill_urb(us->current_urb);
		thread_waiter_wait(&urb_done);
	}

	/* return the URB status */
	return us->current_urb->status;
}

/*
 * Transfer one control message, with timeouts, and allowing early
 * termination.  Return codes are usual -Exxx, *not* USB_STOR_XFER_xxx.
 */
int usb_stor_control_msg(struct us_data *us, unsigned int pipe,
		 u8 request, u8 requesttype, u16 value, u16 index,
		 void *data, u16 size, int timeout)
{
	int status;

	US_DEBUGP("%s: rq=%02x rqtype=%02x value=%04x index=%02x len=%u\n",
			__FUNCTION__, request, requesttype,
			value, index, size);

	/* fill in the devrequest structure */
	us->cr->bRequestType = requesttype;
	us->cr->bRequest = request;
	us->cr->wValue = value;
	us->cr->wIndex = index;
	us->cr->wLength = size;

	/* fill and submit the URB */
	usb_fill_control_urb(us->current_urb, us->pusb_dev, pipe,
			 (unsigned char*) us->cr, data, size,
			 usb_stor_blocking_completion, NULL);

	status = usb_stor_msg_common(us, timeout);

	/* return the actual length of the data transferred if no error */
	if (status == 0)
		status = us->current_urb->actual_length;
	return status;
}

/* This is a version of usb_clear_halt() that allows early termination and
 * doesn't read the status from the device -- this is because some devices
 * crash their internal firmware when the status is requested after a halt.
 *
 * A definitive list of these 'bad' devices is too difficult to maintain or
 * make complete enough to be useful.  This problem was first observed on the
 * Hagiwara FlashGate DUAL unit.  However, bus traces reveal that neither
 * MacOS nor Windows checks the status after clearing a halt.
 *
 * Since many vendors in this space limit their testing to interoperability
 * with these two OSes, specification violations like this one are common.
 */
int usb_stor_clear_halt(struct us_data *us, unsigned int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);

	if (usb_pipein (pipe))
		endp |= USB_DIR_IN;

	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT,
		USB_ENDPOINT_HALT, endp,
		NULL, 0, 3*1000);

	US_DEBUGP("%s: result = %d\n", __FUNCTION__, result);
	return result;
}


/*
 * Interpret the results of a URB transfer
 *
 * This function prints appropriate debugging messages, clears halts on
 * non-control endpoints, and translates the status to the corresponding
 * USB_STOR_XFER_xxx return code.
 */
static int interpret_urb_result(struct us_data *us, unsigned int pipe,
		unsigned int length, int result, unsigned int partial)
{
	US_DEBUGP("Status code %d; transferred %u/%u\n",
			result, partial, length);
	switch (result) {

	/* no error code; did we send all the data? */
	case 0:
		if (partial != length) {
			US_DEBUGP("-- short transfer\n");
			return USB_STOR_XFER_SHORT;
		}

		US_DEBUGP("-- transfer complete\n");
		return USB_STOR_XFER_GOOD;

	/* stalled */
	case -EPIPE:
		/* for control endpoints, (used by CB[I]) a stall indicates
		 * a failed command */
		if (usb_pipecontrol(pipe)) {
			US_DEBUGP("-- stall on control pipe\n");
			return USB_STOR_XFER_STALLED;
		}

		/* for other sorts of endpoint, clear the stall */
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		if (usb_stor_clear_halt(us, pipe) < 0)
			return USB_STOR_XFER_ERROR;
		return USB_STOR_XFER_STALLED;

	/* timeout or excessively long NAK */
	case -ETIMEDOUT:
		US_DEBUGP("-- timeout or NAK\n");
		return USB_STOR_XFER_ERROR;

	/* babble - the device tried to send more than we wanted to read */
	case -EOVERFLOW:
		US_DEBUGP("-- babble\n");
		return USB_STOR_XFER_LONG;

	/* the transfer was cancelled by abort, disconnect, or timeout */
	case -ECONNRESET:
		US_DEBUGP("-- transfer cancelled\n");
		return USB_STOR_XFER_ERROR;

	/* abort or disconnect in progress */
	case -EIO:
		US_DEBUGP("-- abort or disconnect in progress\n");
		return USB_STOR_XFER_ERROR;

	/* the catch-all error case */
	default:
		US_DEBUGP("-- unknown error\n");
		return USB_STOR_XFER_ERROR;
	}
}

/*
 * Transfer one control message, without timeouts, but allowing early
 * termination.  Return codes are USB_STOR_XFER_xxx.
 */
int usb_stor_ctrl_transfer(struct us_data *us, unsigned int pipe,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size)
{
	int result;

	US_DEBUGP("%s: rq=%02x rqtype=%02x value=%04x index=%02x len=%u\n",
			__FUNCTION__, request, requesttype,
			value, index, size);

	/* fill in the devrequest structure */
	us->cr->bRequestType = requesttype;
	us->cr->bRequest = request;
	us->cr->wValue = value;
	us->cr->wIndex = index;
	us->cr->wLength = size;

	/* fill and submit the URB */
	usb_fill_control_urb(us->current_urb, us->pusb_dev, pipe,
			 (unsigned char*) us->cr, data, size,
			 usb_stor_blocking_completion, NULL);

	result = usb_stor_msg_common(us, 0);

	return interpret_urb_result(us, pipe, size, result,
			us->current_urb->actual_length);
}

/*
 * Receive one interrupt buffer, without timeouts, but allowing early
 * termination.  Return codes are USB_STOR_XFER_xxx.
 *
 * This routine always uses us->recv_intr_pipe as the pipe and
 * us->ep_bInterval as the interrupt interval.
 */
static int usb_stor_intr_transfer(struct us_data *us, void *buf,
				  unsigned int length)
{
	int result;
	unsigned int pipe = us->recv_intr_pipe;
	unsigned int maxp;

	US_DEBUGP("%s: xfer %u bytes\n", __FUNCTION__, length);

	/* calculate the max packet size */
	maxp = usb_maxpacket(us->pusb_dev, pipe, usb_pipeout(pipe));
	if (maxp > length)
		maxp = length;

	/* fill and submit the URB */
	usb_fill_int_urb(us->current_urb, us->pusb_dev, pipe, buf,
			maxp, usb_stor_blocking_completion, NULL,
			us->ep_bInterval);

	result = usb_stor_msg_common(us, 0);

	return interpret_urb_result(us, pipe, length, result,
			us->current_urb->actual_length);
}

/*
 * Transfer one buffer via bulk pipe, without timeouts, but allowing early
 * termination.  Return codes are USB_STOR_XFER_xxx.  If the bulk pipe
 * stalls during the transfer, the halt is automatically cleared.
 */
int usb_stor_bulk_transfer_buf(struct us_data *us, unsigned int pipe,
	void *buf, unsigned int length, unsigned int *act_len)
{
	int result;

	US_DEBUGP("%s: xfer %u bytes\n", __FUNCTION__, length);

	/* fill and submit the URB */
	usb_fill_bulk_urb(us->current_urb, us->pusb_dev, pipe, buf, length,
		      usb_stor_blocking_completion, NULL);
	result = usb_stor_msg_common(us, 0);

	/* store the actual length of the data transferred */
	if (act_len)
		*act_len = us->current_urb->actual_length;
	return interpret_urb_result(us, pipe, length, result,
			us->current_urb->actual_length);
}

/*
 * Transfer an entire SCSI command's worth of data payload over the bulk
 * pipe.
 *
 * Note that this uses usb_stor_bulk_transfer_buf() and
 * usb_stor_bulk_transfer_sglist() to achieve its goals --
 * this function simply determines whether we're going to use
 * scatter-gather or not, and acts appropriately.
 */
int usb_stor_bulk_transfer_sg(struct us_data* us, unsigned int pipe,
		void *buf, unsigned int length_left, int *residual)
{
	int result;
	unsigned int partial;

	/* no scatter-gather, just make the request */
	result = usb_stor_bulk_transfer_buf(us, pipe, buf, 
			length_left, &partial);
	length_left -= partial;

	/* store the residual and return the error code */
	if (residual)
		*residual = length_left;
	return result;
}

/***********************************************************************
 * Transport routines
 ***********************************************************************/

/* Invoke the transport and basic error-handling/recovery methods
 *
 * This is used by the protocol layers to actually send the message to
 * the device and receive the response.
 */
void usb_stor_invoke_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int need_auto_sense;
	int result;

	/* send the command to the transport layer */
	srb->resid = 0;
	result = us->transport(srb, us);

	/* if there is a transport error, reset and don't auto-sense */
	if (result == USB_STOR_TRANSPORT_ERROR) {
		US_DEBUGP("-- transport indicates error, resetting\n");
		us->transport_reset(us);
		srb->result = DID_ERROR << 16;
		return;
	}

	/* if the transport provided its own sense data, don't auto-sense */
	if (result == USB_STOR_TRANSPORT_NO_SENSE) {
		srb->result = SAM_STAT_CHECK_CONDITION;
		return;
	}

	srb->result = SAM_STAT_GOOD;

	/* Determine if we need to auto-sense
	 *
	 * I normally don't use a flag like this, but it's almost impossible
	 * to understand what's going on here if I don't.
	 */
	need_auto_sense = 0;

	/*
	 * If we're running the CB transport, which is incapable
	 * of determining status on its own, we will auto-sense
	 * unless the operation involved a data-in transfer.  Devices
	 * can signal most data-in errors by stalling the bulk-in pipe.
	 */
	if ((us->protocol == US_PR_CB || us->protocol == US_PR_DPCM_USB) &&
			srb->data_direction != DATA_DIR_FROM_HOST) {
		US_DEBUGP("-- CB transport device requiring auto-sense\n");
		need_auto_sense = 1;
	}

	/*
	 * If we have a failure, we're going to do a REQUEST_SENSE
	 * automatically.  Note that we differentiate between a command
	 * "failure" and an "error" in the transport mechanism.
	 */
	if (result == USB_STOR_TRANSPORT_FAILED) {
		US_DEBUGP("-- transport indicates command failure\n");
		need_auto_sense = 1;
	}

	/*
	 * A short transfer on a command where we don't expect it
	 * is unusual, but it doesn't mean we need to auto-sense.
	 */
	if ((srb->resid > 0) &&
	    !((srb->cmnd[0] == REQUEST_SENSE) ||
	      (srb->cmnd[0] == INQUIRY) ||
	      (srb->cmnd[0] == MODE_SENSE) ||
	      (srb->cmnd[0] == LOG_SENSE) ||
	      (srb->cmnd[0] == MODE_SENSE_10))) {
		US_DEBUGP("-- unexpectedly short transfer\n");
	}

	/* Now, if we need to do the auto-sense, let's do it */
	if (need_auto_sense) {
		int temp_result;
		void* old_request_buffer;
		unsigned old_request_bufflen;
		unsigned char old_data_direction;
		unsigned char old_cmd_len;
		unsigned char old_cmnd[MAX_COMMAND_SIZE];
		int old_resid;

		US_DEBUGP("Issuing auto-REQUEST_SENSE\n");

		/* save the old command */
		memcpy(old_cmnd, srb->cmnd, MAX_COMMAND_SIZE);
		old_cmd_len = srb->cmd_len;

		/* set the command and the LUN */
		memset(srb->cmnd, 0, MAX_COMMAND_SIZE);
		srb->cmnd[0] = REQUEST_SENSE;
		srb->cmnd[1] = old_cmnd[1] & 0xE0;
		srb->cmnd[4] = 18;

		/* FIXME: we must do the protocol translation here */
		if (us->subclass == US_SC_RBC || us->subclass == US_SC_SCSI)
			srb->cmd_len = 6;
		else
			srb->cmd_len = 12;

		/* set the transfer direction */
		old_data_direction = srb->data_direction;
		srb->data_direction = DATA_DIR_FROM_HOST;

		/* use the new buffer we have */
		old_request_buffer = srb->request_buffer;
		srb->request_buffer = srb->sense_buffer;

		/* set the buffer length for transfer */
		old_request_bufflen = srb->request_bufflen;
		srb->request_bufflen = 18;

		/* issue the auto-sense command */
		old_resid = srb->resid;
		srb->resid = 0;
		temp_result = us->transport(srb, us);

		/* let's clean up right away */
		srb->resid = old_resid;
		srb->request_buffer = old_request_buffer;
		srb->request_bufflen = old_request_bufflen;
		srb->data_direction = old_data_direction;
		srb->cmd_len = old_cmd_len;
		memcpy(srb->cmnd, old_cmnd, MAX_COMMAND_SIZE);

		if (temp_result != USB_STOR_TRANSPORT_GOOD) {
			US_DEBUGP("-- auto-sense failure\n");

			/* we skip the reset if this happens to be a
			 * multi-target device, since failure of an
			 * auto-sense is perfectly valid
			 */
			us->transport_reset(us);
			srb->result = DID_ERROR << 16;
			return;
		}

		printf("usb-storage: -- auto-sense code: 0x%x, key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  srb->sense_buffer[0], srb->sense_buffer[2] & 0xf,
			  srb->sense_buffer[12],  srb->sense_buffer[13]);

		/* set the result so the higher layers expect this data */
		srb->result = SAM_STAT_CHECK_CONDITION;

		/* If things are really okay, then let's show that.  Zero
		 * out the sense buffer so the higher layers won't realize
		 * we did an unsolicited auto-sense. */
		if (result == USB_STOR_TRANSPORT_GOOD &&
			/* Filemark 0, ignore EOM, ILI 0, no sense */
				(srb->sense_buffer[2] & 0xaf) == 0 &&
			/* No ASC or ASCQ */
				srb->sense_buffer[12] == 0 &&
				srb->sense_buffer[13] == 0) {
			srb->result = SAM_STAT_GOOD;
			srb->sense_buffer[0] = 0x0;
		}
	}

	/* Did we transfer less than the minimum amount required? */
	if (srb->result == SAM_STAT_GOOD &&
			srb->request_bufflen - srb->resid < srb->underflow)
		srb->result = SUGGEST_RETRY << 24;

	return;
}

/*
 * Control/Bulk/Interrupt transport
 */

int usb_stor_CBI_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	unsigned int transfer_length = srb->request_bufflen;
	unsigned int pipe = 0;
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
				      US_CBI_ADSC,
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0,
				      us->ifnum, srb->cmnd, srb->cmd_len);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_stor_ctrl_transfer() returned %d\n", result);

	/* if we stalled the command, it means command failed */
	if (result == USB_STOR_XFER_STALLED) {
		return USB_STOR_TRANSPORT_FAILED;
	}

	/* Uh oh... serious problem here */
	if (result != USB_STOR_XFER_GOOD) {
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (transfer_length) {
		pipe = srb->data_direction == DATA_DIR_FROM_HOST ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_sg(us, pipe,
					srb->request_buffer, transfer_length, &srb->resid);
		US_DEBUGP("CBI data stage result is 0x%x\n", result);

		/* if we stalled the data transfer it means command failed */
		if (result == USB_STOR_XFER_STALLED)
			return USB_STOR_TRANSPORT_FAILED;
		if (result > USB_STOR_XFER_STALLED)
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* STATUS STAGE */
	result = usb_stor_intr_transfer(us, us->iobuf, 2);
	US_DEBUGP("Got interrupt data (0x%x, 0x%x)\n", 
			us->iobuf[0], us->iobuf[1]);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* UFI gives us ASC and ASCQ, like a request sense
	 *
	 * REQUEST_SENSE and INQUIRY don't affect the sense data on UFI
	 * devices, so we ignore the information for those commands.  Note
	 * that this means we could be ignoring a real error on these
	 * commands, but that can't be helped.
	 */
	if (us->subclass == US_SC_UFI) {
		if (srb->cmnd[0] == REQUEST_SENSE ||
		    srb->cmnd[0] == INQUIRY)
			return USB_STOR_TRANSPORT_GOOD;
		if (us->iobuf[0])
			goto Failed;
		return USB_STOR_TRANSPORT_GOOD;
	}

	/* If not UFI, we interpret the data as a result code 
	 * The first byte should always be a 0x0.
	 *
	 * Some bogus devices don't follow that rule.  They stuff the ASC
	 * into the first byte -- so if it's non-zero, call it a failure.
	 */
	if (us->iobuf[0]) {
		US_DEBUGP("CBI IRQ data showed reserved bType 0x%x\n",
				us->iobuf[0]);
		goto Failed;

	}

	/* The second byte & 0x0F should be 0x0 for good, otherwise error */
	switch (us->iobuf[1] & 0x0F) {
		case 0x00:
			return USB_STOR_TRANSPORT_GOOD;
		case 0x01:
			goto Failed;
	}
	return USB_STOR_TRANSPORT_ERROR;

	/* the CBI spec requires that the bulk pipe must be cleared
	 * following any data-in/out command failure (section 2.4.3.1.3)
	 */
  Failed:
	if (pipe)
		usb_stor_clear_halt(us, pipe);
	return USB_STOR_TRANSPORT_FAILED;
}

/*
 * Control/Bulk transport
 */
int usb_stor_CB_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	unsigned int transfer_length = srb->request_bufflen;
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
				      US_CBI_ADSC, 
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				      us->ifnum, srb->cmnd, srb->cmd_len);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_stor_ctrl_transfer() returned %d\n", result);

	/* if we stalled the command, it means command failed */
	if (result == USB_STOR_XFER_STALLED) {
		return USB_STOR_TRANSPORT_FAILED;
	}

	/* Uh oh... serious problem here */
	if (result != USB_STOR_XFER_GOOD) {
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (transfer_length) {
		unsigned int pipe = srb->data_direction == DATA_DIR_FROM_HOST ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_sg(us, pipe,
					srb->request_buffer, transfer_length, &srb->resid);
		US_DEBUGP("CB data stage result is 0x%x\n", result);

		/* if we stalled the data transfer it means command failed */
		if (result == USB_STOR_XFER_STALLED)
			return USB_STOR_TRANSPORT_FAILED;
		if (result > USB_STOR_XFER_STALLED)
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* STATUS STAGE */
	/* NOTE: CB does not have a status stage.  Silly, I know.  So
	 * we have to catch this at a higher level.
	 */
	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Bulk only transport
 */

/* Determine what the maximum LUN supported is */
int usb_stor_Bulk_max_lun(struct us_data *us)
{
	int result;

	/* issue the command */
	result = usb_stor_control_msg(us, us->recv_ctrl_pipe,
				 US_BULK_GET_MAX_LUN, 
				 USB_DIR_IN | USB_TYPE_CLASS | 
				 USB_RECIP_INTERFACE,
				 0, us->ifnum, us->iobuf, 1, 1000);

	US_DEBUGP("GetMaxLUN command result is %d, data is %d\n", 
		  result, us->iobuf[0]);

	/* if we have a successful request, return the result */
	if (result > 0)
		return us->iobuf[0];

	/* 
	 * Some devices (i.e. Iomega Zip100) need this -- apparently
	 * the bulk pipes get STALLed when the GetMaxLUN request is
	 * processed.   This is, in theory, harmless to all other devices
	 * (regardless of if they stall or not).
	 */
	if (result == -EPIPE) {
		usb_stor_clear_halt(us, us->recv_bulk_pipe);
		usb_stor_clear_halt(us, us->send_bulk_pipe);
	}

	/*
	 * Some devices don't like GetMaxLUN.  They may STALL the control
	 * pipe, they may return a zero-length result, they may do nothing at
	 * all and timeout, or they may fail in even more bizarrely creative
	 * ways.  In these cases the best approach is to use the default
	 * value: only one LUN.
	 */
	return 0;
}

int usb_stor_Bulk_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;
	unsigned int transfer_length = srb->request_bufflen;
	unsigned int residue;
	int result;
	int fake_sense = 0;
	unsigned int cswlen;
	unsigned int cbwlen = US_BULK_CB_WRAP_LEN;

	/* set up the command wrapper */
	bcb->Signature = US_BULK_CB_SIGN;
	bcb->DataTransferLength = transfer_length;
	bcb->Flags = srb->data_direction == DATA_DIR_FROM_HOST ? 1 << 7 : 0;
	bcb->Tag = ++us->tag;
	bcb->Lun = srb->lun;
	bcb->Length = srb->cmd_len;

	/* copy the command payload */
	memset(bcb->CDB, 0, sizeof(bcb->CDB));
	memcpy(bcb->CDB, srb->cmnd, bcb->Length);

	/* send it to out endpoint */
	US_DEBUGP("Bulk Command S 0x%x L %d F %d LUN %d CL %d\n",
			bcb->Signature, bcb->DataTransferLength, 
			bcb->Flags, bcb->Lun & 0x0F, bcb->Length);
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
				bcb, cbwlen, NULL);
	US_DEBUGP("Bulk command transfer result=%d\n", result);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/*
	 * Some USB-IDE converter chips need a 100us delay between the
	 * command phase and the data phase.  Some devices need a little
	 * more than that, probably because of clock rate inaccuracies.
	 */
	usleep(150);

	/* DATA STAGE */
	/* send/receive data payload, if there is any */
	if (transfer_length) {
		unsigned int pipe = srb->data_direction == DATA_DIR_FROM_HOST ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_sg(us, pipe,
					srb->request_buffer, transfer_length, &srb->resid);
		US_DEBUGP("Bulk data transfer result 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;

		/* If the device tried to send back more data than the
		 * amount requested, the spec requires us to transfer
		 * the CSW anyway.  Since there's no point retrying the
		 * the command, we'll return fake sense data indicating
		 * Illegal Request, Invalid Field in CDB.
		 */
		if (result == USB_STOR_XFER_LONG)
			fake_sense = 1;

		/*
		 * Sometimes a device will mistakenly skip the data phase
		 * and go directly to the status phase without sending a
		 * zero-length packet.  If we get a 13-byte response here,
		 * check whether it really is a CSW.
		 */
		if (result == USB_STOR_XFER_SHORT &&
				srb->data_direction == DATA_DIR_FROM_HOST &&
				transfer_length - srb->resid == US_BULK_CS_WRAP_LEN) {
			unsigned int index = 0, offset = 0;

			if (usb_stor_access_xfer_buf((unsigned char *) bcs,
					US_BULK_CS_WRAP_LEN, srb, &index,
					&offset, FROM_XFER_BUF) ==
						US_BULK_CS_WRAP_LEN &&
					bcs->Signature == US_BULK_CS_SIGN) {
				US_DEBUGP("Device skipped data phase\n");
				srb->resid = transfer_length;
				goto skipped_data_phase;
			}
		}
	}

	/* See flow chart on pg 15 of the Bulk Only Transport spec for
	 * an explanation of how this code works.
	 */

	/* get CSW for device status */
	US_DEBUGP("Attempting to get CSW...\n");
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, &cswlen);

	/* Some broken devices add unnecessary zero-length packets to the
	 * end of their data transfers.  Such packets show up as 0-length
	 * CSWs.  If we encounter such a thing, try to read the CSW again.
	 */
	if (result == USB_STOR_XFER_SHORT && cswlen == 0) {
		US_DEBUGP("Received 0-length CSW; retrying...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, &cswlen);
	}

	/* did the attempt to read the CSW fail? */
	if (result == USB_STOR_XFER_STALLED) {

		/* get the status again */
		US_DEBUGP("Attempting to get CSW (2nd try)...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, NULL);
	}

	/* if we still have a failure at this point, we're in trouble */
	US_DEBUGP("Bulk status result = %d\n", result);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

 skipped_data_phase:
	/* check bulk status */
	residue = bcs->Residue;
	US_DEBUGP("Bulk Status S 0x%x T 0x%x R %u Stat 0x%x\n",
			bcs->Signature, bcs->Tag, 
			residue, bcs->Status);
	if (!(bcs->Tag == us->tag) || bcs->Status > US_BULK_STAT_PHASE) {
		US_DEBUGP("Bulk logical error\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* Some broken devices report odd signatures, so we do not check them
	 * for validity against the spec. We store the first one we see,
	 * and check subsequent transfers for validity against this signature.
	 */
	if (!us->bcs_signature) {
		us->bcs_signature = bcs->Signature;
		if (us->bcs_signature != US_BULK_CS_SIGN)
			US_DEBUGP("Learnt BCS signature 0x%08X\n", us->bcs_signature);
	} else if (bcs->Signature != us->bcs_signature) {
		US_DEBUGP("Signature mismatch: got %08X, expecting %08X\n",
			  bcs->Signature, us->bcs_signature);
		return USB_STOR_TRANSPORT_ERROR;
	}

	/*
	 * try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us
	 */
	if (residue && !(us->flags & US_FL_IGNORE_RESIDUE)) {

		/*
		 * Heuristically detect devices that generate bogus residues
		 * by seeing what happens with INQUIRY and READ CAPACITY
		 * commands.
		 */
		if (bcs->Status == US_BULK_STAT_OK &&
				srb->resid == 0 &&
					((srb->cmnd[0] == INQUIRY &&
						transfer_length == 36) ||
					(srb->cmnd[0] == READ_CAPACITY &&
						transfer_length == 8))) {
			us->flags |= US_FL_IGNORE_RESIDUE;

		} else {
			residue = min(residue, transfer_length);
			srb->resid = max(srb->resid, (int) residue);
		}
	}

	/* based on the status code, we report good or bad */
	switch (bcs->Status) {
		case US_BULK_STAT_OK:
			/* device babbled -- return fake sense data */
			if (fake_sense) {
				memcpy(srb->sense_buffer, 
				       usb_stor_sense_invalidCDB, 
				       sizeof(usb_stor_sense_invalidCDB));
				return USB_STOR_TRANSPORT_NO_SENSE;
			}

			/* command good -- note that data could be short */
			return USB_STOR_TRANSPORT_GOOD;

		case US_BULK_STAT_FAIL:
			/* command failed */
			return USB_STOR_TRANSPORT_FAILED;

		case US_BULK_STAT_PHASE:
			/* phase error -- note that a transport reset will be
			 * invoked by the invoke_transport() function
			 */
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/***********************************************************************
 * Reset routines
 ***********************************************************************/

/* This is the common part of the device reset code.
 *
 * It's handy that every transport mechanism uses the control endpoint for
 * resets.
 *
 * Basically, we send a reset with a 20-second timeout, so we don't get
 * jammed attempting to do the reset.
 */
static int usb_stor_reset_common(struct us_data *us,
		u8 request, u8 requesttype,
		u16 value, u16 index, void *data, u16 size)
{
	int delay;
	int result;
	int result2;

	/* A 20-second timeout may seem rather long, but a LaCie
	 * StudioDrive USB2 device takes 16+ seconds to get going
	 * following a powerup or USB attach event.
	 */
	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
			request, requesttype, value, index, data, size,
			20*1000);
	if (result < 0) {
		US_DEBUGP("Soft reset failed: %d\n", result);
		return -EIO;
	}

	/* Give the device some time to recover from the reset,
	 * but don't delay disconnect processing. */
	delay = 60;
	while (delay--)
	{
		if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
			US_DEBUGP("Reset interrupted by disconnect\n");
			return -ENODEV;
		}

		msleep(100);
	}

	US_DEBUGP("Soft reset: clearing bulk-in endpoint halt\n");
	result = usb_stor_clear_halt(us, us->recv_bulk_pipe);

	US_DEBUGP("Soft reset: clearing bulk-out endpoint halt\n");
	result2 = usb_stor_clear_halt(us, us->send_bulk_pipe);

	/* return a result code based on the result of the control message */
	if (result < 0 || result2 < 0) {
		US_DEBUGP("Soft reset failed\n");
		return -EIO;
	}
	US_DEBUGP("Soft reset done\n");

	return 0;
}

/* This issues a CB[I] Reset to the device in question
 */
#define CB_RESET_CMD_SIZE	12

int usb_stor_CB_reset(struct us_data *us)
{
	US_DEBUGP("%s called\n", __FUNCTION__);

	memset(us->iobuf, 0xFF, CB_RESET_CMD_SIZE);
	us->iobuf[0] = SEND_DIAGNOSTIC;
	us->iobuf[1] = 4;
	return usb_stor_reset_common(us, US_CBI_ADSC, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, us->iobuf, CB_RESET_CMD_SIZE);
}

/* This issues a Bulk-only Reset to the device in question, including
 * clearing the subsequent endpoint halts that may occur.
 */
int usb_stor_Bulk_reset(struct us_data *us)
{
	US_DEBUGP("%s called\n", __FUNCTION__);

	return usb_stor_reset_common(us, US_BULK_RESET_REQUEST, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, NULL, 0);
}
