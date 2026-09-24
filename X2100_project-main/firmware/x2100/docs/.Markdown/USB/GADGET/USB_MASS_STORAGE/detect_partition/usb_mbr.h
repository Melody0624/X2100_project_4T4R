#ifndef _USB_MBR_H
#define _USB_MBR_H
#include <common.h>
#include <os.h>
#include "blkdev.h"
#include "usb_partition_dec.h"
#define EFI_PMBR_OSTYPE_EFI_GPT 0xEE


struct msdos_partition {
	u8 boot_ind;		/* 0x80 - active */
	u8 head;		/* starting head */
	u8 sector;		/* starting sector */
	u8 cyl;			/* starting cylinder */
	u8 sys_ind;		/* What partition type */
	u8 end_head;		/* end head */
	u8 end_sector;		/* end sector */
	u8 end_cyl;		/* end cylinder */
	u32 start_sect;	/* starting sector counting from 0 */
	u32 nr_sects;	/* nr of sectors in partition */
} __packed;

enum msdos_sys_ind {
	/*
	 * These three have identical behaviour; use the second one if DOS FDISK
	 * gets confused about extended/logical partitions starting past
	 * cylinder 1023.
	 */
	DOS_EXTENDED_PARTITION = 5,
	LINUX_EXTENDED_PARTITION = 0x85,
	WIN98_EXTENDED_PARTITION = 0x0f,

	LINUX_DATA_PARTITION = 0x83,
	LINUX_LVM_PARTITION = 0x8e,
	LINUX_RAID_PARTITION = 0xfd,	/* autodetect RAID partition */

	SOLARIS_X86_PARTITION =	0x82,	/* also Linux swap partitions */
	NEW_SOLARIS_X86_PARTITION = 0xbf,

	DM6_AUX1PARTITION = 0x51,	/* no DDO:  use xlated geom */
	DM6_AUX3PARTITION = 0x53,	/* no DDO:  use xlated geom */
	DM6_PARTITION =	0x54,		/* has DDO: use xlated geom & offset */
	EZD_PARTITION =	0x55,		/* EZ-DRIVE */

	FREEBSD_PARTITION = 0xa5,	/* FreeBSD Partition ID */
	OPENBSD_PARTITION = 0xa6,	/* OpenBSD Partition ID */
	NETBSD_PARTITION = 0xa9,	/* NetBSD Partition ID */
	BSDI_PARTITION = 0xb7,		/* BSDI Partition ID */
	MINIX_PARTITION = 0x81,		/* Minix Partition ID */
	UNIXWARE_PARTITION = 0x63,	/* Same as GNU_HURD and SCO Unix */
};
/* media of boot sector */
static inline int fat_valid_media(u8 media)
{
	return 0xf8 <= media || media == 0xf0;
}
struct fat_boot_sector {
	u8	ignored[3];	/* Boot strap short or near jump */
	u8	system_id[8];	/* Name - can be used to special case
				   partition manager volumes */
	u8	sector_size[2];	/* bytes per logical sector */
	u8	sec_per_clus;	/* sectors/cluster */
	u16	reserved;	/* reserved sectors */
	u8	fats;		/* number of FATs */
	u8	dir_entries[2];	/* root directory entries */
	u8	sectors[2];	/* number of sectors */
	u8	media;		/* media code */
	u16	fat_length;	/* sectors/FAT */
	u16	secs_track;	/* sectors per track */
	u16	heads;		/* number of heads */
	u32	hidden;		/* hidden sectors (unused) */
	u32	total_sect;	/* number of sectors (if sectors == 0) */

	union {
		struct {
			/*  Extended BPB Fields for FAT16 */
			u8	drive_number;	/* Physical drive number */
			u8	state;		/* undocumented, but used
						   for mount state. */
			u8	signature;  /* extended boot signature */
			u8	vol_id[4];	/* volume ID */
			u8	vol_label[11];	/* volume label */
			u8	fs_type[8];		/* file system type */
			/* other fields are not added here */
		} fat16;

		struct {
			/* only used by FAT32 */
			u32	length;		/* sectors/FAT */
			u16	flags;		/* bit 8: fat mirroring,
						   low 4: active fat */
			u8	version[2];	/* major, minor filesystem
						   version */
			u32	root_cluster;	/* first cluster in
						   root directory */
			u16	info_sector;	/* filesystem info sector */
			u16	backup_boot;	/* backup boot sector */
			u16	reserved2[6];	/* Unused */
			/* Extended BPB Fields for FAT32 */
			u8	drive_number;   /* Physical drive number */
			u8    state;       	/* undocumented, but used
						   for mount state. */
			u8	signature;  /* extended boot signature */
			u8	vol_id[4];	/* volume ID */
			u8	vol_label[11];	/* volume label */
			u8	fs_type[8];		/* file system type */
			/* other fields are not added here */
		} fat32;
	};
};
int msdos_partition(struct parsed_partitions *state,struct block_dev block_device);
#endif