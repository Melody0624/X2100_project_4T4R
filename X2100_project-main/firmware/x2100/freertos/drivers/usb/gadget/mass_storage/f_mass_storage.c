#include <os.h>
#include <be_byteshift.h>
#include "scsi.h"
#include "storage.h"
#include <driver/cache.h>
#include "f_mass_storage.h"
#include "../../usb_lock.h"

#define FSG_DRIVER_DESC		"Mass Storage Function"
#define FSG_DRIVER_VERSION	"2009/09/11"

static const char fsg_string_interface[] = "Mass Storage";

/* There is only one interface. */
struct usb_interface_descriptor fsg_intf_desc = {
	.bLength =		sizeof fsg_intf_desc,
	.bDescriptorType =	USB_DT_INTERFACE,

	.bNumEndpoints =	2,		/* Adjusted during fsg_bind() */
	.bInterfaceClass =	USB_CLASS_MASS_STORAGE,
	.bInterfaceSubClass =	USB_SC_SCSI,	/* Adjusted during fsg_bind() */
	.bInterfaceProtocol =	USB_PR_BULK,	/* Adjusted during fsg_bind() */
	.iInterface =		FSG_STRING_INTERFACE,
};

/*
 * Three full-speed endpoint descriptors: bulk-in, bulk-out, and
 * interrupt-in.
 */

struct usb_endpoint_descriptor fsg_fs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

struct usb_endpoint_descriptor fsg_fs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	/* wMaxPacketSize set by autoconfiguration */
};

struct usb_descriptor_header *fsg_fs_function[] = {
	(struct usb_descriptor_header *) &fsg_intf_desc,
	(struct usb_descriptor_header *) &fsg_fs_bulk_in_desc,
	(struct usb_descriptor_header *) &fsg_fs_bulk_out_desc,
	NULL,
};

/*
 * USB 2.0 devices need to expose both high speed and full speed
 * descriptors, unless they only run at full speed.
 *
 * That means alternate endpoint descriptors (bigger packets).
 */
struct usb_endpoint_descriptor fsg_hs_bulk_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_in_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	512,
};

struct usb_endpoint_descriptor fsg_hs_bulk_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,

	/* bEndpointAddress copied from fs_bulk_out_desc during fsg_bind() */
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	512,
	.bInterval =		1,	/* NAK every 1 uframe */
};

struct usb_descriptor_header *fsg_hs_function[] = {
	(struct usb_descriptor_header *) &fsg_intf_desc,
	(struct usb_descriptor_header *) &fsg_hs_bulk_in_desc,
	(struct usb_descriptor_header *) &fsg_hs_bulk_out_desc,
	NULL,
};

/* Static strings, in UTF-8 (for simplicity we use only ASCII characters) */
static struct usb_string		fsg_strings[] = {
	{FSG_STRING_INTERFACE,		fsg_string_interface},
	{}
};

static struct usb_gadget_strings	fsg_stringtab = {
	.language	= 0x0409,		/* en-us */
	.strings	= fsg_strings,
};

static struct usb_gadget_strings *fsg_strings_array[] = {
	&fsg_stringtab,
	NULL,
};

/*-------------------------------------------------------------------------*/

struct fsg_dev;
struct fsg_common;

/* Data shared by all the FSG instances. */
struct fsg_common {
	struct usb_gadget	*gadget;
	struct usb_composite_dev *cdev;
	struct fsg_dev		*fsg;

	/* lock protects: state and thread_task */
	spinlock_t		lock;
	int connect_flag;
	connect_callback_t connect_cb;

	struct usb_ep		*ep0;		/* Copy of gadget->ep0 */
	struct usb_request	*ep0req;	/* Copy of cdev->req */
	unsigned int		ep0_req_tag;

	struct fsg_buffhd	*next_buffhd_to_fill;
	struct fsg_buffhd	*next_buffhd_to_drain;
	struct fsg_buffhd	*buffhds;

	int			cmnd_size;
	u8			cmnd[MAX_COMMAND_SIZE];

	unsigned int		lun;
	struct fsg_lun		*luns[FSG_MAX_LUNS];
	struct fsg_lun		*curlun;

	unsigned int		bulk_out_maxpacket;
	enum fsg_state		state;		/* For exception handling */
	unsigned int		exception_req_tag;
	void			*exception_arg;

	enum data_direction	data_dir;
	u32			data_size;
	u32			data_size_from_cmnd;
	u32			tag;
	u32			residue;
	u32			usb_amount_left;

	unsigned int		phase_error:1;
	unsigned int		short_packet_received:1;
	unsigned int		bad_lun_okay:1;
	unsigned int		running:1;

	thread_waiter_t	thread_notifier;
	thread_ptr_t	*thread_task;
};

struct fsg_dev {
	struct usb_function	function;
	struct fsg_common	common;
	struct usb_gadget	*gadget;	/* Copy of cdev->gadget */

	u16			interface_number;

	unsigned int		bulk_in_enabled:1;
	unsigned int		bulk_out_enabled:1;

	unsigned long		atomic_bitflags;
#define IGNORE_BULK_OUT		0

	struct usb_ep		*bulk_in;
	struct usb_ep		*bulk_out;
};

static inline int fsg_is_set(struct fsg_common *common)
{
	if (common->fsg)
		return 1;
	printf("ERROR: common->fsg is NULL in %s at %u\n", __func__, __LINE__);
	return 0;
}

static inline struct fsg_dev *fsg_from_func(struct usb_function *f)
{
	return container_of(f, struct fsg_dev, function);
}

static int exception_in_progress(struct fsg_common *common)
{
	return common->state > FSG_STATE_NORMAL;
}

/* Make bulk-out requests be divisible by the maxpacket size */
static void set_bulk_out_req_length(struct fsg_common *common,
				    struct fsg_buffhd *bh, unsigned int length)
{
	unsigned int	rem;

	bh->bulk_out_intended_length = length;
	rem = length % common->bulk_out_maxpacket;
	if (rem > 0)
		length += common->bulk_out_maxpacket - rem;
	bh->outreq->length = length;
}

/*-------------------------------------------------------------------------*/

/* These routines may be called in process context or in_irq */

static void __raise_exception(struct fsg_common *common, enum fsg_state new_state, void *arg)
{
	unsigned long		flags;

	/*
	 * Do nothing if a higher-priority exception is already in progress.
	 * If a lower-or-equal priority exception is in progress, preempt it
	 * and notify the main thread by sending it a signal.
	 */
	usb_spin_lock_irqsave(&common->lock, flags);
	if (common->state <= new_state) {
		common->exception_req_tag = common->ep0_req_tag;
		common->state = new_state;
		common->exception_arg = arg;
		if (common->thread_task)
			thread_wakeup(common->thread_task);
	}
	usb_spin_unlock_irqrestore(&common->lock, flags);
}

static void raise_exception(struct fsg_common *common, enum fsg_state new_state)
{
	__raise_exception(common, new_state, NULL);
}

/*-------------------------------------------------------------------------*/

static int ep0_queue(struct fsg_common *common)
{
	int	rc;

	rc = usb_ep_queue(common->ep0, common->ep0req);
	common->ep0->driver_data = common;
	if (rc != 0 && rc != -ESHUTDOWN) {
		/* We can't do much more than wait for a reset */
		printf("usb gadget mass storage error in submission: %s --> %d\n",
			common->ep0->name, rc);
	}
	return rc;
}


/*-------------------------------------------------------------------------*/

/* Completion handlers. These always run in_irq. */

static void bulk_in_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_common	*common = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	if (req->status || req->actual != req->length)
		printf("usb gadget mass storage %s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, req->length);
	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	bh->state = BUF_STATE_EMPTY;
	thread_wakeup(common->thread_task);
}

static void bulk_out_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct fsg_common	*common = ep->driver_data;
	struct fsg_buffhd	*bh = req->context;

	if (req->status || req->actual != bh->bulk_out_intended_length)
		printf("usb gadget mass storage %s --> %d, %u/%u\n", __func__,
		    req->status, req->actual, bh->bulk_out_intended_length);
	if (req->status == -ECONNRESET)		/* Request was cancelled */
		usb_ep_fifo_flush(ep);

	bh->state = BUF_STATE_FULL;
	thread_wakeup(common->thread_task);
}

static int _fsg_common_get_max_lun(struct fsg_common *common)
{
	int i = ARRAY_SIZE(common->luns) - 1;

	while (i >= 0 && !common->luns[i])
		--i;

	return i;
}

