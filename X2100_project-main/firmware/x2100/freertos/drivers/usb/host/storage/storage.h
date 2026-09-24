#ifndef _USB_STORAGE_H_
#define _USB_STORAGE_H_

#include <common.h>
#include <os.h>

#include <usb/host_mass_storage.h>

#include "../usb.h"
#include "scsi.h"

#define USB_STORAGE "usb-storage: "

// #define US_DEBUGP	printf
#define US_DEBUGP(x...)

enum data_direction {
	DATA_DIR_UNKNOWN = 0,
	DATA_DIR_FROM_HOST,
	DATA_DIR_TO_HOST,
	DATA_DIR_NONE
};

#define SCSI_SENSE_BUFFERSIZE 	96
#define SCSI_INQUIRY_BUFFERSIZE 	36
#define SCSI_CAPACITY_BUFFERSIZE	8
#define SCSI_MODE_SENSE_BUFFERSIZE	4

struct us_data;

struct scsi_cmnd {
	unsigned int lun;

#define MAX_COMMAND_SIZE	16
	unsigned char cmnd[MAX_COMMAND_SIZE];
	unsigned char cmd_len;

	unsigned request_bufflen;	/* Actual request size */
	void *request_buffer;		/* Actual requested buffer */
	enum data_direction data_direction;

	unsigned underflow;	/* Return error if less than
				   this amount is transferred */

	int resid;		/* Number of bytes requested to be
				   transferred less actual number
				   transferred (0 if not supported) */

	unsigned char sense_buffer[SCSI_SENSE_BUFFERSIZE];		/* obtained by REQUEST SENSE
						 * when CHECK CONDITION is
						 * received on original command
						 * (auto-sense) */

	int result;		/* Status code from lower level driver */
};

/*
 * Unusual device list definitions
 */

struct us_unusual_dev {
	const char* vendorName;
	const char* productName;
	u8  useProtocol;
	u8  useTransport;
	unsigned int flags;
};

/*
 * Static flag definitions.  We use this roundabout technique so that the
 * proc_info() routine can automatically display a message for each flag.
 */
#define US_DO_ALL_FLAGS						\
	US_FLAG(SINGLE_LUN,	0x00000001)			\
		/* allow access to only LUN 0 */		\
	US_FLAG(FIX_INQUIRY,	0x00000002)			\
		/* INQUIRY response needs faking */		\
	US_FLAG(IGNORE_RESIDUE,	0x00000004)			\
		/* reported residue is wrong */			\

#define US_FLAG(name, value)	US_FL_##name = value ,
enum { US_DO_ALL_FLAGS };
#undef US_FLAG

/* Dynamic flag definitions: used in set_bit() etc. */
#define US_FLIDX_DISCONNECTING	21  /* 0x00200000  disconnect in progress */

#define USB_STOR_STRING_LEN 32

/*
 * We provide a DMA-mapped I/O buffer for use with small USB transfers.
 * It turns out that CB[I] needs a 12-byte buffer and Bulk-only needs a
 * 31-byte buffer.  But Freecom needs a 64-byte buffer, so that's the
 * size we'll allocate.
 */

#define US_IOBUF_SIZE		64	/* Size of the DMA-mapped I/O buffer */

typedef int (*trans_cmnd)(struct scsi_cmnd *, struct us_data*);
typedef int (*trans_reset)(struct us_data*);
typedef void (*proto_cmnd)(struct scsi_cmnd*, struct us_data*);

/* we allocate one of these for every device that we remember */
struct us_data {
	/* The device we're working with
	 * It's important to note:
	 *    (o) you must hold dev_mutex to change pusb_dev
	 */
	struct mutex	dev_mutex;	 /* protect pusb_dev */
	struct usb_device	*pusb_dev;	 /* this usb_device */
	struct usb_interface	*pusb_intf;	 /* this interface */
	struct us_unusual_dev   *unusual_dev;	 /* device-filter entry     */
	unsigned int		flags;		 /* from filter initially */
	unsigned int		send_bulk_pipe;	 /* cached pipe values */
	unsigned int		recv_bulk_pipe;
	unsigned int		send_ctrl_pipe;
	unsigned int		recv_ctrl_pipe;
	unsigned int		recv_intr_pipe;

	/* information about the device */
	char			*transport_name;
	char			*protocol_name;
	u32			bcs_signature;
	u8			subclass;
	u8			protocol;

#define USB_STORAGE_MAX_LUNS	8
	u8			max_lun;
	struct usb_storage_device storage_device[USB_STORAGE_MAX_LUNS];

	u8			ifnum;		 /* interface number   */
	u8			ep_bInterval;	 /* interrupt interval */

	/* function pointers for this device */
	trans_cmnd		transport;	 /* transport function	   */
	trans_reset		transport_reset; /* transport device reset */
	proto_cmnd		proto_handler;	 /* protocol handler	   */

	/* control and bulk communications data */
	struct urb		*current_urb;	 /* USB requests	 */
	struct usb_ctrlrequest	*cr;		 /* control requests	 */
	unsigned char		*iobuf;		 /* I/O buffer		 */
	dma_addr_t		cr_dma;		 /* buffer DMA addresses */
	dma_addr_t		iobuf_dma;

	thread_waiter_t exit_wait;

	/* SCSI interfaces */
	unsigned int		tag;		 /* current dCBWTag	*/
	struct scsi_cmnd scmd;
};

/* Function to fill an inquiry response. See usb.c for details */
extern void fill_inquiry_response(struct us_data *us, struct scsi_cmnd *srb,
	unsigned char *data, unsigned int data_len);

#endif
