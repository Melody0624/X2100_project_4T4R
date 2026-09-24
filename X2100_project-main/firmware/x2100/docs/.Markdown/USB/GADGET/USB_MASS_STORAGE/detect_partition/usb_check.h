#ifndef _USB_CHECK_H
#define _USB_CHECK_H
#include "blkdev.h"
#include <common.h>
#include <os.h>
typedef enum
{
    true=1, false=0
}bool;

struct parsed_partitions {
	u32 sector_size;
	u16 logical_block_size;
	u16 block_size;
	u32 total_sectors;
	u64 lastlba;
	size_t p_buff;
	char *partition_type;
	struct {
		u32 from;
		u32 size;
		int flags;
		bool has_info;
		struct partition_meta_info info;
		bool ro;
	} *parts;
	int sum;
	int limit;
};
static inline void
put_partition(struct parsed_partitions *p, int n, u32 from, u32 size)
{
	if (n < p->limit) {
		p->parts[n].from = from;
		p->parts[n].size = size;
	}
}

#endif