static int fsg_setup(struct usb_function *f,
		     const struct usb_ctrlrequest *ctrl)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct usb_request	*req = fsg->common.ep0req;
	u16			w_index = ctrl->wIndex;
	u16			w_value = ctrl->wValue;
	u16			w_length = ctrl->wLength;

	if (!fsg_is_set(&fsg->common))
		return -EOPNOTSUPP;

	++fsg->common.ep0_req_tag;	/* Record arrival of a new request */
	req->context = NULL;
	req->length = 0;

	switch (ctrl->bRequest) {
		case US_BULK_RESET_REQUEST:
			if (ctrl->bRequestType != (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE))
				break;
			if (w_index != fsg->interface_number || w_value != 0 || w_length != 0)
				return -EDOM;
			/*
			* Raise an exception to stop the current operation
			* and reinitialize our state.
			*/
			raise_exception(&fsg->common, FSG_STATE_PROTOCOL_RESET);
			return USB_GADGET_DELAYED_STATUS;

		case US_BULK_GET_MAX_LUN:
			if (ctrl->bRequestType != (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE))
				break;
			if (w_index != fsg->interface_number || w_value != 0 || w_length != 1)
				return -EDOM;
			*(u8 *)req->buf = _fsg_common_get_max_lun(&fsg->common);
			/* Respond with data/status */
			req->length = min((u16)1, w_length);
			return ep0_queue(&fsg->common);
	}

	printf("usb gadget mass storage unknown class-specific control req %02x.%02x v%04x i%04x l%u\n",
	     ctrl->bRequestType, ctrl->bRequest, ctrl->wValue, w_index, w_length);
	return -EOPNOTSUPP;
}


/*-------------------------------------------------------------------------*/

/* All the following routines run in process context */

/* Use this for bulk or interrupt transfers, not ep0 */
static int start_transfer(struct fsg_dev *fsg, struct usb_ep *ep,
			   struct usb_request *req)
{
	int	rc;

	rc = usb_ep_queue(ep, req);
	if (rc) {

		/* We can't do much more than wait for a reset */
		req->status = rc;

		/*
		 * Note: currently the net2280 driver fails zero-length
		 * submissions if DMA is enabled.
		 */
		if (rc != -ESHUTDOWN && !(rc == -EOPNOTSUPP && req->length == 0))
			printf("usb gadget mass storage error in submission: %s --> %d\n", ep->name, rc);
	}
	return rc;
}

static bool start_in_transfer(struct fsg_common *common, struct fsg_buffhd *bh)
{
	if (!fsg_is_set(common))
		return false;
	bh->state = BUF_STATE_SENDING;
	if (start_transfer(common->fsg, common->fsg->bulk_in, bh->inreq))
		bh->state = BUF_STATE_EMPTY;
	return true;
}

static bool start_out_transfer(struct fsg_common *common, struct fsg_buffhd *bh)
{
	if (!fsg_is_set(common))
		return false;
	bh->state = BUF_STATE_RECEIVING;
	if (start_transfer(common->fsg, common->fsg->bulk_out, bh->outreq))
		bh->state = BUF_STATE_FULL;
	return true;
}

static int sleep_thread(struct fsg_common *common, struct fsg_buffhd *bh)
{
	if (exception_in_progress(common))
		return -EINTR;

	while (!bh || bh->state < BUF_STATE_EMPTY)
	{
		thread_wait();

		if (exception_in_progress(common))
			return -EINTR;
	}

	return 0;
}


/*-------------------------------------------------------------------------*/

