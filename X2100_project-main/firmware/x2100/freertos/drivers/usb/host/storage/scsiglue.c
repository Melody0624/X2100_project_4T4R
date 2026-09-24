#include "storage.h"
#include "scsiglue.h"
#include "transport.h"
#include "protocol.h"

#include "scsi.h"

#define MAX_DATA_RETRY_COUNT	3
#define MAX_CMD_RETRY_COUNT		5

/* Command group 3 is reserved and should never be used.  */
const unsigned char scsi_command_size[8] =
{
	6, 10, 10, 12,
	16, 12, 10, 10
};

#define COMMAND_SIZE(opcode) scsi_command_size[((opcode) >> 5) & 7]

static void usb_stor_command_transport(struct us_data *us)
{
	struct scsi_cmnd *srb = &us->scmd;

	/* fail the command if we are disconnecting */
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		US_DEBUGP("Fail command during disconnect\n");
		srb->result = DID_NO_CONNECT << 16;
	}

	/* reject if LUN is higher than
		* the maximum known LUN
		*/
	else if (srb->lun > us->max_lun) {
		US_DEBUGP("Bad LUN (%d), Max LUN (%d)\n",
				srb->lun, us->max_lun);
		srb->result = DID_BAD_TARGET << 16;
	}

	/* Handle those devices which need us to fake
		* their inquiry data */
	else if ((srb->cmnd[0] == INQUIRY) &&
			(us->flags & US_FL_FIX_INQUIRY)) {
		unsigned char data_ptr[36] = {
			0x00, 0x80, 0x02, 0x02,
			0x1F, 0x00, 0x00, 0x00};

		US_DEBUGP("Faking INQUIRY command\n");
		fill_inquiry_response(us, srb, data_ptr, 36);
		srb->result = SAM_STAT_GOOD;
	}

	/* we've got a command, let's do it! */
	else {
		us->proto_handler(srb, us);
	}

}

int usb_stor_device_reset(struct us_data *us)
{
	int result;

	US_DEBUGP("%s called\n", __FUNCTION__);

	/* lock the device pointers and do the reset */
	mutex_lock(&(us->dev_mutex));
	if (test_bit(US_FLIDX_DISCONNECTING, &us->flags)) {
		result = -ENODEV;
		US_DEBUGP("No reset during disconnect\n");
	} else
		result = us->transport_reset(us);
	mutex_unlock(&(us->dev_mutex));

	return result;
}

int usb_stor_scsi_inquiry(struct us_data *us, u8 lun, u8 *buf, u8 size)
{
	int retry_count = 0;
	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);
	assert(size >= SCSI_INQUIRY_BUFFERSIZE);

	mutex_lock(&(us->dev_mutex));
inquiry_retry:
	memset(buf, 0, SCSI_INQUIRY_BUFFERSIZE);
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.lun = lun;
	us->scmd.cmnd[0] = INQUIRY;
	us->scmd.cmnd[1] = lun << 5;
	us->scmd.cmnd[4] = SCSI_INQUIRY_BUFFERSIZE;
	us->scmd.cmd_len = COMMAND_SIZE(INQUIRY);
	us->scmd.request_buffer = buf;
	us->scmd.request_bufflen = SCSI_INQUIRY_BUFFERSIZE;
	us->scmd.data_direction = DATA_DIR_FROM_HOST;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_CMD_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto inquiry_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

int usb_stor_scsi_test_unit_ready(struct us_data *us, u8 lun)
{
	int retry_count = 0;

	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);

	mutex_lock(&(us->dev_mutex));
test_unit_ready_retry:
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.lun = lun;
	us->scmd.cmnd[0] = TEST_UNIT_READY;
	us->scmd.cmnd[1] = lun << 5;
	us->scmd.cmd_len = COMMAND_SIZE(TEST_UNIT_READY);
	us->scmd.data_direction = DATA_DIR_NONE;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_CMD_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto test_unit_ready_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

int usb_stor_scsi_read_capacity(struct us_data *us, u8 lun, u8 *buf, u8 size)
{
	int retry_count = 0;

	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);
	assert(size >= SCSI_CAPACITY_BUFFERSIZE);

	mutex_lock(&(us->dev_mutex));
read_capacity_retry:
	memset(buf, 0, SCSI_CAPACITY_BUFFERSIZE);
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.lun = lun;
	us->scmd.cmnd[0] = READ_CAPACITY;
	us->scmd.cmnd[1] = lun << 5;
	us->scmd.cmd_len = COMMAND_SIZE(READ_CAPACITY);
	us->scmd.request_buffer = buf;
	us->scmd.request_bufflen = SCSI_CAPACITY_BUFFERSIZE;
	us->scmd.data_direction = DATA_DIR_FROM_HOST;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_CMD_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto read_capacity_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

/**
 * This function will execute SCSI_READ_6 command to read data from the usb device.
 *
 * @param buffer the data buffer to save read data
 * @param sector the start sector address to read.
 * @param count the sector count to read.
 *
 * @return the error code, 0 on successfully.
 */
int usb_stor_scsi_read6(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u8 count)
{
	int retry_count = 0;

	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);

	mutex_lock(&(us->dev_mutex));
