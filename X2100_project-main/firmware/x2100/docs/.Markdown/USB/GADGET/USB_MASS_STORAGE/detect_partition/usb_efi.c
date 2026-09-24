#include "usb_check.h"
#include "usb_efi.h"
#include "crc.h"
/* This allows a kernel command line option 'gpt' to override
 * the test for invalid PMBR.  Not __initdata because reloading
 * the partition tables happens after init too.
 */
static int force_gpt=0;


/**
 * efi_crc32() - EFI version of crc32 function
 * @buf: buffer to calculate crc32 of
 * @len: length of buf
 *
 * Description: Returns EFI-style CRC32 value for @buf
 * This function uses the little endian Ethernet polynomial
 * but seeds the function with ~0, and xor's with ~0 at the end.
 * Note, the EFI Specification, v1.02, has a reference to
 * Dr. Dobbs Journal, May 1994 (actually it's in May 1992).
 */
static inline uint32_t
efi_crc32(const void *buf, unsigned long len)
{
	return crc32(buf,len);
}


static inline int pmbr_part_valid(gpt_mbr_record *part)
{
	if (part->os_type != EFI_PMBR_OSTYPE_EFI_GPT)
		goto invalid;

	/* set to 0x00000001 (i.e., the LBA of the GPT Partition Header) */
	if (part->starting_lba!= GPT_PRIMARY_PARTITION_TABLE_LBA)
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
static int is_pmbr_valid(legacy_mbr *mbr, u32 total_sectors)
{
	uint32_t sz = 0;
	int i, part = 0, ret = 0; /* invalid by default */

	if (!mbr || mbr->signature != MSDOS_MBR_SIGNATURE)
		goto done;

	for (i = 0; i < 4; i++) {
		ret = pmbr_part_valid(&mbr->partition_record[i]);
		if (ret == GPT_MBR_PROTECTIVE) {
			part = i;
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

	/*
	 * Protective MBRs take up the lesser of the whole disk
	 * or 2 TiB (32bit LBA), ignoring the rest of the disk.
	 * Some partitioning programs, nonetheless, choose to set
	 * the size to the maximum 32-bit limitation, disregarding
	 * the disk size.
	 *
	 * Hybrid MBRs do not necessarily comply with this.
	 *
	 * Consider a bad value here to be a warning to support dd'ing
	 * an image from a smaller disk to a larger disk.
	 */
	if (ret == GPT_MBR_PROTECTIVE) {
		sz = mbr->partition_record[part].size_in_lba;
		if (sz != (uint32_t) total_sectors - 1 && sz != 0xFFFFFFFF)
			printf("GPT: mbr size in lba (%u) different than whole disk (%u).\n",
				 sz, min_t(u32,total_sectors - 1, 0xFFFFFFFF));
	}
done:
	return ret;
}

/**
 * read_lba(): Read bytes from disk, starting at given LBA
 * @state: disk parsed partitions
 * @lba: the Logical Block Address of the partition table
 * @buffer: destination buffer
 * @count: bytes to read
 * @mass_storage_buf[]: the first adress of mass storage buff
 * Description: Reads @count bytes from @state->disk into @buffer.
 * Returns number of bytes read on success, 0 on error.
 */
static size_t read_lba(struct parsed_partitions *state,
		       uint64_t lba, unsigned char *buffer, size_t count,struct block_dev block_device)
{
	uint64_t pos=lba*block_device.block_size;
	size_t totalreadcount=block_device.read_callback(buffer,count,pos);
	return totalreadcount;
}

/**
 * alloc_read_gpt_entries(): reads partition entries from disk
 * @state: disk parsed partitions
 * @gpt: GPT header
 * Description: Returns ptes on success,  NULL on error.
 * Allocates space for PTEs based on information found in @gpt.
 * Notes: remember to free pte when you're done!
 */
static gpt_entry *alloc_read_gpt_entries(struct parsed_partitions *state,
					 gpt_header *gpt,struct block_dev block_device)
{
	size_t count;
	gpt_entry *pte;
	if (!gpt)
		return NULL;

	count = (size_t)(gpt->num_partition_entries) * (gpt->sizeof_partition_entry);
	if (!count)
		return NULL;
	pte = malloc(count);
	memset(pte,0,count);
	if (!pte)
		return NULL;
	if (read_lba(state, (gpt->partition_entry_lba),
			(u8 *) pte, count,block_device) < count) {
		free(pte);
                pte=NULL;
		return NULL;
	}
	return pte;
}

/**
 * alloc_read_gpt_header(): Allocates GPT header, reads into it from disk
 * @state: disk parsed partitions
 * @lba: the Logical Block Address of the partition table
 * Description: returns GPT header on success, NULL on error.   Allocates
 * and fills a GPT header starting at @ from @state->disk.
 * Note: remember to free gpt when finished with it.
 */
static gpt_header *alloc_read_gpt_header(struct parsed_partitions *state,
					 u64 lba,struct block_dev block_device)
{
	gpt_header *gpt;
	unsigned ssz = state->logical_block_size; 
	gpt = malloc(ssz);
	memset(gpt,0,ssz);
	if (!gpt)
		return NULL;
	if (read_lba(state, lba, (u8 *) gpt, ssz,block_device) < ssz) {
		free(gpt);
        gpt=NULL;
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
static int is_gpt_valid(struct parsed_partitions *state, u64 lba,
			gpt_header **gpt, gpt_entry **ptes,struct block_dev block_device)
{
	u32 crc, origcrc;
	u64 lastlba, pt_size;

	if (!ptes)
		return 0;
	if (!(*gpt = alloc_read_gpt_header(state, lba,block_device)))
		return 0;
	/* Check the GUID Partition Table signature */
	if ((*gpt)->signature!= GPT_HEADER_SIGNATURE) {
		printf("GUID Partition Table Header signature is wrong:"
			 "%lld != %lld\n",
			 (unsigned long long)(*gpt)->signature,
			 (unsigned long long)GPT_HEADER_SIGNATURE);
		goto fail;
	}

	/* Check the GUID Partition Table header size is too big */
	if ((*gpt)->header_size >
			state->logical_block_size) {
		printf("GUID Partition Table Header size is too large: %u > %u\n",
			(*gpt)->header_size,
			state->logical_block_size);
		goto fail;
	}

	/* Check the GUID Partition Table header size is too small */
	if ((*gpt)->header_size < sizeof(gpt_header)) {
		printf("GUID Partition Table Header size is too small: %u < %zu\n",
			(*gpt)->header_size,
			sizeof(gpt_header));
		goto fail;
	}

	/* Check the GUID Partition Table CRC */
	origcrc = (*gpt)->header_crc32;
	(*gpt)->header_crc32 = 0;
	crc = efi_crc32((const unsigned char *) (*gpt),(*gpt)->header_size);

	if (crc != origcrc) {
		printf("GUID Partition Table Header CRC is wrong: %x != %x\n",
			 crc, origcrc);
		goto fail;
	}
	(*gpt)->header_crc32 = origcrc;

	/* Check that the my_lba entry points to the LBA that contains
	 * the GUID Partition Table */
	if ((*gpt)->my_lba != lba) {
		printf("GPT my_lba incorrect: %lld != %lld\n",
			 (unsigned long long)(*gpt)->my_lba,
			 (unsigned long long)lba);
		goto fail;
	}

	/* Check the first_usable_lba and last_usable_lba are
	 * within the disk.
	 */
	lastlba = state->lastlba;
	if ((*gpt)->first_usable_lba> lastlba) {
		printf("GPT: first_usable_lba incorrect: %lld > %lld\n",
			 (unsigned long long)(*gpt)->first_usable_lba,
			 (unsigned long long)lastlba);
		goto fail;
	}
	if ((*gpt)->last_usable_lba > lastlba) {
		printf("GPT: last_usable_lba incorrect: %lld > %lld\n",
			 (unsigned long long)(*gpt)->last_usable_lba,
			 (unsigned long long)lastlba);
		goto fail;
	}
	if ((*gpt)->last_usable_lba < (*gpt)->first_usable_lba) {
		printf("GPT: last_usable_lba incorrect: %lld > %lld\n",
			 (unsigned long long)(*gpt)->last_usable_lba,
			 (unsigned long long)(*gpt)->first_usable_lba);
		goto fail;
	}
	/* Check that sizeof_partition_entry has the correct value */
	if ((*gpt)->sizeof_partition_entry != sizeof(gpt_entry)) {
		printf("GUID Partition Entry Size check failed.\n");
		goto fail;
	}

	/* Sanity check partition table size */
	pt_size = (u64)(*gpt)->num_partition_entries*
		(*gpt)->sizeof_partition_entry;
	if (pt_size > MALLOC_MAX_SIZE) {
		printf("GUID Partition Table is too large: %llu > %lu bytes\n",
			 (unsigned long long)pt_size, MALLOC_MAX_SIZE);
		goto fail;
	}

	if (!(*ptes = alloc_read_gpt_entries(state, *gpt,block_device)))
		goto fail;

	/* Check the GUID Partition Entry Array CRC */
	crc = efi_crc32((const unsigned char *) (*ptes), pt_size);

	if (crc !=(*gpt)->partition_entry_array_crc32) {
		printf("GUID Partition Entry Array CRC check failed.\n");
		goto fail_ptes;
	}

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

int efi_guidcmp (efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof (efi_guid_t));
}
static inline char *
efi_guid_to_str(efi_guid_t *guid, char *out)
{
	sprintf(out, "%pUl", guid->b);
        return out;
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
	    pte->starting_lba > lastlba         ||
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
 */
static void
compare_gpts(gpt_header *pgpt, gpt_header *agpt, u64 lastlba)
{
	int error_found = 0;
	if (!pgpt || !agpt)
		return;
	if (pgpt->my_lba!= agpt->alternate_lba) {
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
	if (pgpt->first_usable_lba!=
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
	if ((pgpt->sizeof_partition_entry) !=
         (agpt->sizeof_partition_entry)) {
		printf("GPT:sizeof_partition_entry values don't match: "
		       "0x%x != 0x%x\n",
                      pgpt->sizeof_partition_entry,
		       		  agpt->sizeof_partition_entry);
		error_found++;
	}
	if ((pgpt->partition_entry_array_crc32) !=
            (agpt->partition_entry_array_crc32)) {
		printf("GPT:partition_entry_array_crc32 values don't match: "
		       "0x%x != 0x%x\n",
                       (pgpt->partition_entry_array_crc32),
		       (agpt->partition_entry_array_crc32));
		error_found++;
	}
	if ((pgpt->alternate_lba) != lastlba) {
		printf("GPT:Primary header thinks Alt. header is not at the end of the disk.\n");
		printf("GPT:%lld != %lld\n",
			(unsigned long long)(pgpt->alternate_lba),
			(unsigned long long)lastlba);
		error_found++;
	}

	if ((agpt->my_lba) != lastlba) {
		printf("GPT:Alternate GPT header not at the end of the disk.\n");
		printf("GPT:%lld != %lld\n",
			(unsigned long long)(agpt->my_lba),
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
static int find_valid_gpt(struct parsed_partitions *state, gpt_header **gpt,
			  gpt_entry **ptes,struct block_dev block_device)
{
	int good_pgpt = 0, good_agpt = 0, good_pmbr = 0;
	gpt_header *pgpt = NULL, *agpt = NULL;
	gpt_entry *pptes = NULL, *aptes = NULL;
	legacy_mbr *legacymbr;
	state->total_sectors=block_device.num_sectors;
	u32 total_sectors = state->total_sectors;
	state->lastlba=state->total_sectors-1;
	u64 lastlba= state->lastlba;

	if (!ptes)
		return 0;

        if (!force_gpt) {
		/* This will be added to the EFI Spec. per Intel after v1.02. */
		legacymbr = malloc(sizeof(*legacymbr));
		memset(legacymbr,0,sizeof(*legacymbr));
		if (!legacymbr)
			goto fail;
		read_lba(state, 0, (u8 *)legacymbr, sizeof(*legacymbr),block_device);
		good_pmbr = is_pmbr_valid(legacymbr, total_sectors);
		free(legacymbr);

		if (!good_pmbr)
			goto fail;

		printf("Device has a %s MBR\n",
			 good_pmbr == GPT_MBR_PROTECTIVE ?
						"protective" : "hybrid");
	}

	good_pgpt = is_gpt_valid(state, GPT_PRIMARY_PARTITION_TABLE_LBA,&pgpt, &pptes,block_device);
        if (good_pgpt)
			good_agpt = is_gpt_valid(state,pgpt->alternate_lba,&agpt, &aptes,block_device);
        if (!good_agpt && force_gpt)
            good_agpt = is_gpt_valid(state, lastlba, &agpt, &aptes,block_device);

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
 * utf16_le_to_7bit(): Naively converts a UTF-16LE string to 7-bit ASCII characters
 * @in: input UTF-16LE string
 * @size: size of the input string
 * @out: output string ptr, should be capable to store @size+1 characters
 *
 * Description: Converts @size UTF16-LE symbols from @in string to 7-bit
 * ASCII characters and stores them to @out. Adds trailing zero to @out array.
 */
static void utf16_le_to_7bit(const u16 *in, unsigned int size, u8 *out)
{
	unsigned int i = 0;

	out[size] = 0;

	while (i < size) {
		u8 c = in[i] & 0xff;

		if (c && !isprint(c))
			c = '!';
		out[i] = c;
		i++;
	}
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
int efi_partition(struct parsed_partitions *state, struct block_dev block_device)
{
	gpt_header *gpt = NULL;
	gpt_entry *ptes = NULL;
	u32 i;
	unsigned ssz = state->logical_block_size / 512;

	if (!find_valid_gpt(state, &gpt, &ptes,block_device) || !gpt || !ptes) {
		free(gpt);
		free(ptes);
		return 0;
	}
	state->partition_type="efi_partiotion";
	printf("GUID Partition Table is valid!  Yea!\n");
	for (i = 0; i < gpt->num_partition_entries && i < state->limit-1; i++) {
		struct partition_meta_info *info;
		unsigned label_max;
		u64 start =ptes[i].starting_lba;
		u64 size = ptes[i].ending_lba -ptes[i].starting_lba + 1ULL;

		if (!is_pte_valid(&ptes[i], state->lastlba))
			continue;

		put_partition(state, i+1, start * ssz, size * ssz);
		printf("partion %d's start_block is: %u\n ",i+1,state->parts[i+1].from);
		printf("partion %d's size_block is: %u\n ",i+1,state->parts[i+1].size);
		state->sum=i+1;
		/* If this is a only-read partition,tell md */
		if(ptes[i].attributes.type_guid_specific==4096)
			state->parts[i+1].ro=1;
		/* If this is a RAID volume, tell md */
		if (!efi_guidcmp(ptes[i].partition_type_guid, PARTITION_LINUX_RAID_GUID))
			state->parts[i + 1].flags = ADDPART_FLAG_RAID;

		info = &state->parts[i + 1].info;
		efi_guid_to_str(&ptes[i].unique_partition_guid, info->uuid);

		/* Naively convert UTF16-LE to 7 bits. */
		label_max = min(ARRAY_SIZE(info->volname) - 1,
				ARRAY_SIZE(ptes[i].partition_name));
		utf16_le_to_7bit(ptes[i].partition_name, label_max, info->volname);
		state->parts[i + 1].has_info = true;
		printf("partion %d's volname is %s\n",i+1,info->volname);
	}
	free(ptes);
	free(gpt);
	return 1;
}
