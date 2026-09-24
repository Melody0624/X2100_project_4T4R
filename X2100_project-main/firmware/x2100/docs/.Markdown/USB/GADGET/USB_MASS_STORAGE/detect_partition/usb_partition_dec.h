#ifndef _USB_PARTITION_DEC_H
#define _USB_PARTITION_DEC_H
#include <common.h>
#include <os.h>
typedef int (*read_callback_t)(void *buf, int count, u64 pos);
typedef int (*write_callback_t)(const void *buf, int count, u64 pos);
struct block_dev {
	read_callback_t read_callback;
	write_callback_t write_callback;

	unsigned long long num_sectors;
	unsigned int block_size;
};

#endif