static int do_read(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	u64			lba;
	struct fsg_buffhd	*bh;
	int			rc;
	u32			amount_left;
	u64			file_offset;
	unsigned int		amount;
	int			nread;

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big.
	 */
	if (common->cmnd[0] == READ_6)
		lba = get_unaligned_be24(&common->cmnd[1]);
	else {
		if (common->cmnd[0] == READ_16)
			lba = get_unaligned_be64(&common->cmnd[2]);
		else		/* READ_10 or READ_12 */
			lba = get_unaligned_be32(&common->cmnd[2]);

		/*
		 * We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = don't read from the
		 * cache), but we don't implement them.
		 */
		if ((common->cmnd[1] & ~0x18) != 0) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}
	file_offset = lba * curlun->blksize;

	/* Carry out the file reads */
	amount_left = common->data_size_from_cmnd;
	if (amount_left == 0)
		return -EIO;		/* No default reply */

	for (;;) {
		/*
		 * Figure out how much we need to read:
		 * Try to read the remaining amount.
		 * But don't read more than the buffer size.
		 * And don't try to read past the end of the file.
		 */
		amount = min(amount_left, FSG_BUFLEN);
		amount = min((u64)amount, curlun->file_length - file_offset);

		/* Wait for the next buffer to become available */
		bh = common->next_buffhd_to_fill;
		rc = sleep_thread(common, bh);
		if (rc)
			return rc;

		/*
		 * If we were asked to read past the end of file,
		 * end with an empty buffer.
		 */
		if (amount == 0) {
			curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info = file_offset / curlun->blksize;
			curlun->info_valid = 1;
			bh->inreq->length = 0;
			bh->state = BUF_STATE_FULL;
			break;
		}

		/* Perform the read */
		nread = curlun->read_callback(bh->buf, amount, file_offset);
		if (exception_in_progress(common))
			return -EINTR;

		if (nread < 0) {
			printf("usb gadget mass storage read_callback error: %d\n", nread);
			nread = 0;
		} else if (nread < amount) {
			printf("usb gadget mass storage read_callback verify: %d/%u\n", nread, amount);
			nread = round_down(nread, curlun->blksize);
		}
		file_offset  += nread;
		amount_left  -= nread;
		common->residue -= nread;

		/*
		 * Except at the end of the transfer, nread will be
		 * equal to the buffer size, which is divisible by the
		 * bulk-in maxpacket size.
		 */
		bh->inreq->length = nread;
		bh->state = BUF_STATE_FULL;

		/* If an error occurred, report it and its position */
		if (nread < amount) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info = file_offset / curlun->blksize;
			curlun->info_valid = 1;
			break;
		}

		if (amount_left == 0)
			break;		/* No more left to read */

		/* Send this buffer and go read some more */
		bh->inreq->zero = 0;
		if (!start_in_transfer(common, bh))
			/* Don't know what to do if common->fsg is NULL */
			return -EIO;
		common->next_buffhd_to_fill = bh->next;
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/

static int do_write(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	u64			lba;
	struct fsg_buffhd	*bh;
	int			get_some_more;
	u32			amount_left_to_req, amount_left_to_write;
	u64			usb_offset, file_offset;
	unsigned int		amount;
	int			nwritten;
	int			rc;

	if (curlun->ro) {
		curlun->sense_data = SS_WRITE_PROTECTED;
		return -EINVAL;
	}

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big
	 */
	if (common->cmnd[0] == WRITE_6)
		lba = get_unaligned_be24(&common->cmnd[1]);
	else {
		if (common->cmnd[0] == WRITE_16)
			lba = get_unaligned_be64(&common->cmnd[2]);
		else		/* WRITE_10 or WRITE_12 */
			lba = get_unaligned_be32(&common->cmnd[2]);

		/*
		 * We allow DPO (Disable Page Out = don't save data in the
		 * cache) and FUA (Force Unit Access = write directly to the
		 * medium).  We don't implement DPO; we implement FUA by
		 * performing synchronous output.
		 */
		if (common->cmnd[1] & ~0x18) {
			curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/* Carry out the file writes */
	get_some_more = 1;
	file_offset = usb_offset = lba * curlun->blksize;
	amount_left_to_req = common->data_size_from_cmnd;
	amount_left_to_write = common->data_size_from_cmnd;

	while (amount_left_to_write > 0) {

		/* Queue a request for more data from the host */
		bh = common->next_buffhd_to_fill;
		if (bh->state == BUF_STATE_EMPTY && get_some_more) {

			/*
			 * Figure out how much we want to get:
			 * Try to get the remaining amount,
			 * but not more than the buffer size.
			 */
			amount = min(amount_left_to_req, FSG_BUFLEN);

			/* Beyond the end of the backing file? */
			if (usb_offset >= curlun->file_length) {
				get_some_more = 0;
				curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
				curlun->sense_data_info = usb_offset / curlun->blksize;
				curlun->info_valid = 1;
				continue;
			}

			/* Get the next buffer */
			usb_offset += amount;
			common->usb_amount_left -= amount;
			amount_left_to_req -= amount;
			if (amount_left_to_req == 0)
				get_some_more = 0;

			/*
			 * Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(common, bh, amount);
			if (!start_out_transfer(common, bh))
				/* Dunno what to do if common->fsg is NULL */
				return -EIO;
			common->next_buffhd_to_fill = bh->next;
			continue;
		}

		/* Write the received data to the backing file */
		bh = common->next_buffhd_to_drain;
		if (bh->state == BUF_STATE_EMPTY && !get_some_more)
			break;			/* We stopped early */

		/* Wait for the data to be received */
		rc = sleep_thread(common, bh);
		if (rc)
			return rc;

		common->next_buffhd_to_drain = bh->next;
		bh->state = BUF_STATE_EMPTY;

		/* Did something go wrong with the transfer? */
		if (bh->outreq->status != 0) {
			curlun->sense_data = SS_COMMUNICATION_FAILURE;
			curlun->sense_data_info = file_offset / curlun->blksize;
			curlun->info_valid = 1;
			break;
		}

		amount = bh->outreq->actual;
		if (curlun->file_length - file_offset < amount) {
			printf("usb gadget mass storage write %u @ %llu beyond end %llu\n", amount, file_offset, curlun->file_length);
			amount = curlun->file_length - file_offset;
		}

		/*
		 * Don't accept excess data.  The spec doesn't say
		 * what to do in this case.  We'll ignore the error.
		 */
		amount = min(amount, bh->bulk_out_intended_length);

		/* Don't write a partial block */
		amount = round_down(amount, curlun->blksize);
		if (amount == 0)
			goto empty_write;

		/* Perform the write */
		nwritten = curlun->write_callback(bh->buf, amount, file_offset);
		if (exception_in_progress(common))
			return -EINTR;

		if (nwritten < 0) {
			printf("usb gadget mass storage write_callback error: %d\n", nwritten);
			nwritten = 0;
		} else if (nwritten < amount) {
			printf("usb gadget mass storage write_callback verify: %d/%u\n", nwritten, amount);
			nwritten = round_down(nwritten, curlun->blksize);
		}
		file_offset += nwritten;
		amount_left_to_write -= nwritten;
		common->residue -= nwritten;

		/* If an error occurred, report it and its position */
		if (nwritten < amount) {
			curlun->sense_data = SS_WRITE_ERROR;
			curlun->sense_data_info =
					file_offset / curlun->blksize;
			curlun->info_valid = 1;
			break;
		}

 empty_write:
		/* Did the host decide to stop early? */
		if (bh->outreq->actual < bh->bulk_out_intended_length) {
			common->short_packet_received = 1;
			break;
		}
	}

	return -EIO;		/* No default reply */
}


/*-------------------------------------------------------------------------*/
static void store_cdrom_address(u8 *dest, int msf, u32 addr)
{
	if (msf) {
		/* Convert to Minutes-Seconds-Frames */
		addr >>= 2;		/* Convert to 2048-byte frames */
		addr += 2*75;		/* Lead-in occupies 2 seconds */
		dest[3] = addr % 75;	/* Frames */
		addr /= 75;
		dest[2] = addr % 60;	/* Seconds */
		addr /= 60;
		dest[1] = addr;		/* Minutes */
		dest[0] = 0;		/* Reserved */
	} else {
		/* Absolute sector */
		put_unaligned_be32(addr, dest);
	}
}

static int fsg_lun_fsync_sub(struct fsg_lun *curlun)
{
	if (curlun->ro || !curlun->sync_callback)
		return 0;
	return curlun->sync_callback();
}

static int do_synchronize_cache(struct fsg_common *common)
{
	struct fsg_lun	*curlun = common->curlun;
	int		rc;

	/* We ignore the requested LBA and write out all file's
	 * dirty data buffers. */
	rc = fsg_lun_fsync_sub(curlun);
	if (rc)
		curlun->sense_data = SS_WRITE_ERROR;
	return 0;
}

static int do_verify(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	u64			lba;
	u32			verification_length;
	struct fsg_buffhd	*bh = common->next_buffhd_to_fill;
	u64			file_offset;
	u32			amount_left;
	unsigned int		amount;
	int			nread;

	/*
	 * Get the starting Logical Block Address and check that it's
	 * not too big.
	 */
	lba = get_unaligned_be32(&common->cmnd[2]);
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	/*
	 * We allow DPO (Disable Page Out = don't save data in the
	 * cache) but we don't implement it.
	 */
	if (common->cmnd[1] & ~0x10) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	verification_length = get_unaligned_be16(&common->cmnd[7]);
	if (verification_length == 0)
		return -EIO;		/* No default reply */

	/* Prepare to carry out the file verify */
	amount_left = verification_length * curlun->blksize;
	file_offset = lba * curlun->blksize;

	/* Write out all the dirty buffers before invalidating them */
	fsg_lun_fsync_sub(curlun);
	if (exception_in_progress(common))
		return -EINTR;

	/* Just try to read the requested blocks */
	while (amount_left > 0) {
		/*
		 * Figure out how much we need to read:
		 * Try to read the remaining amount, but not more than
		 * the buffer size.
		 * And don't try to read past the end of the file.
		 */
		amount = min(amount_left, FSG_BUFLEN);
		amount = min((u64)amount,
			     curlun->file_length - file_offset);
		if (amount == 0) {
			curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
			curlun->sense_data_info = file_offset / curlun->blksize;
			curlun->info_valid = 1;
			break;
		}

		/* Perform the read */
		nread = curlun->read_callback(bh->buf, amount, file_offset);
		if (exception_in_progress(common))
			return -EINTR;

		if (nread < 0) {
			printf("usb gadget mass storage read_callback error: %d\n", nread);
			nread = 0;
		} else if (nread < amount) {
			printf("usb gadget mass storage read_callback verify: %d/%u\n", nread, amount);
			nread = round_down(nread, curlun->blksize);
		}
		if (nread == 0) {
			curlun->sense_data = SS_UNRECOVERED_READ_ERROR;
			curlun->sense_data_info = file_offset / curlun->blksize;
			curlun->info_valid = 1;
			break;
		}
		file_offset += nread;
		amount_left -= nread;
	}
	return 0;
}

static int do_inquiry(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun *curlun = common->curlun;
	u8	*buf = (u8 *) bh->buf;

	if (!curlun) {		/* Unsupported LUNs are okay */
		common->bad_lun_okay = 1;
		memset(buf, 0, 36);
		buf[0] = TYPE_NO_LUN;	/* Unsupported, no device-type */
		buf[4] = 31;		/* Additional length */
		return 36;
	}

	buf[0] = curlun->cdrom ? TYPE_ROM : TYPE_DISK;
	buf[1] = curlun->removable ? 0x80 : 0;
	buf[2] = 2;		/* ANSI SCSI level 2 */
	buf[3] = 2;		/* SCSI-2 INQUIRY data format */
	buf[4] = 31;		/* Additional length */
	buf[5] = 0;		/* No special options */
	buf[6] = 0;
	buf[7] = 0;
	memcpy(buf + 8, curlun->inquiry_string,
			sizeof(curlun->inquiry_string));
	return 36;
}

static int do_request_sense(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	u8		*buf = (u8 *) bh->buf;
	u32		sd, sdinfo;
	int		valid;

	if (!curlun) {		/* Unsupported LUNs are okay */
		common->bad_lun_okay = 1;
		sd = SS_LOGICAL_UNIT_NOT_SUPPORTED;
		sdinfo = 0;
		valid = 0;
	} else {
		sd = curlun->sense_data;
		sdinfo = curlun->sense_data_info;
		valid = curlun->info_valid << 7;
		curlun->sense_data = SS_NO_SENSE;
		curlun->sense_data_info = 0;
		curlun->info_valid = 0;
	}

	memset(buf, 0, 18);
	buf[0] = valid | 0x70;			/* Valid, current error */
	buf[2] = SK(sd);
	put_unaligned_be32(sdinfo, &buf[3]);	/* Sense information */
	buf[7] = 18 - 8;			/* Additional sense length */
	buf[12] = ASC(sd);
	buf[13] = ASCQ(sd);
	return 18;
}

static int do_read_capacity(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	u32		lba = get_unaligned_be32(&common->cmnd[2]);
	int		pmi = common->cmnd[8];
	u8		*buf = (u8 *)bh->buf;
	u32		max_lba;

	/* Check the PMI and LBA fields */
	if (pmi > 1 || (pmi == 0 && lba != 0)) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	if (curlun->num_sectors < 0x100000000ULL)
		max_lba = curlun->num_sectors - 1;
	else
		max_lba = 0xffffffff;
	put_unaligned_be32(max_lba, &buf[0]);		/* Max logical block */
	put_unaligned_be32(curlun->blksize, &buf[4]);	/* Block length */
	return 8;
}

static int do_read_capacity_16(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun  *curlun = common->curlun;
	u64		lba = get_unaligned_be64(&common->cmnd[2]);
	int		pmi = common->cmnd[14];
	u8		*buf = (u8 *)bh->buf;

	/* Check the PMI and LBA fields */
	if (pmi > 1 || (pmi == 0 && lba != 0)) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	put_unaligned_be64(curlun->num_sectors - 1, &buf[0]);
							/* Max logical block */
	put_unaligned_be32(curlun->blksize, &buf[8]);	/* Block length */

	/* It is safe to keep other fields zeroed */
	memset(&buf[12], 0, 32 - 12);
	return 32;
}

static int do_read_header(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	int		msf = common->cmnd[1] & 0x02;
	u32		lba = get_unaligned_be32(&common->cmnd[2]);
	u8		*buf = (u8 *)bh->buf;

	if (common->cmnd[1] & ~0x02) {		/* Mask away MSF */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}
	if (lba >= curlun->num_sectors) {
		curlun->sense_data = SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE;
		return -EINVAL;
	}

	memset(buf, 0, 8);
	buf[0] = 0x01;		/* 2048 bytes of user data, rest is EC */
	store_cdrom_address(&buf[4], msf, lba);
	return 8;
}

static int do_read_toc(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	int		msf = common->cmnd[1] & 0x02;
	int		start_track = common->cmnd[6];
	u8		*buf = (u8 *)bh->buf;

	if ((common->cmnd[1] & ~0x02) != 0 ||	/* Mask away MSF */
			start_track > 1) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	memset(buf, 0, 20);
	buf[1] = (20-2);		/* TOC data length */
	buf[2] = 1;			/* First track number */
	buf[3] = 1;			/* Last track number */
	buf[5] = 0x16;			/* Data track, copying allowed */
	buf[6] = 0x01;			/* Only track is number 1 */
	store_cdrom_address(&buf[8], msf, 0);

	buf[13] = 0x16;			/* Lead-out track is data */
	buf[14] = 0xAA;			/* Lead-out track number */
	store_cdrom_address(&buf[16], msf, curlun->num_sectors);
	return 20;
}

static int do_mode_sense(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	int		mscmnd = common->cmnd[0];
	u8		*buf = (u8 *) bh->buf;
	u8		*buf0 = buf;
	int		pc, page_code;
	int		changeable_values, all_pages;
	int		valid_page = 0;
	int		len, limit;

	if ((common->cmnd[1] & ~0x08) != 0) {	/* Mask away DBD */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}
	pc = common->cmnd[2] >> 6;
	page_code = common->cmnd[2] & 0x3f;
	if (pc == 3) {
		curlun->sense_data = SS_SAVING_PARAMETERS_NOT_SUPPORTED;
		return -EINVAL;
	}
	changeable_values = (pc == 1);
	all_pages = (page_code == 0x3f);

	/*
	 * Write the mode parameter header.  Fixed values are: default
	 * medium type, no cache control (DPOFUA), and no block descriptors.
	 * The only variable value is the WriteProtect bit.  We will fill in
	 * the mode data length later.
	 */
	memset(buf, 0, 8);
	if (mscmnd == MODE_SENSE) {
		buf[2] = (curlun->ro ? 0x80 : 0x00);		/* WP, DPOFUA */
		buf += 4;
		limit = 255;
	} else {			/* MODE_SENSE_10 */
		buf[3] = (curlun->ro ? 0x80 : 0x00);		/* WP, DPOFUA */
		buf += 8;
		limit = 65535;		/* Should really be FSG_BUFLEN */
	}

	/* No block descriptors */

	/*
	 * The mode pages, in numerical order.  The only page we support
	 * is the Caching page.
	 */
	if (page_code == 0x08 || all_pages) {
		valid_page = 1;
		buf[0] = 0x08;		/* Page code */
		buf[1] = 10;		/* Page length */
		memset(buf+2, 0, 10);	/* None of the fields are changeable */

		if (!changeable_values) {
			buf[2] = 0x04;	/* Write cache enable, */
					/* Read cache not disabled */
					/* No cache retention priorities */
			put_unaligned_be16(0xffff, &buf[4]);
					/* Don't disable prefetch */
					/* Minimum prefetch = 0 */
			put_unaligned_be16(0xffff, &buf[8]);
					/* Maximum prefetch */
			put_unaligned_be16(0xffff, &buf[10]);
					/* Maximum prefetch ceiling */
		}
		buf += 12;
	}

	/*
	 * Check that a valid page was requested and the mode data length
	 * isn't too long.
	 */
	len = buf - buf0;
	if (!valid_page || len > limit) {
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	/*  Store the mode data length */
	if (mscmnd == MODE_SENSE)
		buf0[0] = len - 1;
	else
		put_unaligned_be16(len - 2, buf0);
	return len;
}

static int do_start_stop(struct fsg_common *common)
{
	struct fsg_lun	*curlun = common->curlun;
	int		start;

	if (!curlun) {
		return -EINVAL;
	} else if (!curlun->removable) {
		curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	} else if ((common->cmnd[1] & ~0x01) != 0 || /* Mask away Immed */
		   (common->cmnd[4] & ~0x03) != 0) { /* Mask LoEj, Start */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	start = common->cmnd[4] & 0x01;

	/*
	 * Our emulation doesn't support mounting; the medium is
	 * available for use as soon as it is loaded.
	 */
	if (start)
		return 0;

	/* Are we allowed to unload the media? */
	if (curlun->prevent_medium_removal) {
		printf("usb gadget mass storage unload attempt prevented\n");
		curlun->sense_data = SS_MEDIUM_REMOVAL_PREVENTED;
		return -EINVAL;
	}

	return 0;
}

static int do_prevent_allow(struct fsg_common *common)
{
	struct fsg_lun	*curlun = common->curlun;
	int		prevent;

	if (!common->curlun) {
		return -EINVAL;
	} else if (!common->curlun->removable) {
		common->curlun->sense_data = SS_INVALID_COMMAND;
		return -EINVAL;
	}

	prevent = common->cmnd[4] & 0x01;
	if ((common->cmnd[4] & ~0x01) != 0) {	/* Mask away Prevent */
		curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
		return -EINVAL;
	}

	if (curlun->prevent_medium_removal && !prevent)
		fsg_lun_fsync_sub(curlun);
	curlun->prevent_medium_removal = prevent;
	return 0;
}

static int do_read_format_capacities(struct fsg_common *common,
			struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;
	u8		*buf = (u8 *) bh->buf;

	buf[0] = buf[1] = buf[2] = 0;
	buf[3] = 8;	/* Only the Current/Maximum Capacity Descriptor */
	buf += 4;

	put_unaligned_be32(curlun->num_sectors, &buf[0]);
						/* Number of blocks */
	put_unaligned_be32(curlun->blksize, &buf[4]);/* Block length */
	buf[4] = 0x02;				/* Current capacity */
	return 12;
}

static int do_mode_select(struct fsg_common *common, struct fsg_buffhd *bh)
{
	struct fsg_lun	*curlun = common->curlun;

	/* We don't support MODE SELECT */
	if (curlun)
		curlun->sense_data = SS_INVALID_COMMAND;
	return -EINVAL;
}

static int wedge_bulk_in_endpoint(struct fsg_dev *fsg)
{
	int	rc;

	rc = usb_ep_set_wedge(fsg->bulk_in);
	if (rc == -EAGAIN)
		printf("usb gadget mass storage delayed bulk-in endpoint wedge\n");
	while (rc != 0) {
		if (rc != -EAGAIN) {
			printf("ERROR: usb gadget mass storage usb_ep_set_wedge -> %d\n", rc);
			rc = 0;
			break;
		}

		/* Wait for a short time and then try again */
		msleep(100);
		rc = usb_ep_set_wedge(fsg->bulk_in);
	}
	return rc;
}

static int throw_away_data(struct fsg_common *common)
{
	struct fsg_buffhd	*bh, *bh2;
	u32			amount;
	int			rc;

	for (bh = common->next_buffhd_to_drain;
	     bh->state != BUF_STATE_EMPTY || common->usb_amount_left > 0;
	     bh = common->next_buffhd_to_drain) {

		/* Try to submit another request if we need one */
		bh2 = common->next_buffhd_to_fill;
		if (bh2->state == BUF_STATE_EMPTY &&
				common->usb_amount_left > 0) {
			amount = min(common->usb_amount_left, FSG_BUFLEN);

			/*
			 * Except at the end of the transfer, amount will be
			 * equal to the buffer size, which is divisible by
			 * the bulk-out maxpacket size.
			 */
			set_bulk_out_req_length(common, bh2, amount);
			if (!start_out_transfer(common, bh2))
				/* Dunno what to do if common->fsg is NULL */
				return -EIO;
			common->next_buffhd_to_fill = bh2->next;
			common->usb_amount_left -= amount;
			continue;
		}

		/* Wait for the data to be received */
		rc = sleep_thread(common, bh);
		if (rc)
			return rc;

		/* Throw away the data in a filled buffer */
		bh->state = BUF_STATE_EMPTY;
		common->next_buffhd_to_drain = bh->next;

		/* A short packet or an error ends everything */
		if (bh->outreq->actual < bh->bulk_out_intended_length ||
				bh->outreq->status != 0) {
			raise_exception(common, FSG_STATE_ABORT_BULK_OUT);
			return -EINTR;
		}
	}
	return 0;
}

static int finish_reply(struct fsg_common *common)
{
	struct fsg_buffhd	*bh = common->next_buffhd_to_fill;
	int			rc = 0;

	switch (common->data_dir) {
	case DATA_DIR_NONE:
		break;			/* Nothing to send */
	case DATA_DIR_UNKNOWN:
		break;

	/* All but the last buffer of data must have already been sent */
	case DATA_DIR_TO_HOST:
		if (common->data_size == 0) {
			/* Nothing to send */

		/* Don't know what to do if common->fsg is NULL */
		} else if (!fsg_is_set(common)) {
			rc = -EIO;

		/* If there's no residue, simply send the last buffer */
		} else if (common->residue == 0) {
			bh->inreq->zero = 0;
			if (!start_in_transfer(common, bh))
				return -EIO;
			common->next_buffhd_to_fill = bh->next;
		} else {
			bh->inreq->zero = 1;
			if (!start_in_transfer(common, bh))
				rc = -EIO;
			common->next_buffhd_to_fill = bh->next;
		}
		break;

	/*
	 * We have processed all we want from the data the host has sent.
	 * There may still be outstanding bulk-out requests.
	 */
	case DATA_DIR_FROM_HOST:
		if (common->residue == 0) {
			/* Nothing to receive */

		/* Did the host stop sending unexpectedly early? */
		} else if (common->short_packet_received) {
			raise_exception(common, FSG_STATE_ABORT_BULK_OUT);
			rc = -EINTR;
		} else {
			rc = throw_away_data(common);
		}
		break;
	}
	return rc;
}

static void send_status(struct fsg_common *common)
{
	struct fsg_lun		*curlun = common->curlun;
	struct fsg_buffhd	*bh;
	struct bulk_cs_wrap	*csw;
	int			rc;
	u8			status = US_BULK_STAT_OK;
	u32			sd, sdinfo = 0;

	/* Wait for the next buffer to become available */
	bh = common->next_buffhd_to_fill;
	rc = sleep_thread(common, bh);
	if (rc)
		return;

	if (curlun) {
		sd = curlun->sense_data;
		sdinfo = curlun->sense_data_info;
	} else if (common->bad_lun_okay)
		sd = SS_NO_SENSE;
	else
		sd = SS_LOGICAL_UNIT_NOT_SUPPORTED;

	if (common->phase_error) {
		printf("usb gadget mass storage sending phase-error status\n");
		status = US_BULK_STAT_PHASE;
		sd = SS_INVALID_COMMAND;
	} else if (sd != SS_NO_SENSE) {
		printf("usb gadget mass storage sending command-failure status\n");
		status = US_BULK_STAT_FAIL;
		printf("  sense data: SK x%02x, ASC x%02x, ASCQ x%02x;  info x%x\n",
				SK(sd), ASC(sd), ASCQ(sd), sdinfo);
	}

	/* Store and send the Bulk-only CSW */
	csw = (void *)bh->buf;

	csw->Signature = US_BULK_CS_SIGN;
	csw->Tag = common->tag;
	csw->Residue = common->residue;
	csw->Status = status;

	bh->inreq->length = US_BULK_CS_WRAP_LEN;
	bh->inreq->zero = 0;
	if (!start_in_transfer(common, bh))
		/* Don't know what to do if common->fsg is NULL */
		return;

	common->next_buffhd_to_fill = bh->next;
	return;
}


/*-------------------------------------------------------------------------*/

/*
 * Check whether the command is properly formed and whether its data size
 * and direction agree with the values we already have.
 */
static int check_command(struct fsg_common *common, int cmnd_size,
			 enum data_direction data_dir, unsigned int mask, const char *name)
{
	int			i;
	unsigned int		lun = common->cmnd[1] >> 5;
	struct fsg_lun		*curlun;

	/*
	 * We can't reply at all until we know the correct data direction
	 * and size.
	 */
	if (common->data_size_from_cmnd == 0)
		data_dir = DATA_DIR_NONE;
	if (common->data_size < common->data_size_from_cmnd) {
		/*
		 * Host data size < Device data size is a phase error.
		 * Carry out the command, but only transfer as much as
		 * we are allowed.
		 */
		common->data_size_from_cmnd = common->data_size;
		common->phase_error = 1;
	}
	common->residue = common->data_size;
	common->usb_amount_left = common->data_size;

	/* Conflicting data directions is a phase error */
	if (common->data_dir != data_dir && common->data_size_from_cmnd > 0) {
		common->phase_error = 1;
		return -EINVAL;
	}

	/* Verify the length of the command itself */
	if (cmnd_size != common->cmnd_size) {

		/*
		 * Special case workaround: There are plenty of buggy SCSI
		 * implementations. Many have issues with cbw->Length
		 * field passing a wrong command size. For those cases we
		 * always try to work around the problem by using the length
		 * sent by the host side provided it is at least as large
		 * as the correct command length.
		 * Examples of such cases would be MS-Windows, which issues
		 * REQUEST SENSE with cbw->Length == 12 where it should
		 * be 6, and xbox360 issuing INQUIRY, TEST UNIT READY and
		 * REQUEST SENSE with cbw->Length == 10 where it should
		 * be 6 as well.
		 */
		if (cmnd_size <= common->cmnd_size) {
			cmnd_size = common->cmnd_size;
		} else {
			common->phase_error = 1;
			return -EINVAL;
		}
	}

	/* Check that the LUN values are consistent */
	if (common->lun != lun)
		printf("usb gadget mass storage using LUN %u from CBW, not LUN %u from CDB\n",
		    common->lun, lun);

	/* Check the LUN */
	curlun = common->curlun;
	if (curlun) {
		if (common->cmnd[0] != REQUEST_SENSE) {
			curlun->sense_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	} else {
		common->bad_lun_okay = 0;

		/*
		 * INQUIRY and REQUEST SENSE commands are explicitly allowed
		 * to use unsupported LUNs; all others may not.
		 */
		if (common->cmnd[0] != INQUIRY &&
		    common->cmnd[0] != REQUEST_SENSE) {
			printf("usb gadget mass storage unsupported LUN %u\n", common->lun);
			return -EINVAL;
		}
	}

	/* Check that only command bytes listed in the mask are non-zero */
	common->cmnd[1] &= 0x1f;			/* Mask away the LUN */
	for (i = 1; i < cmnd_size; ++i) {
		if (common->cmnd[i] && !(mask & (1 << i))) {
			if (curlun)
				curlun->sense_data = SS_INVALID_FIELD_IN_CDB;
			return -EINVAL;
		}
	}

	return 0;
}

/* wrapper of check_command for data size in blocks handling */
static int check_command_size_in_blocks(struct fsg_common *common,
		int cmnd_size, enum data_direction data_dir,
		unsigned int mask, const char *name)
{
	if (common->curlun)
		common->data_size_from_cmnd = common->data_size_from_cmnd * common->curlun->blksize;
	return check_command(common, cmnd_size, data_dir, mask, name);
}

static int do_scsi_command(struct fsg_common *common)
{
	struct fsg_buffhd	*bh;
	int			rc;
	int			reply = -EINVAL;
	int			i;
	static char		unknown[16];

	/* Wait for the next buffer to become available for data or status */
	bh = common->next_buffhd_to_fill;
	common->next_buffhd_to_drain = bh;
	rc = sleep_thread(common, bh);
	if (rc)
		return rc;

	common->phase_error = 0;
	common->short_packet_received = 0;

	switch (common->cmnd[0]) {

	case INQUIRY:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_TO_HOST,
				      (1<<4), "INQUIRY");
		if (reply == 0)
			reply = do_inquiry(common, bh);
		break;

	case MODE_SELECT:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_FROM_HOST,
				      (1<<1) | (1<<4), "MODE SELECT(6)");
		if (reply == 0)
			reply = do_mode_select(common, bh);
		break;

	case MODE_SELECT_10:
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_FROM_HOST,
				      (1<<1) | (3<<7), "MODE SELECT(10)");
		if (reply == 0)
			reply = do_mode_select(common, bh);
		break;

	case MODE_SENSE:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_TO_HOST,
				      (1<<1) | (1<<2) | (1<<4), "MODE SENSE(6)");
		if (reply == 0)
			reply = do_mode_sense(common, bh);
		break;

	case MODE_SENSE_10:
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (1<<1) | (1<<2) | (3<<7), "MODE SENSE(10)");
		if (reply == 0)
			reply = do_mode_sense(common, bh);
		break;

	case ALLOW_MEDIUM_REMOVAL:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 6, DATA_DIR_NONE,
				      (1<<4), "PREVENT-ALLOW MEDIUM REMOVAL");
		if (reply == 0)
			reply = do_prevent_allow(common);
		break;

	case READ_6:
		i = common->cmnd[4];
		common->data_size_from_cmnd = (i == 0) ? 256 : i;
		reply = check_command_size_in_blocks(common, 6,
				      DATA_DIR_TO_HOST,
				      (7<<1) | (1<<4), "READ(6)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_10:
		common->data_size_from_cmnd =
				get_unaligned_be16(&common->cmnd[7]);
		reply = check_command_size_in_blocks(common, 10,
				      DATA_DIR_TO_HOST,
				      (1<<1) | (0xf<<2) | (3<<7), "READ(10)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_12:
		common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[6]);
		reply = check_command_size_in_blocks(common, 12,
				      DATA_DIR_TO_HOST,
				      (1<<1) | (0xf<<2) | (0xf<<6), "READ(12)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_16:
		common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[10]);
		reply = check_command_size_in_blocks(common, 16,
				      DATA_DIR_TO_HOST,
				      (1<<1) | (0xff<<2) | (0xf<<10), "READ(16)");
		if (reply == 0)
			reply = do_read(common);
		break;

	case READ_CAPACITY:
		common->data_size_from_cmnd = 8;
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (0xf<<2) | (1<<8), "READ CAPACITY");
		if (reply == 0)
			reply = do_read_capacity(common, bh);
		break;

	case READ_HEADER:
		if (!common->curlun || !common->curlun->cdrom)
			goto unknown_cmnd;
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (3<<7) | (0x1f<<1), "READ HEADER");
		if (reply == 0)
			reply = do_read_header(common, bh);
		break;

	case READ_TOC:
		if (!common->curlun || !common->curlun->cdrom)
			goto unknown_cmnd;
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (7<<6) | (1<<1), "READ TOC");
		if (reply == 0)
			reply = do_read_toc(common, bh);
		break;

	case READ_FORMAT_CAPACITIES:
		common->data_size_from_cmnd =
			get_unaligned_be16(&common->cmnd[7]);
		reply = check_command(common, 10, DATA_DIR_TO_HOST,
				      (3<<7), "READ FORMAT CAPACITIES");
		if (reply == 0)
			reply = do_read_format_capacities(common, bh);
		break;

	case REQUEST_SENSE:
		common->data_size_from_cmnd = common->cmnd[4];
		reply = check_command(common, 6, DATA_DIR_TO_HOST,
				      (1<<4), "REQUEST SENSE");
		if (reply == 0)
			reply = do_request_sense(common, bh);
		break;

	case SERVICE_ACTION_IN_16:
		switch (common->cmnd[1] & 0x1f) {

		case SAI_READ_CAPACITY_16:
			common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[10]);
			reply = check_command(common, 16, DATA_DIR_TO_HOST,
					      (1<<1) | (0xff<<2) | (0xf<<10) |
					      (1<<14), "READ CAPACITY(16)");
			if (reply == 0)
				reply = do_read_capacity_16(common, bh);
			break;

		default:
			goto unknown_cmnd;
		}
		break;

	case START_STOP:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 6, DATA_DIR_NONE,
				      (1<<1) | (1<<4), "START-STOP UNIT");
		if (reply == 0)
			reply = do_start_stop(common);
		break;

	case SYNCHRONIZE_CACHE:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 10, DATA_DIR_NONE,
				      (0xf<<2) | (3<<7), "SYNCHRONIZE CACHE");
		if (reply == 0)
			reply = do_synchronize_cache(common);
		break;

	case TEST_UNIT_READY:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 6, DATA_DIR_NONE,
				0, "TEST UNIT READY");
		break;

	/*
	 * Although optional, this command is used by MS-Windows.  We
	 * support a minimal version: BytChk must be 0.
	 */
	case VERIFY:
		common->data_size_from_cmnd = 0;
		reply = check_command(common, 10, DATA_DIR_NONE,
				      (1<<1) | (0xf<<2) | (3<<7), "VERIFY");
		if (reply == 0)
			reply = do_verify(common);
		break;

	case WRITE_6:
		i = common->cmnd[4];
		common->data_size_from_cmnd = (i == 0) ? 256 : i;
		reply = check_command_size_in_blocks(common, 6,
				      DATA_DIR_FROM_HOST,
				      (7<<1) | (1<<4), "WRITE(6)");
		if (reply == 0)
			reply = do_write(common);
		break;

	case WRITE_10:
		common->data_size_from_cmnd =
				get_unaligned_be16(&common->cmnd[7]);
		reply = check_command_size_in_blocks(common, 10,
				      DATA_DIR_FROM_HOST,
				      (1<<1) | (0xf<<2) | (3<<7), "WRITE(10)");
		if (reply == 0)
			reply = do_write(common);
		break;

	case WRITE_12:
		common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[6]);
		reply = check_command_size_in_blocks(common, 12,
				      DATA_DIR_FROM_HOST,
				      (1<<1) | (0xf<<2) | (0xf<<6), "WRITE(12)");
		if (reply == 0)
			reply = do_write(common);
		break;

	case WRITE_16:
		common->data_size_from_cmnd =
				get_unaligned_be32(&common->cmnd[10]);
		reply = check_command_size_in_blocks(common, 16,
				      DATA_DIR_FROM_HOST,
				      (1<<1) | (0xff<<2) | (0xf<<10), "WRITE(16)");
		if (reply == 0)
			reply = do_write(common);
		break;

	/*
	 * Some mandatory commands that we recognize but don't implement.
	 * They don't mean much in this setting.  It's left as an exercise
	 * for anyone interested to implement RESERVE and RELEASE in terms
	 * of Posix locks.
	 */
	case FORMAT_UNIT:
	case RELEASE:
	case RESERVE:
	case SEND_DIAGNOSTIC:

	default:
