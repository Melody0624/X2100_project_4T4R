#ifndef _MMC_PARTITION_FAT_H_
#define _MMC_PARTITION_FAT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <ctype.h>

/*
 * copy from elmfat/ff.c
 * FAT structure
 */
#define BS_JmpBoot                      0   /* x86 jump instruction (3-byte) */
#define BS_OEMName                      3   /* OEM name (8-byte) */
#define BPB_BytsPerSec                  11  /* Sector size [byte] (WORD) */
#define BPB_SecPerClus                  13  /* Cluster size [sector] (BYTE) */
#define BPB_RsvdSecCnt                  14  /* Size of reserved area [sector] (WORD) */
#define BPB_NumFATs                     16  /* Number of FATs (BYTE) */
#define BPB_RootEntCnt                  17  /* Size of root directory area for FAT [entry] (WORD) */
#define BPB_TotSec16                    19  /* Volume size (16-bit) [sector] (WORD) */
#define BPB_Media                       21  /* Media descriptor byte (BYTE) */
#define BPB_FATSz16                     22  /* FAT size (16-bit) [sector] (WORD) */
#define BPB_SecPerTrk                   24  /* Number of sectors per track for int13h [sector] (WORD) */
#define BPB_NumHeads                    26  /* Number of heads for int13h (WORD) */
#define BPB_HiddSec                     28  /* Volume offset from top of the drive (DWORD) */
#define BPB_TotSec32                    32  /* Volume size (32-bit) [sector] (DWORD) */
#define BS_DrvNum                       36  /* Physical drive number for int13h (BYTE) */
#define BS_NTres                        37  /* WindowsNT error flag (BYTE) */
#define BS_BootSig                      38  /* Extended boot signature (BYTE) */
#define BS_VolID                        39  /* Volume serial number (DWORD) */
#define BS_VolLab                       43  /* Volume label string (8-byte) */
#define BS_FilSysType                   54  /* Filesystem type string (8-byte) */
#define BS_BootCode                     62  /* Boot code (448-byte) */
#define BS_55AA                         510 /* Signature word (WORD) */

#define BPB_FATSz32                     36  /* FAT32: FAT size [sector] (DWORD) */
#define BPB_ExtFlags32                  40  /* FAT32: Extended flags (WORD) */
#define BPB_FSVer32                     42  /* FAT32: Filesystem version (WORD) */
#define BPB_RootClus32                  44  /* FAT32: Root directory cluster (DWORD) */
#define BPB_FSInfo32                    48  /* FAT32: Offset of FSINFO sector (WORD) */
#define BPB_BkBootSec32                 50  /* FAT32: Offset of backup boot sector (WORD) */
#define BS_DrvNum32                     64  /* FAT32: Physical drive number for int13h (BYTE) */
#define BS_NTres32                      65  /* FAT32: Error flag (BYTE) */
#define BS_BootSig32                    66  /* FAT32: Extended boot signature (BYTE) */
#define BS_VolID32                      67  /* FAT32: Volume serial number (DWORD) */
#define BS_VolLab32                     71  /* FAT32: Volume label string (8-byte) */
#define BS_FilSysType32                 82  /* FAT32: Filesystem type string (8-byte) */
#define BS_BootCode32                   90  /* FAT32: Boot code (420-byte) */

#define BPB_ZeroedEx                    11  /* exFAT: MBZ field (53-byte) */
#define BPB_VolOfsEx                    64  /* exFAT: Volume offset from top of the drive [sector] (QWORD) */
#define BPB_TotSecEx                    72  /* exFAT: Volume size [sector] (QWORD) */
#define BPB_FatOfsEx                    80  /* exFAT: FAT offset from top of the volume [sector] (DWORD) */
#define BPB_FatSzEx                     84  /* exFAT: FAT size [sector] (DWORD) */
#define BPB_DataOfsEx                   88  /* exFAT: Data offset from top of the volume [sector] (DWORD) */
#define BPB_NumClusEx                   92  /* exFAT: Number of clusters (DWORD) */
#define BPB_RootClusEx                  96  /* exFAT: Root directory start cluster (DWORD) */
#define BPB_VolIDEx                     100 /* exFAT: Volume serial number (DWORD) */
#define BPB_FSVerEx                     104 /* exFAT: Filesystem version (WORD) */
#define BPB_VolFlagEx                   106 /* exFAT: Volume flags (WORD) */
#define BPB_BytsPerSecEx                108 /* exFAT: Log2 of sector size in unit of byte (BYTE) */
#define BPB_SecPerClusEx                109 /* exFAT: Log2 of cluster size in unit of sector (BYTE) */
#define BPB_NumFATsEx                   110 /* exFAT: Number of FATs (BYTE) */
#define BPB_DrvNumEx                    111 /* exFAT: Physical drive number for int13h (BYTE) */
#define BPB_PercInUseEx                 112 /* exFAT: Percent in use (BYTE) */
#define BPB_RsvdEx                      113 /* exFAT: Reserved (7-byte) */
#define BS_BootCodeEx                   120 /* exFAT: Boot code (390-byte) */

