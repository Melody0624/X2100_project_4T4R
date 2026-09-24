#include <driver/cache.h>
#include <crc32.h>

#include "efi.h"

/**
 * last_lba(): return number of last logical block of device
 * @disk: block device
 * 
 * Description: Returns last LBA value on success, 0 on error.
 * This is stored (by sd and ide-geometry) in
 *  the part[0] entry for this disk, and is the number of
 *  physical sectors available on the disk.
 */
static u64 last_lba(struct usb_storage_device *dev)
{
	return dev->block_count - 1ULL;
}

static inline int pmbr_part_valid(gpt_mbr_record *part)
{
	if (part->os_type != EFI_PMBR_OSTYPE_EFI_GPT)
		goto invalid;

	/* set to 0x00000001 (i.e., the LBA of the GPT Partition Header) */
	if (part->starting_lba != GPT_PRIMARY_PARTITION_TABLE_LBA)
		goto invalid;

	return GPT_MBR_PROTECTIVE;
invalid:
	return 0;
}

/**
 * is_pmbr_valid(): test Protective MBR for validity
 * @mbr: pointer to a legacy mbr structure
 * @total_sectors: amount of sectors in the device
 *
 * Description: Checks for a valid protective or hybrid
 * master boot record (MBR). The validity of a pMBR depends
 * on all of the following properties:
 *  1) MSDOS signature is in the last two bytes of the MBR
 *  2) One partition of type 0xEE is found
 *
 * In addition, a hybrid MBR will have up to three additional
 * primary partitions, which point to the same space that's
 * marked out by up to three GPT partitions.
 *
 * Returns 0 upon invalid MBR, or GPT_MBR_PROTECTIVE or
 * GPT_MBR_HYBRID depending on the device layout.
 */
static int is_pmbr_valid(legacy_mbr *mbr)
{
	int i, ret = 0; /* invalid by default */

	if (!mbr || mbr->signature != MSDOS_MBR_SIGNATURE)
		goto done;

	for (i = 0; i < 4; i++) {
		ret = pmbr_part_valid(&mbr->partition_record[i]);
		if (ret == GPT_MBR_PROTECTIVE) {
			/*
			 * Ok, we at least know that there's a protective MBR,
			 * now check if there are other partition types for
			 * hybrid MBR.
			 */
			goto check_hybrid;
		}
	}

	if (ret != GPT_MBR_PROTECTIVE)
		goto done;
check_hybrid:
	for (i = 0; i < 4; i++)
		if ((mbr->partition_record[i].os_type !=
			EFI_PMBR_OSTYPE_EFI_GPT) &&
		    (mbr->partition_record[i].os_type != 0x00))
			ret = GPT_MBR_HYBRID;

done:
	return ret;
}

/**
 * read_lba(): Read bytes from disk, starting at given LBA
 * @state: disk parsed partitions
 * @lba: the Logical Block Address of the partition table
 * @buffer: destination buffer
 * @count: bytes to read
 *
 * Description: Reads @count bytes from @state->disk into @buffer.
 * Returns number of bytes read on success, 0 on error.
 */
static size_t read_lba(struct usb_storage_device *dev,
		       u64 lba, u8 *buffer, size_t count)
{
	int ret;
	int copied;
	unsigned char *buf;
	size_t totalreadcount = 0;

	if (!buffer || !count || lba > last_lba(dev))
		return 0;

	buf = cache_align_malloc(dev->block_size);
	if (!buf)
		return -ENOMEM;

	while (count) {
		copied = dev->block_size;
		ret = usb_host_mass_storage_read(dev, buf, lba++, 1);
		if (ret)
			break;
		if (copied > count)
			copied = count;
		memcpy(buffer, buf, copied);
		buffer += copied;
		totalreadcount +=copied;
		count -= copied;
	}

	free(buf);

	return totalreadcount;
}

/**
 * alloc_read_gpt_entries(): reads partition entries from disk
 * @state: disk parsed partitions
 * @gpt: GPT header
 * 
 * Description: Returns ptes on success,  NULL on error.
 * Allocates space for PTEs based on information found in @gpt.
 * Notes: remember to free pte when you're done!
 */
static gpt_entry *alloc_read_gpt_entries(struct usb_storage_device *dev, gpt_header *gpt)
{
	size_t count;
	gpt_entry *pte;

	if (!gpt)
		return NULL;

	count = (size_t)gpt->num_partition_entries * gpt->sizeof_partition_entry;
	if (!count)
		return NULL;
	pte = cache_align_malloc(count);
	if (!pte)
		return NULL;

	if (read_lba(dev, gpt->partition_entry_lba,
			(u8 *) pte, count) < count) {
		free(pte);
		return NULL;
	}
	return pte;
}

