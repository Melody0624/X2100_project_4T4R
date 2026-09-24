#include "usb_check.h"
#include "usb_mbr.h"
#define MSDOS_LABEL_MAGIC1	0x55
#define MSDOS_LABEL_MAGIC2	0xAA
static inline int
msdos_magic_present(unsigned char *p)
{
	return (p[0] == MSDOS_LABEL_MAGIC1 && p[1] == MSDOS_LABEL_MAGIC2);
}

int msdos_partition(struct parsed_partitions *state,struct block_dev block_device)
{
	struct msdos_partition *p;
	struct fat_boot_sector *fb;
	unsigned char mass_storage_buf[512];
	block_device.read_callback(mass_storage_buf,512,0);
	int slot;
	u32 sector_size=state->sector_size/512;

	if (!msdos_magic_present(mass_storage_buf + 510)) {
		return 0;
	}
	/*
	 * Now that the 55aa signature is present, this is probably
	 * either the boot sector of a FAT filesystem or a DOS-type
	 * partition table. Reject this in case the boot indicator
	 * is not 0 or 0x80.
	 */
	p = (struct msdos_partition *) (mass_storage_buf + 0x1be);
	for (slot = 1; slot <= 4; slot++, p++) {
		if ((p->boot_ind != 0 && p->boot_ind != 0x80)) {
			/*
			 * Even without a valid boot indicator value
			 * its still possible this is valid FAT filesystem
			 * without a partition table.
			 */
			fb = (struct fat_boot_sector *) mass_storage_buf;
			if (slot == 1 && fb->reserved && fb->fats
				&& fat_valid_media(fb->media)) {
					printf("This is a FAT filesystem without a partition table!\n");
				return 1;
			} else {
				return 0;
			}
		}
	}

	p = (struct msdos_partition *) (mass_storage_buf + 0x1be);
	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		/* If this is an EFI GPT disk, msdos should ignore it. */
		if (p->sys_ind == EFI_PMBR_OSTYPE_EFI_GPT) {
			printf("It's the sign of protective mbr,please ignore it!\n");
			return 0;
		}
	}

	state->partition_type="msdos_partition";
	printf("MBR Partition Table is valid!  Yea!\n");
	p = (struct msdos_partition *) (mass_storage_buf + 0x1be);
	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		u32 start = p->start_sect*sector_size;
		u32 size = p->nr_sects*sector_size;
	if(!size)
		continue;
	put_partition(state, slot, start, size);
	printf("partion %d's start_block is: %u\n ",slot,state->parts[slot].from);
	printf("partion %d's size_block is: %u\n ",slot,state->parts[slot].size);
	state->sum=slot;
	state->parts[slot].has_info = true;
	}
	return 1;
}
