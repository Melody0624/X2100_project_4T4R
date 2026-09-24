#ifndef FS_PART_EFI_H_INCLUDED
#define FS_PART_EFI_H_INCLUDED

#include "partition.h"

#define MSDOS_MBR_SIGNATURE 0xaa55
#define EFI_PMBR_OSTYPE_EFI 0xEF
#define EFI_PMBR_OSTYPE_EFI_GPT 0xEE

#define GPT_MBR_PROTECTIVE  1
#define GPT_MBR_HYBRID      2

#define GPT_HEADER_SIGNATURE 0x5452415020494645ULL
#define GPT_HEADER_REVISION_V1 0x00010000
#define GPT_PRIMARY_PARTITION_TABLE_LBA 1

typedef struct {
	u8 b[16];
} efi_guid_t;

#define EFI_GUID(a, b, c, d...) (efi_guid_t){ {					\
	(a) & 0xff, ((a) >> 8) & 0xff, ((a) >> 16) & 0xff, ((a) >> 24) & 0xff,	\
	(b) & 0xff, ((b) >> 8) & 0xff,						\
	(c) & 0xff, ((c) >> 8) & 0xff, d } }

#define NULL_GUID				EFI_GUID(0x00000000, 0x0000, 0x0000,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)

typedef struct _gpt_header {
	u64 signature;
	u32 revision;
	u32 header_size;
	u32 header_crc32;
	u32 reserved1;
	u64 my_lba;
	u64 alternate_lba;
	u64 first_usable_lba;
	u64 last_usable_lba;
	efi_guid_t disk_guid;
	u64 partition_entry_lba;
	u32 num_partition_entries;
	u32 sizeof_partition_entry;
	u32 partition_entry_array_crc32;

	/* The rest of the logical block is reserved by UEFI and must be zero.
	 * EFI standard handles this by:
	 *
	 * uint8_t		reserved2[ BlockSize - 92 ];
	 */
} __packed gpt_header;

typedef struct _gpt_entry_attributes {
	u64 required_to_function:1;
	u64 reserved:47;
	u64 type_guid_specific:16;
} __packed gpt_entry_attributes;

typedef struct _gpt_entry {
	efi_guid_t partition_type_guid;
	efi_guid_t unique_partition_guid;
	u64 starting_lba;
	u64 ending_lba;
	gpt_entry_attributes attributes;
	u16 partition_name[72/sizeof(u16)];
} __packed gpt_entry;

typedef struct _gpt_mbr_record {
	u8	boot_indicator; /* unused by EFI, set to 0x80 for bootable */
	u8	start_head;     /* unused by EFI, pt start in CHS */
	u8	start_sector;   /* unused by EFI, pt start in CHS */
	u8	start_track;
	u8	os_type;        /* EFI and legacy non-EFI OS types */
	u8	end_head;       /* unused by EFI, pt end in CHS */
	u8	end_sector;     /* unused by EFI, pt end in CHS */
	u8	end_track;      /* unused by EFI, pt end in CHS */
	u32	starting_lba;   /* used by EFI - start addr of the on disk pt */
	u32	size_in_lba;    /* used by EFI - size of pt in LBA */
} __packed gpt_mbr_record;


typedef struct _legacy_mbr {
	u8 boot_code[440];
	u32 unique_mbr_signature;
	u16 unknown;
	gpt_mbr_record partition_record[4];
	u16 signature;
} __packed legacy_mbr;

static inline int efi_guidcmp (efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof (efi_guid_t));
}

#endif
