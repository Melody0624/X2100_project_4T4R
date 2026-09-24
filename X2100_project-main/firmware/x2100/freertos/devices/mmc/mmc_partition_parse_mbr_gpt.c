#include <malloc.h>
#include "mmc_devices.h"
#include "mmc_partition.h"
#include "mmc_partition_gpt.h"
#include "mmc_partition_mbr.h"
#include "mmc_partition_fat.h"
#include <ctype.h>
//#define DEBUG_MBR_GPT_PARTITIONS
//#define DEBUG_PARTITION_PARAMS

#ifdef CONFIG_DFS_ELMFAT

#define EMMC_DEVICE_MAX_PARTITION_NUM       8  /* fatfs当前支持挂载的最大分区个数为 10 (ramdisk /sys 各占一个) */

/*
 * device 需要实现
 */
struct mmc_card *mmc_devices_info(void);
uint32_t mmc_device_block_read(uint64_t from, uint32_t len, uint8_t *buf);
int emmc_device_get_partition_sector_size(uint64_t pos, uint32_t size);

#ifdef DEBUG_PARTITION_PARAMS
static void dump_partition_params(struct block_device_partition *parts)
{
    struct block_device_partition *p_parts = NULL;

    if (parts == NULL)
        printf("No valid partition info\n");

    printf("Mount Partition Param\n");
    printf("Name\t\t   offset\t      size\t         sector_size\t  mask\n");

    for (p_parts = parts; p_parts->name != NULL; p_parts++) {
        printf("%-15s 0x%-16llx 0x%-16llx 0x%08x\t  0x%08x\n",
                p_parts->name,
                p_parts->offset,
                p_parts->size,
                p_parts->sector_size,
                p_parts->mask_flags);
    }
}
#endif

#ifdef DEBUG_MBR_GPT_PARTITIONS

/* 本地函数声明 */
static char *get_partition_type_string(uint8_t type);

static char get_boot_char(uint8_t boot)
{
    char value = '?';

    switch (boot) {
    case 0x00: value = 'N';  break;
    case 0x80: value = 'Y';  break;
    default:   value = '?';  break;
    }

    return value;
}

static inline void print_human_size(float size)
{
    if (!size) {
        printf(" %d", 0);
        return ;
    }

    char a = 'T';
    if (size < 1024.0) a = 'B';
    else if ((size /= 1024.0) < 1024.0) a = 'K';
    else if ((size /= 1024.0) < 1024.0) a = 'M';
    else if ((size /= 1024.0) < 1024.0) a = 'G';
    else size /= 1024.0;

    printf(" %.1f %c", size, a);
}

static inline void debug_dump_mbr_partition_info(struct dos_partition *p)
{
    int is_exfat = 0;

    /* NTFS 和 exFAT 标识符相同,进一步判断文件系统类型 */
    if (p->sys_ind == MBR_WINDOWS_EXFAT_PARTITION) {

        uint8_t mbr_buffer[512];
        int offset = dos_partition_get_start(p) * 512;

        /* 从存储介质中读取entrys 信息 */
        int ret = mmc_device_block_read(offset, sizeof(mbr_buffer), mbr_buffer);
        if (ret < 0) {
            printf("dump mbr partition info emmc device get  (device offset) (%d) buffer content failed\n", offset);
            return ;
        }

        if (!memcmp(mbr_buffer, "\xEB\x76\x90" "EXFAT   ", 11))
            is_exfat = 1;

    }

    printf("MBR Boot \t : %c (0x%x)\n", get_boot_char(p->boot_ind), p->boot_ind);
    printf("MBR Type \t : %s (0x%x)\n", is_exfat ? "Windows exFAT" : get_partition_type_string(p->sys_ind), p->sys_ind);
    printf("MBR LBA  \t : 0x%x (unit: 512 Byte)\n", dos_partition_get_start(p));
    printf("MBR Sectors\t : 0x%x ", dos_partition_get_size(p));
    print_human_size( (float)((unsigned long long)dos_partition_get_size(p) * 512));
    printf("\n");
    printf("\n");
}
#endif

/******************************************************************************
 * FAT/exFAT文件系统 解析卷标
 */

static int fat_read_volume_label(uint8_t *boot, uint64_t part_offset, char *out, size_t out_len)
{
    int ret;

    if (!boot || !out || out_len == 0)
        return -EINVAL;

    if (!fat_is_valid_vbr(boot))
        return -EINVAL;

    if (fat_is_fat32(boot))
        ret = ld_label(boot + BS_VolLab32, 11, out, out_len);
    else
        ret = ld_label(boot + BS_VolLab, 11, out, out_len);

    return (ret < 0) ? ret : 0;
}