unknown_cmnd:
		common->data_size_from_cmnd = 0;
		sprintf(unknown, "Unknown x%02x", common->cmnd[0]);
		reply = check_command(common, common->cmnd_size,
				      DATA_DIR_UNKNOWN, ~0, unknown);
		if (reply == 0) {
			common->curlun->sense_data = SS_INVALID_COMMAND;
			reply = -EINVAL;
		}
		break;
	}

	if (reply == -EINTR || exception_in_progress(common))
		return -EINTR;

	/* Set up the single reply buffer for finish_reply() */
	if (reply == -EINVAL)
		reply = 0;		/* Error reply length */
	if (reply >= 0 && common->data_dir == DATA_DIR_TO_HOST) {
		reply = min((u32)reply, common->data_size_from_cmnd);
		bh->inreq->length = reply;
		bh->state = BUF_STATE_FULL;
		common->residue -= reply;
	}				/* Otherwise it's already set */

	return 0;
}


/*-------------------------------------------------------------------------*/

static int received_cbw(struct fsg_dev *fsg, struct fsg_buffhd *bh)
{
	struct usb_request	*req = bh->outreq;
	struct bulk_cb_wrap	*cbw = req->buf;
	struct fsg_common	*common = &fsg->common;

	/* Was this a real packet?  Should it be ignored? */
	if (req->status || (fsg->atomic_bitflags & (1 << IGNORE_BULK_OUT)))
		return -EINVAL;

	/* Is the CBW valid? */
	if (req->actual != US_BULK_CB_WRAP_LEN ||
			cbw->Signature != US_BULK_CB_SIGN) {
		printf("usb gadget mass storage invalid CBW: len %u sig 0x%x\n",
				req->actual, cbw->Signature);

		wedge_bulk_in_endpoint(fsg);
		fsg->atomic_bitflags |= 1 << IGNORE_BULK_OUT;
		return -EINVAL;
	}

	/* Is the CBW meaningful? */
	if (cbw->Lun >= ARRAY_SIZE(common->luns) ||
	    cbw->Flags & ~US_BULK_FLAG_IN || cbw->Length <= 0 ||
	    cbw->Length > MAX_COMMAND_SIZE) {
		printf("usb gadget mass storage non-meaningful CBW: lun = %u, flags = 0x%x, cmdlen %u\n",
				cbw->Lun, cbw->Flags, cbw->Length);
		return -EINVAL;
	}

	/* Save the command for later */
	common->cmnd_size = cbw->Length;
	memcpy(common->cmnd, cbw->CDB, common->cmnd_size);
	if (cbw->Flags & US_BULK_FLAG_IN)
		common->data_dir = DATA_DIR_TO_HOST;
	else
		common->data_dir = DATA_DIR_FROM_HOST;
	common->data_size = cbw->DataTransferLength;
	if (common->data_size == 0)
		common->data_dir = DATA_DIR_NONE;
	common->lun = cbw->Lun;
	if (common->lun < ARRAY_SIZE(common->luns))
		common->curlun = common->luns[common->lun];
	else
		common->curlun = NULL;
	common->tag = cbw->Tag;
	return 0;
}