/**
 * alloc_read_gpt_header(): Allocates GPT header, reads into it from disk
 * @state: disk parsed partitions
 * @lba: the Logical Block Address of the partition table
 * 
 * Description: returns GPT header on success, NULL on error.   Allocates
 * and fills a GPT header starting at @ from @state->disk.
 * Note: remember to free gpt when finished with it.
 */
static gpt_header *alloc_read_gpt_header(struct usb_storage_device *dev, u64 lba)
{
	gpt_header *gpt;
	unsigned ssz = dev->block_size;

	gpt = malloc(ssz);
	if (!gpt)
		return NULL;

	if (read_lba(dev, lba, (u8 *) gpt, ssz) < ssz) {
		free(gpt);
		return NULL;
	}

	return gpt;
}

/**
 * is_gpt_valid() - tests one GPT header and PTEs for validity
 * @state: disk parsed partitions
 * @lba: logical block address of the GPT header to test
 * @gpt: GPT header ptr, filled on return.
 * @ptes: PTEs ptr, filled on return.
 *
 * Description: returns 1 if valid,  0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 */
static int is_gpt_valid(struct usb_storage_device *dev, u64 lba,
			gpt_header **gpt, gpt_entry **ptes)
{
	u32 crc, origcrc;
	u64 lastlba, pt_size;

	if (!ptes)
		return 0;
	if (!(*gpt = alloc_read_gpt_header(dev, lba)))
		return 0;

	/* Check the GUID Partition Table signature */
	if ((*gpt)->signature != GPT_HEADER_SIGNATURE)
		goto fail;

	/* Check the GUID Partition Table header size is too big */
	if ((*gpt)->header_size > dev->block_size)
		goto fail;

	/* Check the GUID Partition Table header size is too small */
	if ((*gpt)->header_size < sizeof(gpt_header))
		goto fail;

	/* Check the GUID Partition Table CRC */
	origcrc = (*gpt)->header_crc32;
	(*gpt)->header_crc32 = 0;
	crc = crc32(0, (const unsigned char *) (*gpt), (*gpt)->header_size);

	if (crc != origcrc)
		goto fail;

	(*gpt)->header_crc32 = origcrc;

	/* Check that the my_lba entry points to the LBA that contains
	 * the GUID Partition Table */
	if ((*gpt)->my_lba != lba)
		goto fail;

	/* Check the first_usable_lba and last_usable_lba are
	 * within the disk.
	 */
	lastlba = last_lba(dev);
	if ((*gpt)->first_usable_lba > lastlba)
		goto fail;

	if ((*gpt)->last_usable_lba > lastlba)
		goto fail;

	if ((*gpt)->last_usable_lba < (*gpt)->first_usable_lba)
		goto fail;

	/* Check that sizeof_partition_entry has the correct value */
	if ((*gpt)->sizeof_partition_entry != sizeof(gpt_entry))
		goto fail;

	/* Check partition entries */
	if ((*gpt)->num_partition_entries > DISK_MAX_PARTS)
		goto fail;

	pt_size = (u64)(*gpt)->num_partition_entries * (*gpt)->sizeof_partition_entry;
	if (!(*ptes = alloc_read_gpt_entries(dev, *gpt)))
		goto fail;

	/* Check the GUID Partition Entry Array CRC */
	crc = crc32(0, (const unsigned char *) (*ptes), pt_size);

	if (crc != (*gpt)->partition_entry_array_crc32)
		goto fail_ptes;

	/* We're done, all's well */
	return 1;

 fail_ptes:
	free(*ptes);
	*ptes = NULL;
 fail:
	free(*gpt);
	*gpt = NULL;
	return 0;
}

/**
 * is_pte_valid() - tests one PTE for validity
 * @pte:pte to check
 * @lastlba: last lba of the disk
 *
 * Description: returns 1 if valid,  0 on error.
 */
static inline int
is_pte_valid(const gpt_entry *pte, const u64 lastlba)
{
	if ((!efi_guidcmp(pte->partition_type_guid, NULL_GUID)) ||
	    pte->starting_lba > lastlba	 ||
	    pte->ending_lba   > lastlba)
		return 0;
	return 1;
}

