#ifndef USB_F_MASS_STORAGE_H
#define USB_F_MASS_STORAGE_H

#include <usb/gadget_mass_storage.h>
#include "../composite.h"

#define CONFIG_USB_GADGET_STORAGE_NUM_BUFFERS	2

/* Length of a SCSI Command Data Block */
#define MAX_COMMAND_SIZE	16

/* SCSI Sense Key/Additional Sense Code/ASC Qualifier values */
#define SS_NO_SENSE				0
#define SS_COMMUNICATION_FAILURE		0x040800
#define SS_INVALID_COMMAND			0x052000
#define SS_INVALID_FIELD_IN_CDB			0x052400
#define SS_LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE	0x052100
#define SS_LOGICAL_UNIT_NOT_SUPPORTED		0x052500
#define SS_MEDIUM_NOT_PRESENT			0x023a00
#define SS_MEDIUM_REMOVAL_PREVENTED		0x055302
#define SS_NOT_READY_TO_READY_TRANSITION	0x062800
#define SS_RESET_OCCURRED			0x062900
#define SS_SAVING_PARAMETERS_NOT_SUPPORTED	0x053900
#define SS_UNRECOVERED_READ_ERROR		0x031100
#define SS_WRITE_ERROR				0x030c02
#define SS_WRITE_PROTECTED			0x072700

#define SK(x)		((u8) ((x) >> 16))	/* Sense Key byte, etc. */
#define ASC(x)		((u8) ((x) >> 8))
#define ASCQ(x)		((u8) (x))

/*
 * Vendor (8 chars), product (16 chars), release (4 hexadecimal digits) and NUL
 * byte
 */
#define INQUIRY_STRING_LEN (8 + 16 + 4 + 1)

struct fsg_lun {
	fsg_read_callback_t read_callback;
	fsg_write_callback_t write_callback;
	fsg_sync_callback_t sync_callback;

	u64		file_length;
	u64		num_sectors;

	unsigned int	ro:1;
	unsigned int	removable:1;
	unsigned int	cdrom:1;
	unsigned int	prevent_medium_removal:1;
	unsigned int	info_valid:1;

	u32		sense_data;
	u32		sense_data_info;

	unsigned int	blksize; /* logical block size of bound block device */
	char		inquiry_string[INQUIRY_STRING_LEN];
};

/* Default size of buffer length. */
#define FSG_BUFLEN	((u32)16384)

/* Maximal number of LUNs supported in mass storage function */
#define FSG_MAX_LUNS	8

enum fsg_buffer_state {
	BUF_STATE_SENDING = -2,
	BUF_STATE_RECEIVING,
	BUF_STATE_EMPTY = 0,
	BUF_STATE_FULL
};

struct fsg_buffhd {
	void				*buf;
	enum fsg_buffer_state		state;
	struct fsg_buffhd		*next;

	/*
	 * The NetChip 2280 is faster, and handles some protocol faults
	 * better, if we don't submit any short bulk-out read requests.
	 * So we will record the intended request length here.
	 */
	unsigned int			bulk_out_intended_length;

	struct usb_request		*inreq;
	struct usb_request		*outreq;
};

enum fsg_state {
	FSG_STATE_NORMAL,
	FSG_STATE_ABORT_BULK_OUT,
	FSG_STATE_PROTOCOL_RESET,
	FSG_STATE_CONFIG_CHANGE,
	FSG_STATE_EXIT,
	FSG_STATE_TERMINATED
};

enum data_direction {
	DATA_DIR_UNKNOWN = 0,
	DATA_DIR_FROM_HOST,
	DATA_DIR_TO_HOST,
	DATA_DIR_NONE
};

enum {
	FSG_STRING_INTERFACE
};

struct usb_function *mass_storage_device_alloc(struct fsg_config *config);

void mass_storage_device_free(struct usb_function *f);

#endif /* USB_F_MASS_STORAGE_H */
