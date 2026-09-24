#include "usb_partition_dec.h"
#include "usb_check.h"
#include "usb_mbr.h"
#include "usb_efi.h"
#include <stdio.h>
static int (*check_part[])(struct parsed_partitions *,struct block_dev) = {
	efi_partition,		/* this must come before msdos */
	msdos_partition,
	NULL
};
static struct parsed_partitions *allocate_partitions(void)
{
	struct parsed_partitions *state;
	int nr = DISK_MAX_PARTS;

	state = malloc(sizeof(*state));
	memset(state,0,sizeof(*state));
	if (!state)
		return NULL;

	state->parts = malloc(nr*sizeof(state->parts[0]));
	memset(state->parts,0,nr*sizeof(state->parts[0]));
	if (!state->parts) {
		free(state);
		return NULL;
	}

	state->limit = nr;

	return state;
}
static void free_partitions(struct parsed_partitions *state)
{
	free(state->parts);
	free(state);
}
struct parsed_partitions *check_partition(struct block_dev block_device)
{

	struct parsed_partitions *state;
	state = allocate_partitions();
	if (!state)
		return NULL;
	state->p_buff=512*block_device.num_sectors;
	state->sector_size=block_device.block_size;
	state->logical_block_size=block_device.block_size;
	int	i =0, res =0, err = 0;
	while (!res && check_part[i]) {
		memset(state->parts, 0, state->limit * sizeof(state->parts[0]));
		res = check_part[i++](state,block_device);
		if (res < 0) {
			/*
			 * We have hit an I/O error which we don't report now.
			 * But record it, and let the others do their job.
			 */
			err = res;
			res = 0;
		}
	}
	if (res > 0) {
		printf("there are %d partition in it!\n",state->sum);
		return state;
	}
	if (err)
		res = err;
	if (res) {
		printf(" unable to read partition table\n");
	}
	free_partitions(state);
	return NULL;
}


