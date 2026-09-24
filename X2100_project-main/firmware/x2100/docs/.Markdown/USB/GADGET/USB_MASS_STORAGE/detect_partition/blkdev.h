#ifndef _BLKDEV_H
#define _BLKDEV_H
#include <types.h>

/*
 * Maximum number of blkcg policies allowed to be registered concurrently.
 * Defined here to simplify include dependency.
 */
#define DISK_MAX_PARTS			256
#define PARTITION_META_INFO_VOLNAMELTH	64
#define	UUID_STRING_LEN		36
/*
 * Enough for the string representation of any kind of UUID plus NULL.
 * EFI UUID is 36 characters. MSDOS UUID is 11 characters.
 */
#define PARTITION_META_INFO_UUIDLTH	(UUID_STRING_LEN + 1)

struct partition_meta_info {
	char uuid[PARTITION_META_INFO_UUIDLTH];
	u8 volname[PARTITION_META_INFO_VOLNAMELTH];
};
#endif