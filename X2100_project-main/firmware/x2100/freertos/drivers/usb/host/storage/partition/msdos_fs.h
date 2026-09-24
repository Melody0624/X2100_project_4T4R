#ifndef _LINUX_MSDOS_FS_H
#define _LINUX_MSDOS_FS_H

#include <common.h>

/*
 * The MS-DOS filesystem constants/structures
 */

#define SECTOR_SIZE	512		/* sector size (bytes) */

#define MSDOS_NAME	11	/* maximum name length */
#define MSDOS_SLOTS	21	/* max # of slots for short and long names */

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
			u8	vol_label[MSDOS_NAME];	/* volume label */
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
			u8	vol_label[MSDOS_NAME];	/* volume label */
			u8	fs_type[8];		/* file system type */
			/* other fields are not added here */
		} fat32;
	};
};

struct fat_boot_fsinfo {
	u32   signature1;	/* 0x41615252L */
	u32   reserved1[120];	/* Nothing as far as I can tell */
	u32   signature2;	/* 0x61417272L */
	u32   free_clusters;	/* Free cluster count.  -1 if unknown */
	u32   next_cluster;	/* Most recently allocated cluster */
	u32   reserved2[4];
};

struct msdos_dir_entry {
	u8	name[MSDOS_NAME];/* name and extension */
	u8	attr;		/* attribute bits */
	u8    lcase;		/* Case for base and extension */
	u8	ctime_cs;	/* Creation time, centiseconds (0-199) */
	u16	ctime;		/* Creation time */
	u16	cdate;		/* Creation date */
	u16	adate;		/* Last access date */
	u16	starthi;	/* High 16 bits of cluster in FAT32 */
	u16	time,date,start;/* time, date and first cluster */
	u32	size;		/* file size (in bytes) */
};

/* Up to 13 characters of the name */
struct msdos_dir_slot {
	u8    id;		/* sequence number for slot */
	u8    name0_4[10];	/* first 5 characters in name */
	u8    attr;		/* attribute byte */
	u8    reserved;	/* always 0 */
	u8    alias_checksum;	/* checksum for 8.3 alias */
	u8    name5_10[12];	/* 6 more characters in name */
	u16   start;		/* starting cluster number, 0 in long slots */
	u8    name11_12[4];	/* last 2 characters in name */
};

/* media of boot sector */
static inline int fat_valid_media(u8 media)
{
	return 0xf8 <= media || media == 0xf0;
}

#endif /* _LINUX_MSDOS_FS_H */
