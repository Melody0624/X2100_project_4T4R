#include <driver/cache.h>
#include <le_byteshift.h>

#include "partition.h"

#include "efi.h"

#include "msdos_fs.h"
#include "msdos_partition.h"

static inline u64 nr_sects(struct msdos_partition *p)
{
	return (u64)get_unaligned_le32(&p->nr_sects);
}

static inline u64 start_sect(struct msdos_partition *p)
{
	return (u64)get_unaligned_le32(&p->start_sect);
}

static inline int is_extended_partition(struct msdos_partition *p)
{
	return (p->sys_ind == DOS_EXTENDED_PARTITION ||
		p->sys_ind == WIN98_EXTENDED_PARTITION ||
		p->sys_ind == LINUX_EXTENDED_PARTITION);
}

#define MSDOS_LABEL_MAGIC1	0x55
#define MSDOS_LABEL_MAGIC2	0xAA

static inline int
msdos_magic_present(unsigned char *p)
{
	return (p[0] == MSDOS_LABEL_MAGIC1 && p[1] == MSDOS_LABEL_MAGIC2);
}

static u8 *read_part_sector(struct usb_storage_device *dev, u64 sector_offset)
{
	int ret;
	int sector_size;
	unsigned char *buf;
	unsigned char *block_buf;

	assert(dev);
	assert(!(dev->block_size % SECTOR_SIZE));

	sector_size = dev->block_size / SECTOR_SIZE;

	buf = malloc(SECTOR_SIZE);
	if (!buf)
		return NULL;

	block_buf = cache_align_malloc(dev->block_size);
	if (!block_buf) {
		free(buf);
		return NULL;
	}

	ret = usb_host_mass_storage_read(dev, block_buf, sector_offset / sector_size, 1);
	if (ret) {
		free(block_buf);
		free(buf);
		return NULL;
	}

	memcpy(buf, block_buf + (sector_offset % sector_size * SECTOR_SIZE), SECTOR_SIZE);

	free(block_buf);

	return buf;
}

/* Value is EBCDIC 'IBMA' */
#define AIX_LABEL_MAGIC1	0xC9
#define AIX_LABEL_MAGIC2	0xC2
#define AIX_LABEL_MAGIC3	0xD4
#define AIX_LABEL_MAGIC4	0xC1
static int aix_magic_present(struct usb_storage_device *dev, unsigned char *p)
{
	struct msdos_partition *pt = (struct msdos_partition *) (p + 0x1be);
	unsigned char *d;
	int slot, ret = 0;

	if (!(p[0] == AIX_LABEL_MAGIC1 &&
		p[1] == AIX_LABEL_MAGIC2 &&
		p[2] == AIX_LABEL_MAGIC3 &&
		p[3] == AIX_LABEL_MAGIC4))
		return 0;

	/*
	 * Assume the partition table is valid if Linux partitions exists.
	 * Note that old Solaris/x86 partitions use the same indicator as
	 * Linux swap partitions, so we consider that a Linux partition as
	 * well.
	 */
	for (slot = 1; slot <= 4; slot++, pt++) {
		if (pt->sys_ind == SOLARIS_X86_PARTITION ||
		    pt->sys_ind == LINUX_RAID_PARTITION ||
		    pt->sys_ind == LINUX_DATA_PARTITION ||
		    pt->sys_ind == LINUX_LVM_PARTITION ||
		    is_extended_partition(pt))
			return 0;
	}
	d = read_part_sector(dev, 7);
	if (d) {
		if (d[0] == '_' && d[1] == 'L' && d[2] == 'V' && d[3] == 'M')
			ret = 1;
		free(d);
	}
	return ret;
}

/*
 * Create devices for each logical partition in an extended partition.
 * The logical partitions form a linked list, with each entry being
 * a partition table with two entries.  The first entry
 * is the real data partition (with a start relative to the partition
 * table start).  The second is a pointer to the next logical partition
 * (with a start relative to the entire extended partition).
 * We do not create a Linux partition for the partition tables, but
 * only for the actual data partitions.
 */

static void parse_extended(const char *udisk_name, struct usb_storage_device *dev,
		struct storage_device_partition *parts, u32 *part_index_p,
		u64 first_sector, u64 first_size)
{
	char *part_name;
	unsigned char *data;
	struct msdos_partition *p;
	u64 this_sector, this_size;
	u64 sector_size;
	int loopct = 0;		/* number of links followed
				   without finding a data partition */
	int i;

	sector_size = dev->block_size / SECTOR_SIZE;
	this_sector = first_sector;
	this_size = first_size;

	while (1) {
		if (++loopct > 100)
			return;
		if (*part_index_p == STORAGE_MAX_PARTITION_NUM)
			return;
		data = read_part_sector(dev, this_sector);
		if (!data)
			return;

		if (!msdos_magic_present(data + 510))
			goto done;

		p = (struct msdos_partition *) (data + 0x1be);

		/*
		 * Usually, the first entry is the real data partition,
		 * the 2nd entry is the next extended partition, or empty,
		 * and the 3rd and 4th entries are unused.
		 * However, DRDOS sometimes has the extended partition as
		 * the first entry (when the data partition is empty),
		 * and OS/2 seems to use all four entries.
		 */

		/*
		 * First process the data partition(s)
		 */
		for (i = 0; i < 4; i++, p++) {
			u64 offs, size, next;

			if (!nr_sects(p) || is_extended_partition(p))
				continue;

			/* Check the 3rd and 4th entries -
			   these sometimes contain random garbage */
			offs = start_sect(p)*sector_size;
			size = nr_sects(p)*sector_size;
			next = this_sector + offs;
			if (i >= 2) {
				if (offs + size > this_size)
					continue;
				if (next < first_sector)
					continue;
				if (next + size > first_sector + first_size)
					continue;
			}

			part_name = malloc(STORAGE_PARTITION_NAME_LEN);
			if (!part_name)
				goto done;
			snprintf(part_name, STORAGE_PARTITION_NAME_LEN, "%sp%d", udisk_name, *part_index_p);

			parts[*part_index_p].name        = part_name;
			parts[*part_index_p].offset      = next * SECTOR_SIZE;
			parts[*part_index_p].size        = size * SECTOR_SIZE;
			parts[*part_index_p].sector_size = dev->block_size;
			parts[*part_index_p].mask_flags  = PART_FLAG_RDWR | PART_TYPE_BLK;
			(*part_index_p)++;
			loopct = 0;
			if (*part_index_p == STORAGE_MAX_PARTITION_NUM)
				goto done;
		}
		/*
		 * Next, process the (first) extended partition, if present.
		 * (So far, there seems to be no reason to make
		 *  parse_extended()  recursive and allow a tree
		 *  of extended partitions.)
		 * It should be a link to the next logical partition.
		 */
		p -= 4;
		for (i = 0; i < 4; i++, p++)
			if (nr_sects(p) && is_extended_partition(p))
				break;
		if (i == 4)
			goto done;	 /* nothing left to do */

		this_sector = first_sector + start_sect(p) * sector_size;
		this_size = nr_sects(p) * sector_size;
		free(data);
	}
done:
	free(data);
}

