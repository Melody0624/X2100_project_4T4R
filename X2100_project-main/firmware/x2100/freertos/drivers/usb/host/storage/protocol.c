#include "storage.h"
#include "protocol.h"
#include "scsiglue.h"
#include "transport.h"

/***********************************************************************
 * Protocol routines
 ***********************************************************************/

void usb_stor_qic157_command(struct scsi_cmnd *srb, struct us_data *us)
{
	/* Pad the ATAPI command with zeros
	 *
	 * NOTE: This only works because a scsi_cmnd struct field contains
	 * a unsigned char cmnd[16], so we know we have storage available
	 */
	for (; srb->cmd_len<12; srb->cmd_len++)
		srb->cmnd[srb->cmd_len] = 0;

	/* set command length to 12 bytes */
	srb->cmd_len = 12;

	/* send the command to the transport layer */
	usb_stor_invoke_transport(srb, us);
}

void usb_stor_ATAPI_command(struct scsi_cmnd *srb, struct us_data *us)
{
	/* Pad the ATAPI command with zeros
	 *
	 * NOTE: This only works because a scsi_cmnd struct field contains
	 * a unsigned char cmnd[16], so we know we have storage available
	 */

	/* Pad the ATAPI command with zeros */
	for (; srb->cmd_len<12; srb->cmd_len++)
		srb->cmnd[srb->cmd_len] = 0;

	/* set command length to 12 bytes */
	srb->cmd_len = 12;

	/* send the command to the transport layer */
	usb_stor_invoke_transport(srb, us);
}


void usb_stor_ufi_command(struct scsi_cmnd *srb, struct us_data *us)
{
	/* fix some commands -- this is a form of mode translation
	 * UFI devices only accept 12 byte long commands
	 *
	 * NOTE: This only works because a scsi_cmnd struct field contains
	 * a unsigned char cmnd[16], so we know we have storage available
	 */

	/* Pad the ATAPI command with zeros */
	for (; srb->cmd_len<12; srb->cmd_len++)
		srb->cmnd[srb->cmd_len] = 0;

	/* set command length to 12 bytes (this affects the transport layer) */
	srb->cmd_len = 12;

	/* XXX We should be constantly re-evaluating the need for these */

	/* determine the correct data length for these commands */
	switch (srb->cmnd[0]) {

		/* for INQUIRY, UFI devices only ever return 36 bytes */
	case INQUIRY:
		srb->cmnd[4] = 36;
		break;

		/* again, for MODE_SENSE_10, we get the minimum (8) */
	case MODE_SENSE_10:
		srb->cmnd[7] = 0;
		srb->cmnd[8] = 8;
		break;

		/* for REQUEST_SENSE, UFI devices only ever return 18 bytes */
	case REQUEST_SENSE:
		srb->cmnd[4] = 18;
		break;
	} /* end switch on cmnd[0] */

	/* send the command to the transport layer */
	usb_stor_invoke_transport(srb, us);
}

void usb_stor_transparent_scsi_command(struct scsi_cmnd *srb,
				       struct us_data *us)
{
	/* send the command to the transport layer */
	usb_stor_invoke_transport(srb, us);
}

unsigned int usb_stor_access_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb, unsigned int *index,
	unsigned int *offset, enum xfer_buf_dir dir)
{
	unsigned int cnt;

	if (*offset >= srb->request_bufflen)
		return 0;

	cnt = min(buflen, srb->request_bufflen - *offset);
	if (dir == TO_XFER_BUF)
		memcpy((unsigned char *) srb->request_buffer + *offset,
				buffer, cnt);
	else
		memcpy(buffer, (unsigned char *) srb->request_buffer +
				*offset, cnt);
	*offset += cnt;

	/* Return the amount actually transferred */
	return cnt;
}

/* Store the contents of buffer into srb's transfer buffer and set the
 * SCSI residue. */
void usb_stor_set_xfer_buf(unsigned char *buffer,
	unsigned int buflen, struct scsi_cmnd *srb)
{
	unsigned int index = 0, offset = 0;

	usb_stor_access_xfer_buf(buffer, buflen, srb, &index, &offset,
			TO_XFER_BUF);
	if (buflen < srb->request_bufflen)
		srb->resid = srb->request_bufflen - buflen;
}
