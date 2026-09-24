// SPDX-License-Identifier: GPL-2.0
#ifndef _SCSI_SCSI_H
#define _SCSI_SCSI_H

/*
 *      SCSI opcodes
 */

#define TEST_UNIT_READY       0x00
#define REZERO_UNIT           0x01
#define REQUEST_SENSE         0x03
#define FORMAT_UNIT           0x04
#define READ_BLOCK_LIMITS     0x05
#define REASSIGN_BLOCKS       0x07
#define INITIALIZE_ELEMENT_STATUS 0x07
#define READ_6                0x08
#define WRITE_6               0x0a
#define SEEK_6                0x0b
#define READ_REVERSE          0x0f
#define WRITE_FILEMARKS       0x10
#define SPACE                 0x11
#define INQUIRY               0x12
#define RECOVER_BUFFERED_DATA 0x14
#define MODE_SELECT           0x15
#define RESERVE               0x16
#define RELEASE               0x17
#define COPY                  0x18
#define ERASE                 0x19
#define MODE_SENSE            0x1a
#define START_STOP            0x1b
#define RECEIVE_DIAGNOSTIC    0x1c
#define SEND_DIAGNOSTIC       0x1d
#define ALLOW_MEDIUM_REMOVAL  0x1e

#define READ_FORMAT_CAPACITIES 0x23
#define SET_WINDOW            0x24
#define READ_CAPACITY         0x25
#define READ_10               0x28
#define WRITE_10              0x2a
#define SEEK_10               0x2b
#define POSITION_TO_ELEMENT   0x2b
#define WRITE_VERIFY          0x2e
#define VERIFY                0x2f
#define SEARCH_HIGH           0x30
#define SEARCH_EQUAL          0x31
#define SEARCH_LOW            0x32
#define SET_LIMITS            0x33
#define PRE_FETCH             0x34
#define READ_POSITION         0x34
#define SYNCHRONIZE_CACHE     0x35
#define LOCK_UNLOCK_CACHE     0x36
#define READ_DEFECT_DATA      0x37
#define MEDIUM_SCAN           0x38
#define COMPARE               0x39
#define COPY_VERIFY           0x3a
#define WRITE_BUFFER          0x3b
#define READ_BUFFER           0x3c
#define UPDATE_BLOCK          0x3d
#define READ_LONG             0x3e
#define WRITE_LONG            0x3f
#define CHANGE_DEFINITION     0x40
#define WRITE_SAME            0x41
#define UNMAP		      0x42
#define READ_TOC              0x43
#define READ_HEADER           0x44
#define GET_EVENT_STATUS_NOTIFICATION 0x4a
#define LOG_SELECT            0x4c
#define LOG_SENSE             0x4d
#define XDWRITEREAD_10        0x53
#define MODE_SELECT_10        0x55
#define RESERVE_10            0x56
#define RELEASE_10            0x57
#define MODE_SENSE_10         0x5a
#define PERSISTENT_RESERVE_IN 0x5e
#define PERSISTENT_RESERVE_OUT 0x5f
#define VARIABLE_LENGTH_CMD   0x7f
#define REPORT_LUNS           0xa0
#define SECURITY_PROTOCOL_IN  0xa2
#define MAINTENANCE_IN        0xa3
#define MAINTENANCE_OUT       0xa4
#define MOVE_MEDIUM           0xa5
#define EXCHANGE_MEDIUM       0xa6
#define READ_12               0xa8
#define SERVICE_ACTION_OUT_12 0xa9
#define WRITE_12              0xaa
#define READ_MEDIA_SERIAL_NUMBER 0xab /* Obsolete with SPC-2 */
#define SERVICE_ACTION_IN_12  0xab
#define WRITE_VERIFY_12       0xae
#define VERIFY_12	      0xaf
#define SEARCH_HIGH_12        0xb0
#define SEARCH_EQUAL_12       0xb1
#define SEARCH_LOW_12         0xb2
#define SECURITY_PROTOCOL_OUT 0xb5
#define READ_ELEMENT_STATUS   0xb8
#define SEND_VOLUME_TAG       0xb6
#define WRITE_LONG_2          0xea
#define EXTENDED_COPY         0x83
#define RECEIVE_COPY_RESULTS  0x84
#define ACCESS_CONTROL_IN     0x86
#define ACCESS_CONTROL_OUT    0x87
#define READ_16               0x88
#define COMPARE_AND_WRITE     0x89
#define WRITE_16              0x8a
#define READ_ATTRIBUTE        0x8c
#define WRITE_ATTRIBUTE	      0x8d
#define WRITE_VERIFY_16	      0x8e
#define VERIFY_16	      0x8f
#define SYNCHRONIZE_CACHE_16  0x91
#define WRITE_SAME_16	      0x93
#define ZBC_OUT		      0x94
#define ZBC_IN		      0x95
#define SERVICE_ACTION_BIDIRECTIONAL 0x9d
#define SERVICE_ACTION_IN_16  0x9e
#define SERVICE_ACTION_OUT_16 0x9f
/* values for service action in */
#define	SAI_READ_CAPACITY_16  0x10
#define SAI_GET_LBA_STATUS    0x12
#define SAI_REPORT_REFERRALS  0x13
/* values for maintenance in */
#define MI_REPORT_IDENTIFYING_INFORMATION 0x05
#define MI_REPORT_TARGET_PGS  0x0a
#define MI_REPORT_ALIASES     0x0b
#define MI_REPORT_SUPPORTED_OPERATION_CODES 0x0c
#define MI_REPORT_SUPPORTED_TASK_MANAGEMENT_FUNCTIONS 0x0d
#define MI_REPORT_PRIORITY    0x0e
#define MI_REPORT_TIMESTAMP   0x0f
#define MI_MANAGEMENT_PROTOCOL_IN 0x10
/* value for MI_REPORT_TARGET_PGS ext header */
#define MI_EXT_HDR_PARAM_FMT  0x20
/* values for maintenance out */
#define MO_SET_IDENTIFYING_INFORMATION 0x06
#define MO_SET_TARGET_PGS     0x0a
#define MO_CHANGE_ALIASES     0x0b
#define MO_SET_PRIORITY       0x0e
#define MO_SET_TIMESTAMP      0x0f
#define MO_MANAGEMENT_PROTOCOL_OUT 0x10
/* values for ZBC_IN */
#define ZI_REPORT_ZONES	      0x00
/* values for ZBC_OUT */
#define ZO_CLOSE_ZONE	      0x01
#define ZO_FINISH_ZONE	      0x02
#define ZO_OPEN_ZONE	      0x03
#define ZO_RESET_WRITE_POINTER 0x04
/* values for variable length command */
#define XDREAD_32	      0x03
#define XDWRITE_32	      0x04
#define XPWRITE_32	      0x06
#define XDWRITEREAD_32	      0x07
#define READ_32		      0x09
#define VERIFY_32	      0x0a
#define WRITE_32	      0x0b
#define WRITE_VERIFY_32	      0x0c
#define WRITE_SAME_32	      0x0d
#define ATA_32		      0x1ff0