struct exfat_ctx {
    uint64_t vol_start;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t fat_ofs;
    uint32_t data_ofs;
    uint32_t num_clusters;
};

/* Load a 2-byte little-endian word */
static inline uint16_t ld_word(const uint8_t* ptr)
{
    uint16_t rv;

    rv = ptr[1];
    rv = rv << 8 | ptr[0];
    return rv;
}

/* Load a 4-byte little-endian word */
static inline uint32_t ld_dword(const uint8_t* ptr)
{
    uint32_t rv;

    rv = ptr[3];
    rv = rv << 8 | ptr[2];
    rv = rv << 8 | ptr[1];
    rv = rv << 8 | ptr[0];
    return rv;
}

/* Load an 8-byte little-endian word */
static inline uint64_t ld_qword(const uint8_t* ptr)
{
    uint64_t rv;

    rv = ptr[7];
    rv = rv << 8 | ptr[6];
    rv = rv << 8 | ptr[5];
    rv = rv << 8 | ptr[4];
    rv = rv << 8 | ptr[3];
    rv = rv << 8 | ptr[2];
    rv = rv << 8 | ptr[1];
    rv = rv << 8 | ptr[0];

    return rv;
}

/* Load a label string from a FAT VBR.*/
static inline int ld_label(const uint8_t *src, size_t src_len, char *out, size_t out_len)
{
    size_t i, end = 0;

    if (!out || out_len == 0)
        return -EINVAL;

    for (i = 0; i < src_len; i++) {
        if (src[i] != ' ')
            end = i + 1;
    }

    if (end == 0) {
        out[0] = '\0';
        return -ENOENT;
    }
    if (end + 1 > out_len)
        return -ENOSPC;

    for (i = 0; i < end; i++) {
        char c = (char)src[i];
        if (!isprint((unsigned char)c))
            c = '?';
        out[i] = c;
    }
    out[end] = '\0';

    return 0;
}

static inline int fat_is_fat32(const uint8_t *b)
{
    return (ld_word(b + BPB_FATSz16) == 0);
}

static inline int fat_is_valid_vbr(const uint8_t *b)
{
    return ld_word(b + BS_55AA) == 0xAA55;
}

static inline int exfat_is_valid_vbr(const uint8_t *b)
{
    if (!fat_is_valid_vbr(b))
        return 0;

    return (memcmp(b, "\xEB\x76\x90" "EXFAT   ", 11) == 0);
}

/* parse exFAT VBR and initialize context */
static inline int exfat_ctx_init(struct exfat_ctx *ctx, const uint8_t *boot,
                                 uint64_t part_offset, uint32_t *root_cluster)
{
    if (!ctx || !boot || !root_cluster)
        return -EINVAL;

    if (!exfat_is_valid_vbr(boot))
        return -EINVAL;

    ctx->bytes_per_sector = 1u << boot[BPB_BytsPerSecEx];
    ctx->sectors_per_cluster = 1u << boot[BPB_SecPerClusEx];
    ctx->fat_ofs = ld_dword(boot + BPB_FatOfsEx);
    ctx->data_ofs = ld_dword(boot + BPB_DataOfsEx);
    ctx->num_clusters = ld_dword(boot + BPB_NumClusEx);
    *root_cluster = ld_dword(boot + BPB_RootClusEx);
    ctx->vol_start = part_offset;

    if (ctx->bytes_per_sector == 0 || ctx->sectors_per_cluster == 0)
        return -EINVAL;

    if (*root_cluster < 2 || *root_cluster >= ctx->num_clusters + 2)
        return -EINVAL;

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _MMC_PARTITION_FAT_H_ */