struct storage_device_partition *msdos_partition(const char *udisk_name, struct usb_storage_device *dev)
{
	u64 sector_size;
	unsigned char *data;
	char *part_name;
	u32 part_index = 0;
	struct msdos_partition *p;
	struct fat_boot_sector *fb;
	struct storage_device_partition *parts;
	int slot;

	sector_size = dev->block_size / SECTOR_SIZE;
	data = read_part_sector(dev, 0);
	if (!data)
		return NULL;

	/*
	 * Note order! (some AIX disks, e.g. unbootable kind,
	 * have no MSDOS 55aa)
	 */
	if (aix_magic_present(dev, data)) {
		free(data);
		return NULL;
	}

	if (!msdos_magic_present(data + 510)) {
		free(data);
		return NULL;
	}

	/* 多申请一个part作为结束标记 */
	parts = malloc((STORAGE_MAX_PARTITION_NUM + 1) * sizeof(struct storage_device_partition));
	if (!parts) {
		free(data);
		return NULL;
	}
	memset(parts, 0x00, (STORAGE_MAX_PARTITION_NUM + 1) * sizeof(struct storage_device_partition));

	/*
	 * Now that the 55aa signature is present, this is probably
	 * either the boot sector of a FAT filesystem or a DOS-type
	 * partition table. Reject this in case the boot indicator
	 * is not 0 or 0x80.
	 */
	p = (struct msdos_partition *) (data + 0x1be);
	for (slot = 1; slot <= 4; slot++, p++) {
		if (p->boot_ind != 0 && p->boot_ind != 0x80) {
			/*
			 * Even without a valid boot indicator value
			 * its still possible this is valid FAT filesystem
			 * without a partition table.
			 */
			fb = (struct fat_boot_sector *) data;
			if (slot == 1 && fb->reserved && fb->fats
				&& fat_valid_media(fb->media)) {

				part_name = malloc(STORAGE_PARTITION_NAME_LEN);
				if (!part_name) {
					free(parts);
					free(data);
					return NULL;
				}
				snprintf(part_name, STORAGE_PARTITION_NAME_LEN, "%sp%d", udisk_name, part_index);

				parts[part_index].name        = part_name;
				parts[part_index].offset      = 0;
				parts[part_index].size        = dev->block_count * dev->block_size;
				parts[part_index].sector_size = dev->block_size;
				parts[part_index].mask_flags  = PART_FLAG_RDWR | PART_TYPE_BLK;
				part_index++;
				free(data);
				return parts;
			} else {
				free(parts);
				free(data);
				return NULL;
			}
		}
	}

	p = (struct msdos_partition *) (data + 0x1be);
	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		/* If this is an EFI GPT disk, msdos should ignore it. */
		if (p->sys_ind == EFI_PMBR_OSTYPE_EFI_GPT) {
			free(parts);
			free(data);
			return NULL;
		}
	}
	p = (struct msdos_partition *) (data + 0x1be);

	for (slot = 1 ; slot <= 4 ; slot++, p++) {
		u64 start = start_sect(p)*sector_size;
		u64 size = nr_sects(p)*sector_size;

		if (!size)
			continue;
		if (is_extended_partition(p)) {
			parse_extended(udisk_name, dev, parts, &part_index, start, size);
			continue;
		}

		part_name = malloc(STORAGE_PARTITION_NAME_LEN);
		if (!part_name)
			break;
		snprintf(part_name, STORAGE_PARTITION_NAME_LEN, "%sp%d", udisk_name, part_index);

		parts[part_index].name        = part_name;
		parts[part_index].offset      = start * SECTOR_SIZE;
		parts[part_index].size        = size * SECTOR_SIZE;
		parts[part_index].sector_size = dev->block_size;
		parts[part_index].mask_flags  = PART_FLAG_RDWR | PART_TYPE_BLK;
		part_index++;
		if (part_index == STORAGE_MAX_PARTITION_NUM)
			break;
	}

	free(data);

	if (part_index == 0) {
		free(parts);
		parts = NULL;
	}

	return parts;
}