static int get_next_command(struct fsg_common *common)
{
	struct fsg_buffhd	*bh;
	int			rc = 0;

	/* Wait for the next buffer to become available */
	bh = common->next_buffhd_to_fill;
	rc = sleep_thread(common, bh);
	if (rc)
		return rc;

	/* Queue a request to read a Bulk-only CBW */
	set_bulk_out_req_length(common, bh, US_BULK_CB_WRAP_LEN);
	if (!start_out_transfer(common, bh))
		/* Don't know what to do if common->fsg is NULL */
		return -EIO;

	/*
	 * We will drain the buffer in software, which means we
	 * can reuse it for the next filling.  No need to advance
	 * next_buffhd_to_fill.
	 */

	/* Wait for the CBW to arrive */
	rc = sleep_thread(common, bh);
	if (rc)
		return rc;

	rc = fsg_is_set(common) ? received_cbw(common->fsg, bh) : -EIO;
	bh->state = BUF_STATE_EMPTY;

	return rc;
}


/*-------------------------------------------------------------------------*/

static int alloc_request(struct fsg_common *common, struct usb_ep *ep,
		struct usb_request **preq)
{
	*preq = usb_ep_alloc_request(ep);
	if (*preq)
		return 0;
	printf("usb gadget mass storage can't allocate request for %s\n", ep->name);
	return -ENOMEM;
}