read6_retry:
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.lun = lun;
	us->scmd.cmnd[0] = READ_6;
	us->scmd.cmnd[1] = (unsigned char) ((sector >> 16) & 0x1F);
	us->scmd.cmnd[2] = (unsigned char) (sector >> 8) & 0xFF;
	us->scmd.cmnd[3] = (unsigned char) sector & 0xFF;
	us->scmd.cmnd[4] = count;
	us->scmd.cmnd[5] = 0;
	us->scmd.cmd_len = COMMAND_SIZE(READ_6);
	us->scmd.request_buffer = buffer;
	us->scmd.request_bufflen = count * block_size;
	us->scmd.data_direction = DATA_DIR_FROM_HOST;
	us->scmd.underflow = us->scmd.request_bufflen;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_DATA_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto read6_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

/**
 * This function will execute SCSI_READ_10 command to read data from the usb device.
 *
 * @param buffer the data buffer to save read data
 * @param sector the start sector address to read.
 * @param count the sector count to read.
 *
 * @return the error code, 0 on successfully.
 */
int usb_stor_scsi_read10(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u16 count)
{
	int retry_count = 0;

	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);

	mutex_lock(&(us->dev_mutex));
read10_retry:
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.cmnd[0] = READ_10;
	us->scmd.cmnd[1] = lun << 5;
	us->scmd.cmnd[2] = (unsigned char) (sector >> 24) & 0xFF;
	us->scmd.cmnd[3] = (unsigned char) (sector >> 16) & 0xFF;
	us->scmd.cmnd[4] = (unsigned char) (sector >> 8) & 0xFF;
	us->scmd.cmnd[5] = (unsigned char) sector & 0xFF;
	us->scmd.cmnd[7] = (unsigned char) (count >> 8) & 0xFF;
	us->scmd.cmnd[8] = (unsigned char) count & 0xFF;
	us->scmd.cmd_len = COMMAND_SIZE(READ_10);
	us->scmd.request_buffer = buffer;
	us->scmd.request_bufflen = count * block_size;
	us->scmd.data_direction = DATA_DIR_FROM_HOST;
	us->scmd.underflow = us->scmd.request_bufflen;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_DATA_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto read10_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

/**
 * This function will execute SCSI_WRITE_10 command to write data to the usb device.
 *
 * @param buffer the data buffer to save write data
 * @param sector the start sector address to write.
 * @param count the sector count to write.
 *
 * @return the error code, 0 on successfully.
 */
int usb_stor_scsi_write6(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u8 count)
{
	int retry_count = 0;

	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);

	mutex_lock(&(us->dev_mutex));
write6_retry:
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.cmnd[0] = WRITE_6;
	us->scmd.cmnd[1] = (unsigned char) ((sector >> 16) & 0x1F);
	us->scmd.cmnd[2] = (unsigned char) (sector >> 8) & 0xFF;
	us->scmd.cmnd[3] = (unsigned char) sector & 0xFF;
	us->scmd.cmnd[4] = count;
	us->scmd.cmnd[5] = 0;
	us->scmd.cmd_len = COMMAND_SIZE(WRITE_6);
	us->scmd.request_buffer = buffer;
	us->scmd.request_bufflen = count * block_size;
	us->scmd.data_direction = DATA_DIR_TO_HOST;
	us->scmd.underflow = us->scmd.request_bufflen;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_DATA_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto write6_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

/**
 * This function will execute SCSI_WRITE_10 command to write data to the usb device.
 *
 * @param buffer the data buffer to save write data
 * @param sector the start sector address to write.
 * @param count the sector count to write.
 *
 * @return the error code, 0 on successfully.
 */
int usb_stor_scsi_write10(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u16 count)
{
	int retry_count = 0;

	US_DEBUGP("%s called\n", __FUNCTION__);

	assert(lun < USB_STORAGE_MAX_LUNS);

	mutex_lock(&(us->dev_mutex));
write10_retry:
	memset(&us->scmd, 0, sizeof(struct scsi_cmnd));
	us->scmd.cmnd[0] = WRITE_10;
	us->scmd.cmnd[1] = lun << 5;
	us->scmd.cmnd[2] = (unsigned char) (sector >> 24) & 0xFF;
	us->scmd.cmnd[3] = (unsigned char) (sector >> 16) & 0xFF;
	us->scmd.cmnd[4] = (unsigned char) (sector >> 8) & 0xFF;
	us->scmd.cmnd[5] = (unsigned char) sector & 0xFF;
	us->scmd.cmnd[7] = (unsigned char) (count >> 8) & 0xFF;
	us->scmd.cmnd[8] = (unsigned char) count & 0xFF;
	us->scmd.cmd_len = COMMAND_SIZE(WRITE_10);
	us->scmd.request_buffer = buffer;
	us->scmd.request_bufflen = count * block_size;
	us->scmd.data_direction = DATA_DIR_TO_HOST;
	us->scmd.underflow = us->scmd.request_bufflen;

	usb_stor_command_transport(us);

	if (us->scmd.result) {
		if ((!(us->scmd.result & 0x00FF0000)) && (retry_count++ < MAX_DATA_RETRY_COUNT)) {
				printf(USB_STORAGE "%s: retry\n", __func__);
				goto write10_retry;
		}

		printf(USB_STORAGE "%s: fail result %d\n", __func__, us->scmd.result);
		mutex_unlock(&(us->dev_mutex));
		return -ENODEV;
	}

	mutex_unlock(&(us->dev_mutex));

	return 0;
}

/* To Report "Illegal Request: Invalid Field in CDB */
unsigned char usb_stor_sense_invalidCDB[18] = {
	[0]	= 0x70,			    /* current error */
	[2]	= ILLEGAL_REQUEST,	    /* Illegal Request = 0x05 */
	[7]	= 0x0a,			    /* additional length */
	[12]	= 0x24			    /* Invalid Field in CDB */
};