/**
 * compare_gpts() - Search disk for valid GPT headers and PTEs
 * @pgpt: primary GPT header
 * @agpt: alternate GPT header
 * @lastlba: last LBA number
 *
 * Description: Returns nothing.  Sanity checks pgpt and agpt fields
 * and prints warnings on discrepancies.
 * 
 */
static void
compare_gpts(gpt_header *pgpt, gpt_header *agpt, u64 lastlba)
{
	int error_found = 0;
	if (!pgpt || !agpt)
		return;
	if (pgpt->my_lba != agpt->alternate_lba) {
		printf("GPT:Primary header LBA != Alt. header alternate_lba\n");
		printf("GPT:%lld != %lld\n",
		       (unsigned long long)pgpt->my_lba,
		       (unsigned long long)agpt->alternate_lba);
		error_found++;
	}
	if (pgpt->alternate_lba != agpt->my_lba) {
		printf("GPT:Primary header alternate_lba != Alt. header my_lba\n");
		printf("GPT:%lld != %lld\n",
		       (unsigned long long)pgpt->alternate_lba,
		       (unsigned long long)agpt->my_lba);
		error_found++;
	}
	if (pgpt->first_usable_lba !=
	    agpt->first_usable_lba) {
		printf("GPT:first_usable_lbas don't match.\n");
		printf("GPT:%lld != %lld\n",
		       (unsigned long long)pgpt->first_usable_lba,
		       (unsigned long long)agpt->first_usable_lba);
		error_found++;
	}
	if (pgpt->last_usable_lba !=
	    agpt->last_usable_lba) {
		printf("GPT:last_usable_lbas don't match.\n");
		printf("GPT:%lld != %lld\n",
		       (unsigned long long)pgpt->last_usable_lba,
		       (unsigned long long)agpt->last_usable_lba);
		error_found++;
	}
	if (efi_guidcmp(pgpt->disk_guid, agpt->disk_guid)) {
		printf("GPT:disk_guids don't match.\n");
		error_found++;
	}
	if (pgpt->num_partition_entries !=
	    agpt->num_partition_entries) {
		printf("GPT:num_partition_entries don't match: "
		       "0x%x != 0x%x\n",
		       pgpt->num_partition_entries,
		       agpt->num_partition_entries);
		error_found++;
	}
	if (pgpt->sizeof_partition_entry !=
	    agpt->sizeof_partition_entry) {
		printf("GPT:sizeof_partition_entry values don't match: "
		       "0x%x != 0x%x\n",
		       pgpt->sizeof_partition_entry,
		       agpt->sizeof_partition_entry);
		error_found++;
	}
	if (pgpt->partition_entry_array_crc32 !=
	    agpt->partition_entry_array_crc32) {
		printf("GPT:partition_entry_array_crc32 values don't match: "
		       "0x%x != 0x%x\n",
		       pgpt->partition_entry_array_crc32,
		       agpt->partition_entry_array_crc32);
		error_found++;
	}
	if (pgpt->alternate_lba != lastlba) {
		printf("GPT:Primary header thinks Alt. header is not at the end of the disk.\n");
		printf("GPT:%lld != %lld\n",
			(unsigned long long)pgpt->alternate_lba,
			(unsigned long long)lastlba);
		error_found++;
	}

	if (agpt->my_lba != lastlba) {
		printf("GPT:Alternate GPT header not at the end of the disk.\n");
		printf("GPT:%lld != %lld\n",
			(unsigned long long)agpt->my_lba,
			(unsigned long long)lastlba);
		error_found++;
	}

	if (error_found)
		printf("GPT: Use GNU Parted to correct GPT errors.\n");
	return;
}

/**
 * find_valid_gpt() - Search disk for valid GPT headers and PTEs
 * @state: disk parsed partitions
 * @gpt: GPT header ptr, filled on return.
 * @ptes: PTEs ptr, filled on return.
 *
 * Description: Returns 1 if valid, 0 on error.
 * If valid, returns pointers to newly allocated GPT header and PTEs.
 * Validity depends on PMBR being valid (or being overridden by the
 * 'gpt' kernel command line option) and finding either the Primary
 * GPT header and PTEs valid, or the Alternate GPT header and PTEs
 * valid.  If the Primary GPT header is not valid, the Alternate GPT header
 * is not checked unless the 'gpt' kernel command line option is passed.
 * This protects against devices which misreport their size, and forces
 * the user to decide to use the Alternate GPT.
 */
