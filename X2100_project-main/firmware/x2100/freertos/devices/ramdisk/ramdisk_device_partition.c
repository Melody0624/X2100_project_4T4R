#include "ramdisk_partition.h"
#include "ramdisk_device.h"

#ifdef CONFIG_DFS_ELMFAT

/*
 * FAT12 范围: 64K ~ 128M
 * FAT16 范围: 2M  ~ 2G
 * FAT32 范围: 32M ~ 2T
 * exFAT 范围: 2M ～ 128PB(2^57)
 *
 * dfs_elm FAT12/FAT16支持扇区最大字节数 (8192)
 * 扇区字节 512     最小块大小 > 64K
 * 扇区字节 1024    最小块大小 > 128K
 * 扇区字节 2048    最小块大小 > 256K
 * 扇区字节 4096    最小块大小 > 512K
 * 扇区字节 8192    最小块大小 > 1M
 *
 * dfs_elm FAT32
 * 扇区字节 512     最小块大小 > 32M + 272K
 *
 * dfs_elm exFAT
 * 扇区字节 512     最小块大小 > 2M + 31.5K
 * 扇区字节 1024    最小块大小 > 4M + 63K
 * 扇区字节 2048    最小块大小 > 8M + 126K
 * 扇区字节 4096    最小块大小 > 16M +252K
 * 扇区字节 8192    最小块大小 > 32M +504K
 */
#define DFS_ELM_RAMDISK_BLOCK_SIZE          (4096)


/*
 * ramdisk device 需要实现
 */
uint32_t ramdisk_device_block_read(uint32_t from, uint32_t len, uint8_t *buf);
uint32_t ramdisk_device_block_write(uint32_t to, uint32_t len, const uint8_t *buf);
int ramdisk_device_block_erase(uint32_t addr, uint32_t len);