static int exfat_read_fat_entry(struct exfat_ctx *ctx, uint32_t cluster, uint32_t *next)
{
    uint64_t fat_start = ctx->vol_start + (uint64_t)ctx->fat_ofs * ctx->bytes_per_sector;
    uint64_t entry_off = (uint64_t)cluster * 4;
    uint64_t abs_off = fat_start + entry_off;
    uint32_t off_in_sector = (uint32_t)(abs_off % ctx->bytes_per_sector);
    uint8_t *sec;
    int ret;

    sec = (uint8_t *)malloc(ctx->bytes_per_sector);
    if (!sec)
        return -ENOMEM;

    ret = mmc_device_block_read(abs_off - off_in_sector, ctx->bytes_per_sector, sec);
    if (ret < 0) {
        free(sec);
        return -EIO;
    }

    if (off_in_sector <= ctx->bytes_per_sector - 4) {
        *next = ld_dword(sec + off_in_sector);
        free(sec);
        return 0;
    }

    uint8_t sec2[4];
    uint8_t *sec_next;
    int need = 4 - (ctx->bytes_per_sector - off_in_sector);

    memcpy(sec2, sec + off_in_sector, ctx->bytes_per_sector - off_in_sector);
    sec_next = (uint8_t *)malloc(ctx->bytes_per_sector);
    if (!sec_next) {
        free(sec);
        return -ENOMEM;
    }

    ret = mmc_device_block_read(abs_off - off_in_sector + ctx->bytes_per_sector,
                                ctx->bytes_per_sector, sec_next);
    if (ret < 0) {
        free(sec);
        free(sec_next);
        return -EIO;
    }

    memcpy(sec2 + (ctx->bytes_per_sector - off_in_sector), sec_next, need);
    *next = ld_dword(sec2);

    free(sec);
    free(sec_next);
    return 0;
}

static int exfat_read_cluster(struct exfat_ctx *ctx, uint32_t cluster, uint8_t *buf)
{
    uint64_t cluster0_lba = (ctx->vol_start / ctx->bytes_per_sector) + ctx->data_ofs;
    uint64_t lba = cluster0_lba + (uint64_t)(cluster - 2) * ctx->sectors_per_cluster;
    uint64_t offset = lba * ctx->bytes_per_sector;
    uint32_t size = ctx->bytes_per_sector * ctx->sectors_per_cluster;

    if (mmc_device_block_read(offset, size, buf) < 0)
        return -EIO;
    return 0;
}

static int exfat_read_volume_label(uint8_t *boot, uint64_t part_offset, char *out, size_t out_len)
{
    struct exfat_ctx ctx = {0};
    uint8_t *cluster_buf;
    uint32_t root_cluster;
    uint32_t cluster;
    uint32_t iter = 0;
    uint32_t cluster_bytes;
    int ret;

    if (!boot || !out || out_len <= 0)
        return -EINVAL;

    ret = exfat_ctx_init(&ctx, boot, part_offset, &root_cluster);
    if (ret < 0)
        return ret;

    cluster_bytes = ctx.bytes_per_sector * ctx.sectors_per_cluster;
    cluster_buf = (uint8_t *)malloc(cluster_bytes);
    if (!cluster_buf)
        return -ENOMEM;

    cluster = root_cluster;
    while (cluster >= 2 && cluster < ctx.num_clusters + 2 && iter++ < ctx.num_clusters) {
        size_t off;

        ret = exfat_read_cluster(&ctx, cluster, cluster_buf);
        if (ret < 0) {
            free(cluster_buf);
            return ret;
        }

        for (off = 0; off + 32 <= cluster_bytes; off += 32) {
            uint8_t type = cluster_buf[off];

            if (type == 0x00) {
                free(cluster_buf);
                return -ENOENT;
            }

            if (type == 0x83) {
                uint8_t name_len = cluster_buf[off + 1];
                size_t i, out_i = 0;

                if (name_len > 11)
                    name_len = 11;

                if (name_len + 1 > out_len) {
                    free(cluster_buf);
                    return -ENOSPC;
                }

                for (i = 0; i < name_len; i++) {
                    uint16_t wc = (uint16_t)cluster_buf[off + 2 + 2 * i] |
                                  ((uint16_t)cluster_buf[off + 3 + 2 * i] << 8);
                    char c = (char)(wc & 0xFF);
                    if (!isprint((unsigned char)c))
                        c = '?';
                    out[out_i++] = c;
                }
                out[out_i] = '\0';
                free(cluster_buf);
                return 0;
            }
        }

        uint32_t next;
        ret = exfat_read_fat_entry(&ctx, cluster, &next);
        if (ret < 0) {
            free(cluster_buf);
            return ret;
        }

        if (next >= 0xFFFFFFF8u || next == 0xFFFFFFFFu)
            break;

        if (next < 2 || next >= ctx.num_clusters + 2)
            break;

        cluster = next;
    }

    free(cluster_buf);
    return -ENOENT;
}