static int find_valid_gpt(struct usb_storage_device *dev, gpt_header **gpt,
			  gpt_entry **ptes)
{
	int good_pgpt = 0, good_agpt = 0, good_pmbr = 0;
	gpt_header *pgpt = NULL, *agpt = NULL;
	gpt_entry *pptes = NULL, *aptes = NULL;
	legacy_mbr *legacymbr;
	u64 lastlba;

	if (!ptes)
		return 0;

	lastlba = last_lba(dev);
	/* This will be added to the EFI Spec. per Intel after v1.02. */
	legacymbr = malloc(sizeof(*legacymbr));
	if (!legacymbr)
		goto fail;

	memset(legacymbr, 0, sizeof(*legacymbr));
	read_lba(dev, 0, (u8 *)legacymbr, sizeof(*legacymbr));
	good_pmbr = is_pmbr_valid(legacymbr);
	free(legacymbr);

	if (!good_pmbr)
		goto fail;

	good_pgpt = is_gpt_valid(dev, GPT_PRIMARY_PARTITION_TABLE_LBA, &pgpt, &pptes);
	if (good_pgpt)
		good_agpt = is_gpt_valid(dev, pgpt->alternate_lba, &agpt, &aptes);

	/* The obviously unsuccessful case */
	if (!good_pgpt && !good_agpt)
			goto fail;

	compare_gpts(pgpt, agpt, lastlba);

	/* The good cases */
	if (good_pgpt) {
		*gpt  = pgpt;
		*ptes = pptes;
		free(agpt);
		free(aptes);
		if (!good_agpt)
			printf("Alternate GPT is invalid, using primary GPT.\n");
		return 1;
	}
	else if (good_agpt) {
		*gpt  = agpt;
		*ptes = aptes;
		free(pgpt);
		free(pptes);
		printf("Primary GPT is invalid, using alternate GPT.\n");
		return 1;
	}

 fail:
	free(pgpt);
	free(agpt);
	free(pptes);
	free(aptes);
	*gpt = NULL;
	*ptes = NULL;
	return 0;
}

/**
 * efi_partition - scan for GPT partitions
 * @state: disk parsed partitions
 *
 * Description: called from check.c, if the disk contains GPT
 * partitions, sets up partition entries in the kernel.
 *
 * If the first block on the disk is a legacy MBR,
 * it will get handled by msdos_partition().
 * If it's a Protective MBR, we'll handle it here.
 *
 * We do not create a Linux partition for GPT, but
 * only for the actual data partitions.
 * Returns:
 * -1 if unable to read the partition table
 *  0 if this isn't our partition table
 *  1 if successful
 *
 */
struct storage_device_partition *efi_partition(const char *udisk_name, struct usb_storage_device *dev)
{
	u32 i;
	char *part_name;
	u32 part_index = 0;
	gpt_header *gpt = NULL;
	gpt_entry *ptes = NULL;
	struct storage_device_partition *parts;

	assert(udisk_name);
	assert(dev);

	if (!find_valid_gpt(dev, &gpt, &ptes) || !gpt || !ptes) {
		free(gpt);
		free(ptes);
		return NULL;
	}

	/* 多申请一个part作为结束标记 */
	parts = malloc((STORAGE_MAX_PARTITION_NUM + 1) * sizeof(struct storage_device_partition));
	if (!parts)
		return NULL;

	memset(parts, 0x00, (STORAGE_MAX_PARTITION_NUM + 1) * sizeof(struct storage_device_partition));

	for (i = 0; i < gpt->num_partition_entries && part_index < STORAGE_MAX_PARTITION_NUM; i++) {
		u64 start = ptes[i].starting_lba;
		u64 size = ptes[i].ending_lba - ptes[i].starting_lba + 1ULL;

		if (!is_pte_valid(&ptes[i], last_lba(dev)))
			continue;

		part_name = malloc(STORAGE_PARTITION_NAME_LEN);
		if (!part_name)
			break;
		snprintf(part_name, STORAGE_PARTITION_NAME_LEN, "%sp%d", udisk_name, part_index);

		parts[part_index].name        = part_name;
		parts[part_index].offset      = start * dev->block_size;
		parts[part_index].size        = size * dev->block_size;
		parts[part_index].sector_size = dev->block_size;
		parts[part_index].mask_flags  = PART_FLAG_RDWR | PART_TYPE_BLK;
		part_index++;
	}
	free(ptes);
	free(gpt);

	if (part_index == 0) {
		free(parts);
		parts = NULL;
	}

	return parts;
}