/* Reset interface setting and re-init endpoint state (toggle etc). */
static int do_set_interface(struct fsg_common *common, struct fsg_dev *new_fsg)
{
	struct fsg_dev *fsg;
	int i, rc = 0;

reset:
	/* Deallocate the requests */
	if (common->fsg) {
		fsg = common->fsg;

		for (i = 0; i < CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS; ++i) {
			struct fsg_buffhd *bh = &common->buffhds[i];

			if (bh->inreq) {
				usb_ep_free_request(fsg->bulk_in, bh->inreq);
				bh->inreq = NULL;
			}
			if (bh->outreq) {
				usb_ep_free_request(fsg->bulk_out, bh->outreq);
				bh->outreq = NULL;
			}
		}

		/* Disable the endpoints */
		if (fsg->bulk_in_enabled) {
			usb_ep_disable(fsg->bulk_in);
			fsg->bulk_in_enabled = 0;
		}
		if (fsg->bulk_out_enabled) {
			usb_ep_disable(fsg->bulk_out);
			fsg->bulk_out_enabled = 0;
		}

		common->fsg = NULL;
	}

	common->running = 0;
	if (!new_fsg || rc)
		return rc;

	common->fsg = new_fsg;
	fsg = common->fsg;

	/* Enable the endpoints */
	rc = config_ep_by_speed(common->gadget, &(fsg->function), fsg->bulk_in);
	if (rc)
		goto reset;
	rc = usb_ep_enable(fsg->bulk_in);
	if (rc)
		goto reset;
	fsg->bulk_in->driver_data = common;
	fsg->bulk_in_enabled = 1;

	rc = config_ep_by_speed(common->gadget, &(fsg->function),
				fsg->bulk_out);
	if (rc)
		goto reset;
	rc = usb_ep_enable(fsg->bulk_out);
	if (rc)
		goto reset;
	fsg->bulk_out->driver_data = common;
	fsg->bulk_out_enabled = 1;
	common->bulk_out_maxpacket = usb_endpoint_maxp(fsg->bulk_out->desc);
	fsg->atomic_bitflags &= ~(1 << IGNORE_BULK_OUT);

	/* Allocate the requests */
	for (i = 0; i < CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS; ++i) {
		struct fsg_buffhd	*bh = &common->buffhds[i];

		rc = alloc_request(common, fsg->bulk_in, &bh->inreq);
		if (rc)
			goto reset;
		rc = alloc_request(common, fsg->bulk_out, &bh->outreq);
		if (rc)
			goto reset;
		bh->inreq->buf = bh->outreq->buf = bh->buf;
		bh->inreq->context = bh->outreq->context = bh;
		bh->inreq->complete = bulk_in_complete;
		bh->outreq->complete = bulk_out_complete;
	}

	common->running = 1;

	return rc;
}


