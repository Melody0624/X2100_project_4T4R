#ifndef _SCSIGLUE_H_
#define _SCSIGLUE_H_

#include <common.h>

extern unsigned char usb_stor_sense_invalidCDB[18];

extern int usb_stor_device_reset(struct us_data *us);
extern int usb_stor_scsi_inquiry(struct us_data *us, u8 lun, u8 *buf, u8 size);
extern int usb_stor_scsi_test_unit_ready(struct us_data *us, u8 lun);
extern int usb_stor_scsi_read_capacity(struct us_data *us, u8 lun, u8 *buf, u8 size);

extern int usb_stor_scsi_read6(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u8 count);
extern int usb_stor_scsi_read10(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u16 count);
extern int usb_stor_scsi_write6(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u8 count);
extern int usb_stor_scsi_write10(struct us_data *us, u8 lun, u32 block_size, u8 *buffer, u32 sector, u16 count);

#endif