/******************************************************************************
 * 分区表不包含分区名(如MBR) 或 没有分区表:
 *  解析 0地址是否为有效FAT/exFAT文件系统, 是则从中解析分区名
 */

static int check_fs(uint8_t *buffer);

static char *alloc_partition_label_from_fs(uint64_t part_offset, uint32_t sector_size)
{
    int label_len = 12;
    int fs_type;
    int ret = -EINVAL;

    if (sector_size < 512)
        sector_size = 512;

    uint8_t *boot = malloc(sector_size);
    if (!boot)
        return NULL;

    char *label = malloc(label_len);
    if (!label)
        goto err_label;

    if (mmc_device_block_read(part_offset, sector_size, boot) < 0) {
        printf("read vbr failed at offset %llu\n", part_offset);
        goto err_read;
    }

    fs_type = check_fs(boot);
    memset(label, 0x00, label_len);

    switch (fs_type) {
    case 1:
        /* FAT/FAT32 */
        ret = fat_read_volume_label(boot, part_offset, label, label_len);
        break;

    case 2:
        /* exFAT */
        ret = exfat_read_volume_label(boot, part_offset, label, label_len);
        break;

    default:
        break;
    }

    if (ret < 0)
        goto err_read;

    free(boot);
    return label;
err_read:
    free(label);
err_label:
    free(boot);
    return NULL;
}


static char *get_partition_type_string(uint8_t type)
{
    switch (type) {
    case MBR_EMPTY_PARTITION:               return "Empty partition";
    case MBR_FAT12_PARTITION:               return "FAT12";
    case MBR_FAT16_LESS32M_PARTITION:       return "FAT16 < 32M";
    case MBR_DOS_EXTENDED_PARTITION:        return "Extended";
    case MBR_FAT16_PARTITION:               return "FAT16";
    case MBR_HPFS_NTFS_PARTITION:           return "NTFS";
    case MBR_W95_FAT32_PARTITION:           return "WIN95 FAT32";
    case MBR_W95_FAT32_LBA_PARTITION:       return "WIN95 FAT32 (LBA)";
    case MBR_W95_FAT16_LBA_PARTITION:       return "WIN95 FAT16 (LBA)";
    case MBR_W95_EXTENDED_PARTITION:        return "WIN95 Ext'd (LBA)";
    case MBR_HIDDEN_FAT12_PARTITION:        return "Hidden FAT12";
    case MBR_HIDDEN_FAT16_L32M_PARTITION:   return "Hidden FAT16<32M";
    case MBR_HIDDEN_FAT16_PARTITION:        return "Hidden FAT16";
    case MBR_HIDDEN_HPFS_NTFS_PARTITION:    return "Hidden NTFS";
    case MBR_HIDDEN_W95_FAT32_PARTITION:    return "Hidden WIN95 FAT32";
    case MBR_HIDDEN_W95_FAT32LBA_PARTITION: return "Hidden WIN95 FAT32 (LBA)";
    case MBR_HIDDEN_W95_FAT16LBA_PARTITION: return "Hidden WIN95 FAT16 (LBA)";
    case MBR_LINUX_SWAP_PARTITION:          return "Linux Swap";
    case MBR_LINUX_DATA_PARTITION:          return "Linux";
    case MBR_LINUX_EXTENDED_PARTITION:      return "Linux Ext'd";
    case MBR_NTFS_VOL_SET1_PARTITION:       return "NTFS Vol. Set";
    case MBR_NTFS_VOL_SET2_PARTITION:       return "NTFS Vol. Set";
    case MBR_LINUX_LVM_PARTITION:           return "Linux LVM";
    case MBR_BSD_OS_PARTITION:              return "BSD/OS";
    case MBR_FREEBSD_PARTITION:             return "FreeBSD";
    case MBR_OPENBSD_PARTITION:             return "OpenBSD";
    case MBR_NETBSD_PARTITION:              return "NetBSD";
    case MBR_BEOS_FS_PARTITION:             return "BeOS fs";
    case MBR_GPT_PARTITION:                 return "EFI GPT";
    case MBR_EFI_SYSTEM_PARTITION:          return "EFI FAT?";
    default:                                return "????????";
    }
}