/****************************** ALT CONFIGS ******************************/

static int fsg_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct fsg_dev *fsg = fsg_from_func(f);

	__raise_exception(&fsg->common, FSG_STATE_CONFIG_CHANGE, fsg);
	return USB_GADGET_DELAYED_STATUS;
}

static void fsg_disable(struct usb_function *f)
{
	struct fsg_dev *fsg = fsg_from_func(f);

	/* Disable the endpoints */
	if (fsg->bulk_in_enabled) {
		usb_ep_disable(fsg->bulk_in);
		fsg->bulk_in_enabled = 0;
	}
	if (fsg->bulk_out_enabled) {
		usb_ep_disable(fsg->bulk_out);
		fsg->bulk_out_enabled = 0;
	}

	__raise_exception(&fsg->common, FSG_STATE_CONFIG_CHANGE, NULL);
}


/*-------------------------------------------------------------------------*/

static void handle_exception(struct fsg_common *common)
{
	int			i;
	int connect_flag;
	struct fsg_buffhd	*bh;
	enum fsg_state		old_state;
	struct fsg_lun		*curlun;
	unsigned int		exception_req_tag;
	struct fsg_dev		*new_fsg;
	int state_change = 0;

	/* Cancel all the pending transfers */
	if (common->fsg) {
		for (i = 0; i < CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS; ++i) {
			bh = &common->buffhds[i];
			if (bh->state == BUF_STATE_SENDING)
				usb_ep_dequeue(common->fsg->bulk_in, bh->inreq);
			if (bh->state == BUF_STATE_RECEIVING)
				usb_ep_dequeue(common->fsg->bulk_out, bh->outreq);
		}
		/* Clear out the controller's fifos */
		if (common->fsg->bulk_in_enabled)
			usb_ep_fifo_flush(common->fsg->bulk_in);
		if (common->fsg->bulk_out_enabled)
			usb_ep_fifo_flush(common->fsg->bulk_out);
	}

	/*
	 * Reset the I/O buffer states and pointers, the SCSI
	 * state, and the exception.  Then invoke the handler.
	 */
	usb_spin_lock_irq(&common->lock);

	for (i = 0; i < CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS; ++i) {
		bh = &common->buffhds[i];
		bh->state = BUF_STATE_EMPTY;
	}
	common->next_buffhd_to_fill = &common->buffhds[0];
	common->next_buffhd_to_drain = &common->buffhds[0];
	exception_req_tag = common->exception_req_tag;
	new_fsg = common->exception_arg;
	old_state = common->state;
	common->state = FSG_STATE_NORMAL;

	if (old_state != FSG_STATE_ABORT_BULK_OUT) {
		for (i = 0; i < ARRAY_SIZE(common->luns); ++i) {
			curlun = common->luns[i];
			if (!curlun)
				continue;
			curlun->prevent_medium_removal = 0;
			curlun->sense_data = SS_NO_SENSE;
			curlun->sense_data_info = 0;
			curlun->info_valid = 0;
		}
	}
	usb_spin_unlock_irq(&common->lock);

	/* Carry out any extra actions required for the exception */
	switch (old_state) {
	case FSG_STATE_NORMAL:
		break;

	case FSG_STATE_ABORT_BULK_OUT:
		send_status(common);
		break;

	case FSG_STATE_PROTOCOL_RESET:
		/*
		 * In case we were forced against our will to halt a
		 * bulk endpoint, clear the halt now.  (The SuperH UDC
		 * requires this.)
		 */
		if (!fsg_is_set(common))
			break;

		if (common->fsg->atomic_bitflags & (1 << IGNORE_BULK_OUT)) {
			common->fsg->atomic_bitflags &= ~(1 << IGNORE_BULK_OUT);
			usb_ep_clear_halt(common->fsg->bulk_in);
		}

		if (common->ep0_req_tag == exception_req_tag)
			ep0_queue(common);	/* Complete the status stage */

		break;

	case FSG_STATE_CONFIG_CHANGE:
		do_set_interface(common, new_fsg);
		connect_flag = new_fsg ? 1 : 0;
		usb_spin_lock_irq(&common->lock);
		if (connect_flag != common->connect_flag) {
			common->connect_flag = connect_flag;
			state_change = 1;
		}
		usb_spin_unlock_irq(&common->lock);

		if (state_change) {
			if (common->connect_cb)
				common->connect_cb(connect_flag);
			state_change = 0;
		}

		if (new_fsg)
			usb_composite_setup_continue(common->cdev);
		break;

	case FSG_STATE_EXIT:
		do_set_interface(common, NULL);		/* Free resources */
		connect_flag = 0;
		usb_spin_lock_irq(&common->lock);
		if (connect_flag != common->connect_flag) {
			common->connect_flag = connect_flag;
			state_change = 1;
		}
		common->state = FSG_STATE_TERMINATED;	/* Stop the thread */
		usb_spin_unlock_irq(&common->lock);

		if (state_change) {
			if (common->connect_cb)
				common->connect_cb(connect_flag);
			state_change = 0;
		}

		break;

	case FSG_STATE_TERMINATED:
		break;
	}
}