#ifdef DEBUG_PARTITION_PARAMS
static void dump_partition_params(struct ramdisk_block_partition *parts)
{
    struct ramdisk_block_partition *p_parts = NULL;

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


static int ramdisk_device_get_defaule_sector_size(void)
{
    return DFS_ELM_RAMDISK_BLOCK_SIZE;
}

/*
 * 检查文件系统类型是否为有效的FAT/exFAT, 并返回FAT sector size(单位:字节)
 *
 * 参数: buf 分区第一块内容, 大小:>=512
 * 返回值: -1   : 非FAT类型/未格式化
 *        other: sector size
 *
 * 实现和其他的存储介质check_fs_sector_size函数实现一样. 加ramdisk前缀方便阅读代码/跳转
 */
static inline int ramdisk_device_check_fs_sector_size(uint8_t *buf, uint32_t size)
{
#define BPB_BytsPerSec                  11 /* Sector size [byte] (WORD) */
#define BPB_SecPerClus                  13 /* Cluster size [sector] (BYTE) */
#define BPB_RsvdSecCnt                  14 /* Size of reserved area [sector] (WORD) */
#define BPB_NumFATs                     16 /* Number of FATs (BYTE) */
#define BPB_RootEntCnt                  17 /* Size of root directory area for FAT [entry] (WORD) */
#define BPB_FATSz16                     22 /* FAT size (16-bit) [sector] (WORD) */
#define BS_55AA                         510 /* Signature word (WORD) */

    uint32_t tmp;
    uint32_t value;

    /* FAT buf大小 检查 */
    if (size < 512)
        return -1;


    /* FAT 结束标志 检查 */
    value = 0xAA     << 8 | 0x55;
    tmp   = buf[BS_55AA + 1] << 8 | buf[BS_55AA];
    if (value != tmp)
        return -1;


    /* exFAT */
    if (!memcmp(buf, "\xEB\x76\x90" "EXFAT   ", 11)) {      /* It is an exFAT VBR */
        goto found_fat_type;
    }


    /* FAT Type */
    uint8_t b = buf[0];
    if (b == 0xEB || b == 0xE9 || b == 0xE8) {
        /* FAT32 */
        if (!memcmp(buf + 82, "FAT32   ", 8)) {
            goto found_fat_type;
        }


        /* FAT12/FAT16 格式判断 */
        uint16_t ssize = (buf[BPB_BytsPerSec + 1] << 8) | buf[BPB_BytsPerSec];
        uint8_t fat_num = buf[BPB_NumFATs];
        uint16_t reserved_sec_cnt = (buf[BPB_RsvdSecCnt + 1] << 8) | buf[BPB_RsvdSecCnt];
        uint16_t root_entry_cnt = (buf[BPB_RootEntCnt + 1] << 8) | buf[BPB_RootEntCnt];
        uint16_t fat_size = buf[BPB_FATSz16];
        if ( (ssize & (ssize - 1)) == 0         /* Properness of sector size (512-4096 and 2^n) */
            && reserved_sec_cnt != 0         /* Properness of reserved sectors (MNBZ) */
            && fat_num -1 <= 1               /* Properness of FATs (1 or 2) */
            && root_entry_cnt != 0           /* Properness of root dir entries (MNBZ) */
            && fat_size != 0)                /* Properness of FAT size (MNBZ) */
        {
            goto found_fat_type;
        }
    }

    return -1;


found_fat_type:
    return (buf[BPB_BytsPerSec + 1] << 8) | buf[BPB_BytsPerSec];
}



static int ramdisk_device_get_partition_sector_size(uint64_t pos, uint32_t size)
{
    int ret;
    int sector_size = ramdisk_device_get_defaule_sector_size();
    uint8_t *buffer = (uint8_t *)malloc(size);
    if (buffer == NULL) {
        printf("ramdisk device get partition malloc buffer faild. size=%d\n", size);
        goto default_sector_size;
    }

    ret = ramdisk_device_block_read(pos, size, buffer);
    if (ret < 0) {
        printf("block device get partition read error\n");
        goto default_sector_size;
    }

    int ssize = ramdisk_device_check_fs_sector_size(buffer, size);
    if (ssize > 0)
        sector_size = ssize;


default_sector_size:
    if (buffer)
        free(buffer);
    return sector_size;
}

static struct ramdisk_block_partition *get_ramdisk_device_default_partition_info(void)
{
    struct ramdisk_block_partition *parts;
    int index;
    int num_partition = 1 + 1;
    int sector_size;

    struct ramdisk_block *rdev = ramdisk_devices_info();
    if (rdev == NULL) {
        printf("ramdisk device device is invalid\n");
        return NULL;
    }

    parts = malloc(num_partition * sizeof(struct ramdisk_block_partition));
    if (parts == NULL) {
        printf("emmc device malloc default partition info failed\n");
        return NULL;
    }

    uint32_t start_offset = (uint32_t)(rdev->start_addr); /* 0MByte */
    uint32_t part0_size = rdev->size;  /* Unit: Byte */

    sector_size = ramdisk_device_get_partition_sector_size(start_offset, 512);
    index = 0;
    parts[index].name       = "ramdisk_ingenic_focre_rot0_ponit";
    parts[index].offset     = start_offset;    /* start address */
    parts[index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
    parts[index].size       = part0_size;
    parts[index].sector_size= sector_size;

    /* end of partition */
    index = 1;
    parts[index].name = NULL;

#ifdef DEBUG_PARTITION_PARAMS
    dump_partition_params(parts);
#endif

    return parts;
}

static struct ramdisk_block_partition *ramdisk_device_alloc_partition_information(void)
{
    struct ramdisk_block_partition *parttion;

    parttion = get_ramdisk_device_default_partition_info();

    return parttion;
}

static int ramdisk_device_block_partition_init(const char *device_name)
{
    struct ramdisk_block_partition *flash_parts = ramdisk_device_alloc_partition_information();

    ramdisk_block_device_init_partition_ops(device_name, flash_parts);

    return 0;
}

/*
 * 卸载分区
 */
static int ramdisk_device_free_partition_information(struct ramdisk_block_partition *parts)
{
    if (parts)
        free(parts);

    return 0;
}

static int ramdisk_device_block_partition_deinit(struct ramdisk_block_device *blk_device)
{
    ramdisk_block_device_deinit_partition_ops(blk_device);

    ramdisk_device_free_partition_information(blk_device->device_parts);

    blk_device->device_parts = NULL;

    return 0;
}

/***********************************************************************
 *
 ***********************************************************************/
/*
 * ramdisk devices operation
 */


static uint32_t ramdisk_devices_blk_read(struct ramdisk_block_device* device, uint32_t offset, uint8_t* data, uint32_t length)
{
    return ramdisk_device_block_read(offset, length, data);
}

static uint32_t ramdisk_devices_blk_write(struct ramdisk_block_device* device, uint32_t offset, const uint8_t* data, uint32_t length)
{
    return ramdisk_device_block_write(offset, length, data);
}

static int ramdisk_devices_blk_erase(struct ramdisk_block_device* device, uint32_t offset, uint32_t length)
{
    return ramdisk_device_block_erase(offset, length);
}

const static struct ramdisk_block_driver_ops ramdisk_blk_ops = {
    .read           = ramdisk_devices_blk_read,
    .write          = ramdisk_devices_blk_write,
    .erase_block    = ramdisk_devices_blk_erase,
};


int ramdisk_device_partitions_init(struct ramdisk_block *rdev)
{
    struct ramdisk_block_device *rblk_device = malloc(sizeof(struct ramdisk_block_device));
    assert(rblk_device != NULL);

    memset(rblk_device, 0x00, sizeof(struct ramdisk_block_device));
    sprintf(rblk_device->name, "%s", "ramdisk_ram0");

    rblk_device->ops         = &ramdisk_blk_ops;
    rblk_device->block_size  = ramdisk_device_get_defaule_sector_size(); /* Max:8KByte: 该值目前没用到: 读/写/擦除按照sector size对齐操作 */
    rblk_device->block_start = (uint32_t)(rdev->start_addr);        /*  该值目前没用到 */
    rblk_device->block_end   = (uint32_t)(rdev->end_addr);          /*  该值目前没用到 */

    ramdisk_block_register_device(rblk_device->name, rblk_device);

    ramdisk_device_block_partition_init(rblk_device->name);

    return 0;
}


int ramdisk_device_partitions_deinit(struct ramdisk_block *rdev)
{
    char blk_name[64];

    memset(blk_name, 0x00, sizeof(blk_name));
    sprintf(blk_name, "%s", "ramdisk_ram0");

    struct ramdisk_block_device *blk_device = (struct ramdisk_block_device *)device_find(blk_name);
    assert(blk_device != NULL);

    ramdisk_device_block_partition_deinit(blk_device);

    ramdisk_block_unregister_device(blk_device);

    free(blk_device);

    return 0;
}

#endif