/*
 * part_index     : 分区索引
 */

static inline char *alloc_partition_name(int part_index)
{
    /* 在设备卸载时释放 */
    int name_len = 64;
    char *part_name = malloc(name_len);
    assert(part_name);

    memset(part_name, 0x00, name_len);

    struct mmc_card *card = mmc_devices_info();
    if (card == NULL) {
        sprintf(part_name, "%sp%d", "Unkonw", part_index);
    } else {
        sprintf(part_name, "mmcblk%dp%d", card->host->index, part_index);
    }

    return part_name;
}

/**
 * alloc_utf16_le_to_7bit(): Naively converts a UTF-16LE string to 7-bit ASCII characters
 * @in: input UTF-16LE string
 * @size: size of the input string
 *
 * Description: Converts @size UTF16-LE symbols from @in string to 7-bit
 * ASCII characters and stores them to @out. Adds trailing zero to @out array.
 */
static char *alloc_utf16_le_to_7bit(const u16 *in, unsigned int size)
{
	unsigned int i = 0;

	char *out = malloc(size);
    memset(out, 0, size);

	while (i < size) {
		char c = in[i] & 0xff;

		if (c && !isprint(c))
			c = '!';
		out[i] = c;
		i++;
	}
    return out;
}

/******************************************************************************
 * 无分区表信息, 解析 0地址是否为有效FAT/exFAT文件系统, 有效则解析其信息
 *
 * 最大只有一个有效分区
 */