/*-------------------------------------------------------------------------*/
static void fsg_main_thread(void *common_)
{
	struct fsg_common *common = common_;

	/* The main loop */
	while (common->state != FSG_STATE_TERMINATED) {
		if (exception_in_progress(common)) {
			handle_exception(common);
			continue;
		}

		if (!common->running) {
			sleep_thread(common, NULL);
			continue;
		}

		if (get_next_command(common) || exception_in_progress(common))
			continue;
		if (do_scsi_command(common) || exception_in_progress(common))
			continue;
		if (finish_reply(common) || exception_in_progress(common))
			continue;
		send_status(common);
	}

	usb_spin_lock_irq(&common->lock);
	common->thread_task = NULL;
	usb_spin_unlock_irq(&common->lock);

	/* Let fsg_unbind() know the thread has exited */
	thread_waiter_wakeup(&common->thread_notifier);
}

/****************************** FSG COMMON ******************************/

static void _fsg_common_free_buffers(struct fsg_buffhd *buffhds, unsigned n)
{
	if (buffhds) {
		struct fsg_buffhd *bh = buffhds;
		while (n--) {
			free(bh->buf);
			++bh;
		}
		free(buffhds);
	}
}

int fsg_common_set_num_buffers(struct fsg_common *common)
{
	int i;
	struct fsg_buffhd *bh, *buffhds;
	unsigned int n = CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS;

	buffhds = calloc(n, sizeof(*buffhds));
	if (!buffhds)
		return -ENOMEM;

	/* Data buffers cyclic list */
	bh = buffhds;
	i = n;
	goto buffhds_first_it;
	do {
		bh->next = bh + 1;
		++bh;
buffhds_first_it:
		bh->buf = memalign(cache_line_size(), FSG_BUFLEN);
		if (!bh->buf)
			goto error_release;
	} while (--i);
	bh->next = buffhds;


	common->buffhds = buffhds;

	return 0;

error_release:
	_fsg_common_free_buffers(buffhds, n - i);

	return -ENOMEM;
}

static void _fsg_common_remove_luns(struct fsg_common *common, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		if (common->luns[i]) {
			free(common->luns[i]);
			common->luns[i] = NULL;
		}
}

void fsg_common_remove_luns(struct fsg_common *common)
{
	_fsg_common_remove_luns(common, ARRAY_SIZE(common->luns));
}

void fsg_common_free_buffers(struct fsg_common *common)
{
	_fsg_common_free_buffers(common->buffhds, CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS);
	common->buffhds = NULL;
}

int fsg_common_create_lun(struct fsg_common *common, struct fsg_lun_config *cfg,
			  unsigned int id)
{
	struct fsg_lun *lun;

	if (id >= ARRAY_SIZE(common->luns))
		return -ENODEV;

	if (common->luns[id])
		return -EBUSY;

	lun = malloc(sizeof(*lun));
	if (!lun)
		return -ENOMEM;

	memset(lun, 0, sizeof(*lun));
	lun->cdrom = !!cfg->cdrom;
	lun->ro = cfg->cdrom || cfg->ro;
	lun->removable = !!cfg->removable;

	lun->blksize = cfg->block_size;
	lun->num_sectors = cfg->num_sectors;
	lun->file_length = cfg->block_size * cfg->num_sectors;
	lun->read_callback = cfg->read_callback;
	lun->write_callback = cfg->write_callback;
	lun->sync_callback = cfg->sync_callback;

	snprintf(lun->inquiry_string, sizeof(lun->inquiry_string),
		 "%-8s%-16s%04x", cfg->vendor_name ?: "Ingenic",
		 /* Assume product name dependent on the first LUN */
		 cfg->product_name ?: (lun->cdrom
		     ? "File-CD Gadget"
		     : "File-Stor Gadget"),
		 cfg->product_version);

	common->luns[id] = lun;

	printf("USB MASS STORAGE LUN%d: %s%s%s\n", id,
	      lun->removable ? "removable " : "",
	      lun->ro ? "read only " : "",
	      lun->cdrom ? "CD-ROM " : "");

	return 0;
}

int fsg_common_create_luns(struct fsg_common *common, struct fsg_config *cfg)
{
	int i, rc;

	fsg_common_remove_luns(common);

	for (i = 0; i < cfg->nluns; ++i) {
		rc = fsg_common_create_lun(common, &cfg->luns[i], i);
		if (rc)
			goto fail;
	}

	return 0;

fail:
	_fsg_common_remove_luns(common, i);
	return rc;
}

static int fsg_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct fsg_common	*common = &fsg->common;
	int			i;
	struct usb_ep		*ep;
	int			ret;
	struct usb_string *us;

	/* Don't allow to bind if we don't have at least one LUN */
	ret = _fsg_common_get_max_lun(common);
	if (ret < 0) {
		printf("usb gadget mass storage there should be at least one LUN.\n");
		return -EINVAL;
	}

	common->gadget = cdev->gadget;
	common->ep0 = cdev->gadget->ep0;
	common->ep0req = cdev->req;
	common->cdev = cdev;

	us = usb_gstrings_attach(cdev, fsg_strings_array,
				 ARRAY_SIZE(fsg_strings));
	if (IS_ERR(us))
		return PTR_ERR(us);

	fsg_intf_desc.iInterface = us[FSG_STRING_INTERFACE].id;

	thread_waiter_init(&common->thread_notifier);
	common->state = FSG_STATE_NORMAL;
	common->thread_task = thread_create("file-storage", 4096, fsg_main_thread, common);

	fsg->gadget = cdev->gadget;

	/* New interface */
	i = usb_interface_id(c, f);
	if (i < 0)
		goto fail;
	fsg_intf_desc.bInterfaceNumber = i;
	fsg->interface_number = i;

	/* Find all the endpoints we will use */
	ep = usb_ep_autoconfig(cdev->gadget, &fsg_fs_bulk_in_desc);
	if (!ep)
		goto autoconf_fail;
	fsg->bulk_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, &fsg_fs_bulk_out_desc);
	if (!ep)
		goto autoconf_fail;
	fsg->bulk_out = ep;

	/* Assume endpoint addresses are the same for both speeds */
	fsg_hs_bulk_in_desc.bEndpointAddress = fsg_fs_bulk_in_desc.bEndpointAddress;
	fsg_hs_bulk_out_desc.bEndpointAddress = fsg_fs_bulk_out_desc.bEndpointAddress;

	ret = usb_assign_descriptors(f, fsg_fs_function, fsg_hs_function);
	if (ret)
		goto autoconf_fail;

	return 0;

autoconf_fail:
	printf("usb gadget mass storage unable to autoconfigure all endpoints\n");
	i = -ENODEV;
fail:
	/* terminate the thread */
	if (common->state != FSG_STATE_TERMINATED) {
		raise_exception(common, FSG_STATE_EXIT);
		while (common->state != FSG_STATE_TERMINATED)
			thread_waiter_wait(&common->thread_notifier);
	}
	return i;
}

static void fsg_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct fsg_dev		*fsg = fsg_from_func(f);
	struct fsg_common	*common = &fsg->common;

	if (common->state != FSG_STATE_TERMINATED) {
		raise_exception(common, FSG_STATE_EXIT);
		while (common->state != FSG_STATE_TERMINATED)
			thread_waiter_wait(&common->thread_notifier);
	}

	usb_free_all_descriptors(&fsg->function);
}

void mass_storage_device_free(struct usb_function *f)
{
	struct fsg_dev *fsg = container_of(f, struct fsg_dev, function);

	fsg_common_remove_luns(&fsg->common);
	fsg_common_free_buffers(&fsg->common);
	free(fsg);
}

struct usb_function *mass_storage_device_alloc(struct fsg_config *config)
{
	int status;
	struct fsg_dev *fsg;

	fsg = malloc(sizeof(*fsg));
	if (!fsg)
		return ERR_PTR(-ENOMEM);

	memset(fsg, 0, sizeof(*fsg));

	/* fsg common setup */
	spin_lock_init(&fsg->common.lock);
	fsg->common.state = FSG_STATE_TERMINATED;
	fsg->common.connect_cb = config->connect_cb;

	status = fsg_common_set_num_buffers(&fsg->common);
	if (status)
		goto fail_set_num_buffers;

	status = fsg_common_create_luns(&fsg->common, config);
	if (status)
		goto fail_create_luns;

	fsg->function.name	= FSG_DRIVER_DESC;
	fsg->function.bind	= fsg_bind;
	fsg->function.unbind	= fsg_unbind;
	fsg->function.setup	= fsg_setup;
	fsg->function.set_alt	= fsg_set_alt;
	fsg->function.disable	= fsg_disable;

	return &fsg->function;

fail_create_luns:
	fsg_common_free_buffers(&fsg->common);
fail_set_num_buffers:
	free(fsg);
	return ERR_PTR(-ENOMEM);
}