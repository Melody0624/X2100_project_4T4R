#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

/* Sub Classes */
#define US_SC_RBC	0x01		/* Typically, flash devices */
#define US_SC_8020	0x02		/* CD-ROM */
#define US_SC_QIC	0x03		/* QIC-157 Tapes */
#define US_SC_UFI	0x04		/* Floppy */
#define US_SC_8070	0x05		/* Removable media */
#define US_SC_SCSI	0x06		/* Transparent */
#define US_SC_MIN	US_SC_RBC
#define US_SC_MAX	US_SC_SCSI

#define US_SC_DEVICE	0xff		/* Use device's value */

/* Protocol handling routines */
extern void usb_stor_ATAPI_command(struct scsi_cmnd*, struct us_data*);
extern void usb_stor_qic157_command(struct scsi_cmnd*, struct us_data*);
extern void usb_stor_ufi_command(struct scsi_cmnd*, struct us_data*);
extern void usb_stor_transparent_scsi_command(struct scsi_cmnd*,
		struct us_data*);

/* struct scsi_cmnd transfer buffer access utilities */
enum xfer_buf_dir	{TO_XFER_BUF, FROM_XFER_BUF};

extern unsigned int usb_stor_access_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb, unsigned int *index,
	unsigned int *offset, enum xfer_buf_dir dir);

extern void usb_stor_set_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb);
#endif