static int try_support_fat_partition(uint8_t *buffer, struct block_device_partition *parts, int part_max_num)
{
    /* FAT/FAT32 info */
    int sector_size = ld_word(buffer + BPB_BytsPerSec);

    uint32_t offset_sect = 0;
    uint64_t totle_sect = ld_word(buffer + BPB_TotSec16);  /* Number of sectors on the volume */
    if (totle_sect == 0)
        totle_sect = ld_dword(buffer + BPB_TotSec32);

    uint64_t start_offset = offset_sect * sector_size;
    uint64_t part_size = totle_sect * sector_size;

    int index = 0;
    parts[index].name       = alloc_partition_name(index);
    parts[index].part_name  = alloc_partition_label_from_fs(start_offset, sector_size);
    parts[index].offset     = start_offset;
    parts[index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
    parts[index].size       = part_size;
    parts[index].sector_size= sector_size;

    return 0;
}


static int try_support_exfat_partition(uint8_t *buffer, struct block_device_partition *parts, int part_max_num)
{
    /* exFAT info */
    uint64_t offset_sect = 0;
    uint64_t totle_sect = ld_qword(&buffer[BPB_TotSecEx]);
    uint8_t exfat_byte_per_sec = buffer[BPB_BytsPerSecEx];

    int sector_size = 1 << exfat_byte_per_sec;
    uint64_t start_offset = offset_sect * sector_size;
    uint64_t part_size = totle_sect * sector_size;

    int index = 0;
    parts[index].name       = alloc_partition_name(index);
    parts[index].part_name  = alloc_partition_label_from_fs(start_offset, sector_size);
    parts[index].offset     = start_offset;
    parts[index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
    parts[index].size       = part_size;
    parts[index].sector_size= sector_size;

    return 0;
}


/*
 * return: =1: 文件系统为 FAT/FAT32格式
 *         =2: 文件系统为 exFAT格式
 *         <0: 文件系统无效/类型不支持
 */
static int check_fs(uint8_t *buffer)
{
    uint16_t sign = ld_word(buffer + BS_55AA);

    /* FAT 结束标志 检查 */
    if (sign != 0xAA55)
        return -1;

    /* FS类型是否为 : exFAT */
    if (!memcmp(buffer, "\xEB\x76\x90" "EXFAT   ", 11)) {    /* It is an exFAT VBR */
        return 2;
    }

    uint8_t b = buffer[BS_JmpBoot];
    if (b == 0xEB || b == 0xE9 || b == 0xE8) {    /* Valid JumpBoot code? (short jump, near jump or near call) */
        if (!memcmp(buffer + BS_FilSysType32, "FAT32   ", 8)) {
            return 1;    /* It is an FAT32 VBR */
        }

        /* FAT volumes formatted with early MS-DOS lack BS_55AA and BS_FilSysType,
         * so FAT VBR needs to be identified without them.
         */
        uint16_t w = ld_word(buffer + BPB_BytsPerSec);
        b = buffer[BPB_SecPerClus];

        uint32_t power = w / 512;
        if ((w & (w - 1)) == 0 && w >= 512 && w <= 8192 /* Properness of sector size (512-8192 and 2^n) */
            && b != 0 && (b & (b - 1)) == 0             /* Properness of cluster size (2^n) */
            && ld_word(buffer + BPB_RsvdSecCnt) != 0    /* Properness of reserved sectors (MNBZ) */
            && (uint16_t)buffer[BPB_NumFATs] - 1 <= 1   /* Properness of FATs (1 or 2) */
            && ld_word(buffer + BPB_RootEntCnt) != 0    /* Properness of root dir entries (MNBZ) */
            && ((ld_word(buffer + BPB_TotSec16) * power) >= 128 || (ld_dword(buffer + BPB_TotSec32) * power)>= 0x10000)    /* Properness of volume sectors (>=128) */
            && ld_word(buffer + BPB_FATSz16) != 0) {    /* Properness of FAT size (MNBZ) */
                return 1;    /* It can be presumed an FAT VBR */
        }
    }

    return -1;
}


static int try_support_fs_partition(uint8_t *buffer, struct block_device_partition *parts, int part_max_num)
{
    int ret;
    int fs_type;

    fs_type = check_fs(buffer);
    if (fs_type < 0) {
        printf("filesystem type is invalid or not support\n");
        return -1;
    }

    switch (fs_type) {
    case 1:
        /* FAT/FAT32 */
        ret = try_support_fat_partition(buffer, parts, part_max_num);
        break;

    case 2:
        /* exFAT */
        ret = try_support_exfat_partition(buffer, parts, part_max_num);
        break;

    default:
        ret = -1;
        break;
    }

    return ret;
}


/******************************************************************************
 * MBR 格式解析
 */

/*
 * parts           : 分区表记录变量
 * part_index      : 分区信息存储索引, 使用扩展分区填充信息，索引号需增加
 * part_max_num    : 文件系统运行mount的最大分区个数
 * start_lba_offset: 分区信息存储偏移, 单位 512字节
 */
static void fill_mbr_extended_partition(struct block_device_partition *parts,
            int *part_index, int part_max_num, uint64_t start_lba_offset)
{
    uint8_t ebr[512]; /* extended boot record buffer */
    int mbr_sector_size = 512;
    uint64_t offset = start_lba_offset * mbr_sector_size;
    int ret;

    /* 从存储介质的指定偏移地址处读取保存数据 */
    ret = mmc_device_block_read(offset, sizeof(ebr), ebr);
    if (ret < 0) {
        printf("emmc device get mbr extended(device offset) (%lld) buffer content failed\n", offset);
        goto out;
    }

    /* 超过fatfs支持的最大分区数,则不添加分区信息 */
    int index = *part_index + 1;
    if (index > part_max_num)
        goto out;

    /* 注册分区表数 加1 */
    *part_index = index;

    struct dos_partition *p = mbr_get_partition(ebr, 0);

#ifdef DEBUG_MBR_GPT_PARTITIONS
    debug_dump_mbr_partition_info(p);
#endif

    uint64_t start_offset = (uint64_t)dos_partition_get_start(p) * mbr_sector_size + offset;
    uint64_t part_size = (uint64_t)dos_partition_get_size(p) * mbr_sector_size;
    int sector_size = emmc_device_get_partition_sector_size(start_offset, mbr_sector_size);

    parts[index].name       = alloc_partition_name(index);
    parts[index].part_name  = alloc_partition_label_from_fs(start_offset, sector_size);
    parts[index].offset     = start_offset;
    parts[index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
    parts[index].size       = part_size;
    parts[index].sector_size= sector_size;

    p = mbr_get_partition(ebr, 1);	/* EBR中分区表表项二是描述下个子扩展分区的位置的 */
    if (IS_EXTENDED(p->sys_ind)) {
        fill_mbr_extended_partition(parts, part_index, part_max_num, start_lba_offset + dos_partition_get_start(p));
    }

out:
    return;
}

static void fill_mbr_primary_partition(uint8_t *mbr, struct block_device_partition *parts, int part_max_num)
{
    int i;
    int part_index = -1;  /* 初始值为 -1, 第一次注册分区时候 +1，则第一个分区索引为0 */

    /* MBR最多支持4个分区 */
    for (i = 0; i < 4; i++) {
        struct dos_partition *p = mbr_get_partition(mbr, i);

        /* 不是有效的分区跳过 */
        if (dos_partition_get_start(p) == 0)
            continue;

        if (IS_EXTENDED(p->sys_ind)) {  /* extended partition */
            uint64_t extended_partition_lba_offset = dos_partition_get_start(p);
            fill_mbr_extended_partition(parts, &part_index, part_max_num, extended_partition_lba_offset);
        } else {
#ifdef DEBUG_MBR_GPT_PARTITIONS
            debug_dump_mbr_partition_info(p);
#endif

            part_index++;
            /* 超过fatfs支持的最大分区数,则不添加分区信息 */
            if (part_index > part_max_num)
                break;

            /* MBR扇区大小默认为512 */
            int mbr_sector_size = 512;
            uint64_t start_offset = (uint64_t)dos_partition_get_start(p) * mbr_sector_size;
            uint64_t part_size = (uint64_t)dos_partition_get_size(p) * mbr_sector_size;
            int sector_size = emmc_device_get_partition_sector_size(start_offset, mbr_sector_size);

            parts[part_index].name       = alloc_partition_name(part_index);
            parts[part_index].part_name  = alloc_partition_label_from_fs(start_offset, sector_size);
            parts[part_index].offset     = start_offset;
            parts[part_index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
            parts[part_index].size       = part_size;
            parts[part_index].sector_size= sector_size;
        }
    }
}


/******************************************************************************
 * GPT 格式解析
 */

#ifdef DEBUG_MBR_GPT_PARTITIONS
static inline void debug_dump_gpt_partition_header(struct gpt_header *gpthdr)
{
    int gpt_sector_size = 512;

    char a = gpthdr->signature >> (0 * 8) & 0xff;
    char b = gpthdr->signature >> (1 * 8) & 0xff;
    char c = gpthdr->signature >> (2 * 8) & 0xff;
    char d = gpthdr->signature >> (3 * 8) & 0xff;
    char e = gpthdr->signature >> (4 * 8) & 0xff;
    char f = gpthdr->signature >> (5 * 8) & 0xff;
    char g = gpthdr->signature >> (6 * 8) & 0xff;
    char h = gpthdr->signature >> (7 * 8) & 0xff;

    printf("GPT Header:\n");
    printf(" Signature                  : %c%c%c%c%c%c%c%c\n", a, b, c, d, e, f, g, h);
    printf(" Version                    : 0x%08x\n", gpthdr->revision);
    printf(" Hdr Size                   : %d\n", gpthdr->size);
    printf(" Hdr CRC32                  : 0x%08x\n", gpthdr->crc32);
    printf(" Reserved                   : 0x%08x\n", gpthdr->reserved1);
    printf(" Hdr Start LBA              : %llu\n", gpthdr->my_lba);
    printf(" Backup Hdr Start LBA       : %llu\n", gpthdr->alternative_lba);
    printf(" Partition Start LBA        : %llu\n", gpthdr->first_usable_lba);
    printf(" Partition End LBA          : %llu\n", gpthdr->last_usable_lba);
    printf(" Partition Table Start LBA  : %llu\n", gpthdr->partition_entry_lba);
    printf(" Number of Partition Entry  : %u\n", gpthdr->npartition_entries);
    printf(" Size of Partition Entry    : %u\n", gpthdr->sizeof_partition_entry);
    printf(" Partition Table CRC32      : 0x%08x\n", gpthdr->partition_entry_array_crc32);
    printf(" GUID                       : %08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X\n",
            gpthdr->disk_guid.time_low,
            gpthdr->disk_guid.time_mid,
            gpthdr->disk_guid.time_hi_and_version,
            gpthdr->disk_guid.clock_seq_hi,
            gpthdr->disk_guid.clock_seq_low,
            gpthdr->disk_guid.node[0],
            gpthdr->disk_guid.node[1],
            gpthdr->disk_guid.node[2],
            gpthdr->disk_guid.node[3],
            gpthdr->disk_guid.node[4],
            gpthdr->disk_guid.node[5] );
    printf(" GPT Sector Size(Default)   : %d\n", gpt_sector_size);
    printf(" Reserved2 must be all 0\n");
    printf("\n");
}

static inline void debug_dump_gpt_partition_entry(const struct gpt_entry *gpt, int index)
{
    /* 打印提示表头 */
    if (index == 0) {
        printf("Partition Table:\n");
        printf("  [Num]       Partition Type GUID              Unique Partition Guid             Start(sector)    End(sector)    Size      Name\n");
    }

    printf("  [%3d] ", index);

    int gpt_sector_size = 512;
    char buf[128];

    /* Type */
    snprintf(buf, sizeof(buf), "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            gpt->type.time_low, gpt->type.time_mid, gpt->type.time_hi_and_version,
            gpt->type.clock_seq_hi, gpt->type.clock_seq_low,
            gpt->type.node[0], gpt->type.node[1], gpt->type.node[2],
            gpt->type.node[3], gpt->type.node[4], gpt->type.node[5]);
    printf("%s", buf);

    /* Partition GUID */
    snprintf(buf, sizeof(buf), "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            gpt->partition_guid.time_low, gpt->partition_guid.time_mid, gpt->partition_guid.time_hi_and_version,
            gpt->partition_guid.clock_seq_hi, gpt->partition_guid.clock_seq_low,
            gpt->partition_guid.node[0], gpt->partition_guid.node[1], gpt->partition_guid.node[2],
            gpt->partition_guid.node[3], gpt->partition_guid.node[4], gpt->partition_guid.node[5]);
    printf(" %s", buf);

    /* LBA start */
    printf("%10llu", gpt_partition_get_lab_start(gpt));

    /* LBA end */
    printf("%16llu", gpt_partition_get_lba_end(gpt));

    /* LBA size */
    printf("%13llu    ", (gpt_partition_get_lba_end(gpt) - gpt_partition_get_lab_start(gpt) + 1) * gpt_sector_size);

    /* Name */
    const uint16_t *p = gpt->name;
    for ( ; ; ) {
        const char c = (const char)*p;
        if (c == '\0')
            break;

        printf("%c", c);
        p++;
    }
    printf("\n");
}

#endif

static int fill_gpt_partition(uint8_t *mbr, struct block_device_partition *parts, int part_max_num)
{
    struct gpt_header gpthdr;
    int gpt_sector_size = 512;
    int ret;
    int i;

    int gpt_lba1_offset = 1 * gpt_sector_size;
    int part_index = 0;

    /* 从存储介质的 LBA1 偏移地址处读取保存数据 */
    ret = mmc_device_block_read(gpt_lba1_offset, sizeof(gpthdr), (void *)&gpthdr);
    if (ret < 0) {
        printf("emmc device get gpt partition(device offset) (%d) buffer content failed\n", gpt_lba1_offset);
        return -EIO;
    }

    if (gpthdr.signature != GPT_HEADER_SIGNATURE) {
        printf("GPT signature is Illegal\n");
        return -EINVAL;
    }

#ifdef DEBUG_MBR_GPT_PARTITIONS
    debug_dump_gpt_partition_header(&gpthdr);
#endif

    /* 读取 GPT entrys信息 */
    int gpt_entrys_offset = 2 * gpt_sector_size;
    int entrys_len = gpthdr.npartition_entries * gpthdr.sizeof_partition_entry;
    int mmc_blk_size = 512;

    /* emmc block size (512字节)对齐 */
    entrys_len = (entrys_len + mmc_blk_size -1) & ~(mmc_blk_size - 1);

    uint8_t *entrys_buf = malloc(entrys_len);
    if (entrys_buf == NULL) {
        printf("GPT malloc entrys buffer\n");
        return -ENOMEM;
    }

    /* 从存储介质中读取entrys 信息 */
    ret = mmc_device_block_read(gpt_entrys_offset, entrys_len, entrys_buf);
    if (ret < 0) {
        printf("emmc device get gpt entrys (device offset) (%d) buffer content failed\n", gpt_entrys_offset);
        ret = -EIO;
        goto out;
    }

    for (i = 0; i < gpthdr.npartition_entries; i++) {
        const struct gpt_entry *gpt = gpt_get_partition(entrys_buf, i);

        /* GUID 为空则无效 */
        if (memcmp(&gpt->type, &GPT_UNUSED_ENTRY_GUID, sizeof(struct gpt_guid)) == 0)
            continue;

        /* 超过fatfs支持的最大分区数,则不添加分区信息 */
        if (part_index > part_max_num)
            break;

#ifdef DEBUG_MBR_GPT_PARTITIONS
        debug_dump_gpt_partition_entry(gpt, i);
#endif

        const uint16_t *name = gpt_partition_get_name(gpt);
        uint64_t start_offset = gpt_partition_get_lab_start(gpt) * gpt_sector_size;
        uint64_t part_size = (gpt_partition_get_lba_end(gpt) - gpt_partition_get_lab_start(gpt) + 1) * gpt_sector_size;
        int sector_size = emmc_device_get_partition_sector_size(start_offset, gpt_sector_size);

        parts[part_index].name       = alloc_partition_name(part_index);
        parts[part_index].part_name  = alloc_utf16_le_to_7bit(name, 64);
        parts[part_index].offset     = start_offset;
        parts[part_index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
        parts[part_index].size       = part_size;
        parts[part_index].sector_size= sector_size;

        part_index++;
    }

out:
    if (entrys_buf)
        free(entrys_buf);

    return ret;
}


/*
 * 解析设备0地址 读取的 MBR/GPT分区表是否有效, 以及分区表信息
 */
static struct block_device_partition *emmc_device_parse_device_info_partition(void)
{
    int ret;
    uint8_t mbr[512];
    struct block_device_partition *parts = NULL;

    /* 从存储介质的0地址处读取保存数据 */
    ret = mmc_device_block_read(0, sizeof(mbr), mbr);
    if (ret < 0) {
        printf("emmc device get mbr/gpt info(device block) 0 buffer content failed\n");
        goto out;
    }

    /* 判断是否包含有效分区信息 */
    if (mbr_is_valid_magic(mbr) == 0) {
        printf("parse part: mbr magic Illegal\n");
        //ret = -EILSEQ;
        goto out;
    }

    /* 打印分区基础信息 */
    struct dos_partition *p = mbr_get_partition(mbr, 0);

    /* 注册分区表信息到dfs_elm 文件系统中 */
    int max_num_partition = EMMC_DEVICE_MAX_PARTITION_NUM;  /* fatfs 最大支持10个分区,预留2个分区给其他用途 */

    /* 多一个用作结束标志 */
    parts = malloc((max_num_partition + 1) * sizeof(struct block_device_partition));
    if (parts == NULL) {
        printf("emmc device malloc partition info failed\n");
        goto out;
    }
    memset(parts, 0x00, (max_num_partition + 1) * sizeof(struct block_device_partition));

    switch (p->sys_ind) {
    case MBR_GPT_PARTITION:
        ret = fill_gpt_partition(mbr, parts, max_num_partition);
        if (ret < 0)
            goto out;

        break;

    case MBR_FAT12_PARTITION:
    case MBR_FAT16_LESS32M_PARTITION:
    case MBR_DOS_EXTENDED_PARTITION:
    case MBR_FAT16_PARTITION:
    case MBR_W95_FAT32_PARTITION:
    case MBR_W95_FAT32_LBA_PARTITION:
    case MBR_W95_FAT16_LBA_PARTITION:
    case MBR_HIDDEN_FAT12_PARTITION:
    case MBR_HIDDEN_FAT16_L32M_PARTITION:
    case MBR_HIDDEN_FAT16_PARTITION:
    case MBR_HIDDEN_W95_FAT32_PARTITION:
    case MBR_HIDDEN_W95_FAT32LBA_PARTITION:
    case MBR_HIDDEN_W95_FAT16LBA_PARTITION:
    case MBR_LINUX_DATA_PARTITION:
    case MBR_LINUX_EXTENDED_PARTITION:
    case MBR_BSD_OS_PARTITION:
    case MBR_FREEBSD_PARTITION:
    case MBR_OPENBSD_PARTITION:
    case MBR_NETBSD_PARTITION:
    case MBR_BEOS_FS_PARTITION:
        fill_mbr_primary_partition(mbr, parts, max_num_partition);
        break;

    case MBR_WINDOWS_EXFAT_PARTITION:
        fill_mbr_primary_partition(mbr, parts, max_num_partition);
        break;

    case MBR_EMPTY_PARTITION:
    default:
        /* 无分区表,解析0偏移 格式化信息 */
        ret = try_support_fs_partition(mbr, parts, max_num_partition);
        if (ret == 0) {
            //printf("parse support partition from offset 0 OK\n");
            break;
        }

        printf("parse part: type(0x%x) %s can not handle\n", p->sys_ind, get_partition_type_string(p->sys_ind));
        ret = -EILSEQ;
        goto out;
    }

#ifdef DEBUG_PARTITION_PARAMS
    dump_partition_params(parts);
#endif

    return parts;

out:
    if (parts)
        free(parts);

    return NULL;
}

struct block_device_partition *get_emmc_device_gpt_partition_info(void)
{
    return emmc_device_parse_device_info_partition();
}


#endif