/* Values for T10/04-262r7 */
#define	ATA_16		      0x85	/* 16-byte pass-thru */
#define	ATA_12		      0xa1	/* 12-byte pass-thru */

/* Vendor specific CDBs start here */
#define VENDOR_SPECIFIC_CDB 0xc0

/*
 *  Status codes
 */
#define SAM_STAT_GOOD            0x00
#define SAM_STAT_CHECK_CONDITION 0x01

/* Status error codes */
#define DID_NO_CONNECT  0x01	/* Couldn't connect before timeout period  */
#define DID_BAD_TARGET  0x02	/* BAD target.                             */
#define DID_ERROR       0x03	/* Internal error                          */

/* These indicate the error that occurred, and what is available. */
#define SUGGEST_RETRY       0x01

/*
 *  SENSE KEYS
 */

#define NO_SENSE            0x00
#define RECOVERED_ERROR     0x01
#define NOT_READY           0x02
#define MEDIUM_ERROR        0x03
#define HARDWARE_ERROR      0x04
#define ILLEGAL_REQUEST     0x05
#define UNIT_ATTENTION      0x06
#define DATA_PROTECT        0x07
#define BLANK_CHECK         0x08
#define COPY_ABORTED        0x0a
#define ABORTED_COMMAND     0x0b
#define VOLUME_OVERFLOW     0x0d
#define MISCOMPARE          0x0e


/*
 *  DEVICE TYPES
 */

#define TYPE_DISK           0x00
#define TYPE_TAPE           0x01
#define TYPE_PROCESSOR      0x03    /* HP scanners use this */
#define TYPE_WORM           0x04    /* Treated as ROM by our system */
#define TYPE_ROM            0x05
#define TYPE_SCANNER        0x06
#define TYPE_MOD            0x07    /* Magneto-optical disk -
				     * - treated as TYPE_DISK */
#define TYPE_MEDIUM_CHANGER 0x08
#define TYPE_ENCLOSURE	    0x0d    /* Enclosure Services Device */
#define TYPE_NO_LUN         0x7f

#endif
