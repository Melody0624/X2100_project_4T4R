#include <malloc.h>
#include "mmc_devices.h"
#include "mmc_partition.h"
#include "mmc_partition_fat.h"
#include <driver/storage_info.h>

#ifdef CONFIG_DFS_ELMFAT

/*
 * dfs_elm支持扇区最大字节数
 */
#define DFS_ELM_MAX_BLOCK_SIZE          (512)

/*
 * device 需要实现
 */
uint32_t mmc_device_block_read(uint64_t from, uint32_t len, uint8_t *buf);
uint32_t mmc_device_block_write(uint64_t to, uint32_t len, const uint8_t *buf);
int mmc_device_block_erase(uint64_t addr, uint32_t len);

struct block_device_partition *get_emmc_device_gpt_partition_info(void);

int file_system_root_path_is_valid(void);

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

static int emmc_device_get_defaule_sector_size(void)
{
    return DFS_ELM_MAX_BLOCK_SIZE;
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
static inline int emmc_device_check_fs_sector_size(uint8_t *buf, uint32_t size)
{
    /* FAT buf大小 检查 */
    if (size < 512)
        return -1;

    uint16_t sign = ld_word(buf + BS_55AA);

    /* FAT 结束标志 检查 */
    if (sign != 0xAA55)
        return -1;

    int sector_size = -1;

    /* exFAT */
    if (!memcmp(buf, "\xEB\x76\x90" "EXFAT   ", 11)) {      /* It is an exFAT VBR */
        sector_size = 1 << buf[BPB_BytsPerSecEx];
        goto found_fat_type;
    }


    /* FAT Type */
    uint8_t b = buf[0];
    if (b == 0xEB || b == 0xE9 || b == 0xE8) {
        /* FAT32 */
        if (!memcmp(buf + 82, "FAT32   ", 8)) {
            sector_size = ld_word(buf + BPB_BytsPerSec);
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
            sector_size = ld_word(buf + BPB_BytsPerSec);
            goto found_fat_type;
        }
    }

    return -1;


found_fat_type:
    return sector_size;
}

/*
 * 适配设备加载/卸载操作， partition name 和MBR/GPT方式一致使用malloc申请
 */
static inline char *alloc_partition_name(const char *name)
{
    /* 在设备卸载时释放 */
    int name_len = 64;
    char *part_name = malloc(name_len);
    assert(part_name);

    memset(part_name, 0x00, name_len);
    sprintf(part_name, "%s", name);

    return part_name;
}

int emmc_device_get_partition_sector_size(uint64_t pos, uint32_t size)
{
    int ret;
    int sector_size = emmc_device_get_defaule_sector_size();
    uint8_t *buffer = (uint8_t *)malloc(size);
    if (buffer == NULL) {
        printf("block device get partition malloc buffer faild. size=%d\n", size);
        goto default_sector_size;
    }

    ret = mmc_device_block_read(pos, size, buffer);
    if (ret < 0) {
        printf("block device get partition read error\n");
        goto default_sector_size;
    }

    int ssize = emmc_device_check_fs_sector_size(buffer, size);
    if (ssize > 0)
        sector_size = ssize;


default_sector_size:
    if (buffer)
        free(buffer);
    return sector_size;
}


static struct block_device_partition *get_emmc_device_default_partition_info(void)
{
    struct block_device_partition *parts;
    int index;
    int num_partition = 2 + 1;
    int sector_size;

    struct mmc_card *card = mmc_devices_info();
    if (card == NULL) {
        printf("emmc device device card is invalid\n");
        return NULL;
    }

    parts = malloc(num_partition * sizeof(struct block_device_partition));
    if (parts == NULL) {
        printf("emmc device malloc default partition info failed\n");
        return NULL;
    }
    memset(parts, 0x00, num_partition * sizeof(struct block_device_partition));

    /* partition-0 */
    uint64_t start_offset = 99 * 1024 * 1024; /* 99MByte */
    uint64_t part0_size = 2 * 1024 * 1024; /* 2MByte */

    sector_size = emmc_device_get_partition_sector_size(start_offset, card->card_blk_size);
    index = 0;
    parts[index].name       = alloc_partition_name("rootfs");
    parts[index].offset     = start_offset;    /* 99M */
    parts[index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
    parts[index].size       = part0_size;  /* 2M */
    parts[index].sector_size= sector_size;


    /* partition-1 */
    start_offset = start_offset + part0_size;
    uint64_t part1_size = (card->card_capacity << 10) - 1;  /* Unit: Byte */
    //part1_size = part1_size - start_offset;
    part1_size = 8 *1024 * 1024;   /* 8M */

    sector_size = emmc_device_get_partition_sector_size(start_offset, card->card_blk_size);
    index = 1;
    parts[index].name       = alloc_partition_name("data");
    parts[index].offset     = start_offset;    /* 101M */
    parts[index].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;
    parts[index].size       = part1_size;  /* 8M */
    parts[index].sector_size= sector_size;

    /* end of partition */
    index = 2;
    parts[index].name = NULL;

#ifdef DEBUG_PARTITION_PARAMS
    dump_partition_params(parts);
#endif

    return parts;
}

static struct block_device_partition *mmc_device_alloc_partition_information(int removable)
{
    struct block_device_partition *parttion = NULL;

    /* 可动态拔插的设备，读取MBR/GPT的信息，分区表无效则不挂载 */
    if (removable)
        parttion = get_emmc_device_gpt_partition_info();
    else if (parttion == NULL) {
        /* 不可拔插设备,使用默认分区表，不使用设备中存储的MBR/GPT, (方便更改分区信息) */
        parttion = get_emmc_device_default_partition_info();
    }

    return parttion;
}

static int emmc_device_gpt_partition_init(const char *device_name, int removable)
{
    struct block_device_partition *flash_parts = mmc_device_alloc_partition_information(removable);

    block_device_init_partition_ops(device_name, flash_parts);

    return 0;
}

#ifdef CONFIG_DFS_HOTPLUG
static int emmc_device_hotplug_insert_partition_event(struct block_device *blk_device)
{
    return block_device_hotplug_partition_register(blk_device);
}

static void thread_device_emmc_insert_partition_event(void *data)
{
    struct block_device *blk_device = (struct block_device *)data;

    /* 等待 根节点 是否挂载成功 */
    int ret;
    int count = 1000;
    do {
        ret = file_system_root_path_is_valid();
        msleep(1);
    } while(ret == 0 && count-- > 0);

    /* 挂载分区 */
    emmc_device_hotplug_insert_partition_event(blk_device);
}
#endif

/*
 * 卸载分区
 */
static int mmc_device_free_partition_information(struct block_device_partition *parts)
{
    if (!parts)
        return 0;

    struct block_device_partition *device_part;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        if (device_part->name)
            free(device_part->name);
        if (device_part->part_name)
            free(device_part->part_name);
    }

    free(parts);
    return 0;
}

static int emmc_device_gpt_partition_deinit(struct block_device *blk_device)
{
    block_device_deinit_partition_ops(blk_device);

    mmc_device_free_partition_information(blk_device->device_parts);

    blk_device->device_parts = NULL;

    return 0;
}

#ifdef CONFIG_DFS_HOTPLUG
static int emmc_device_hotplug_remove_partition_event(struct block_device *blk_device)
{
    return block_device_hotplug_partition_unregister(blk_device);
}
#endif

/***********************************************************************
 *
 ***********************************************************************/
/*
 * emmc devices operation
 */


static uint32_t emmc_devices_blk_read(struct block_device* device, uint64_t offset, uint8_t* data, uint32_t length)
{
    return mmc_device_block_read(offset, length, data);
}

static uint32_t emmc_devices_blk_write(struct block_device* device, uint64_t offset, const uint8_t* data, uint32_t length)
{
    return mmc_device_block_write(offset, length, data);
}

static int emmc_devices_blk_erase(struct block_device* device, uint64_t offset, uint32_t length)
{
    return mmc_device_block_erase(offset, length);
}

const static struct block_driver_ops emmc_blk_ops = {
    .read           = emmc_devices_blk_read,
    .write          = emmc_devices_blk_write,
    .erase_block    = emmc_devices_blk_erase,
};

int get_mmc_partition_information_by_name(char *name, uint64_t *offset, uint64_t *size)
{
    int i = 0;
    struct block_device_partition *partition = get_emmc_device_gpt_partition_info();

    for(i = 0; i < 8; i++) {
        if (partition[i].name == NULL)
            break;
        if (!strcmp(partition[i].part_name, name)) {
                memcpy(offset, &partition[i].offset, sizeof(uint64_t));
                memcpy(size, &partition[i].size, sizeof(uint64_t));
                return 0;
        }
    }
    return -1;
}

struct storage_info mmc_device_info;
const struct storage_info *mmc_device_storage_info(void)
{
    struct mmc_card *card = mmc_devices_info();
    if (card == NULL) {
        printf("emmc device device card is invalid\n");
        return NULL;
    }

    struct mmc_csd *csd = &(card->csd);

    mmc_device_info.chipsize = card->card_capacity * 1024;
    mmc_device_info.pagesize = csd->rd_blk_len * 4;
    mmc_device_info.erasesize = csd->wr_blk_len * 256;

    return &mmc_device_info;
}

int emmc_device_partition_init(struct mmc_card *card)
{
    struct block_device *blk_device = malloc(sizeof(struct block_device));
    assert(blk_device != NULL);

    int removable = card->host->capacity & MMC_CAP_NONREMOVABLE ? 0 : 1;

    memset(blk_device, 0x00, sizeof(struct block_device));
    sprintf(blk_device->name, "%s%d", "block_mmc", card->host->index);

    /* eMMC设备写操作无需发送擦除命令, 操作的块大小可以大于card操作大小 */
    blk_device->ops         = &emmc_blk_ops;
    blk_device->block_size  = emmc_device_get_defaule_sector_size(); /* 该值目前没用到: 读/写/擦除按照sector size对齐操作 */
    blk_device->block_start = 0;    /* 该值目前没用到 */
    blk_device->block_end   = (card->card_capacity / blk_device->block_size) << 10; /* 该值目前没用到 */

    block_devices_register_device(blk_device->name, blk_device);

    emmc_device_gpt_partition_init(blk_device->name, removable);

#ifdef CONFIG_DFS_HOTPLUG
    /* 可动态拔插的设备，自动挂载设备/分区 */
    if (removable)
        thread_create("emmc partition", 2048, thread_device_emmc_insert_partition_event, blk_device);
#endif

    return 0;
}


int emmc_device_partition_deinit(struct mmc_card *card)
{
    char blk_name[64];

    memset(blk_name, 0x00, sizeof(blk_name));
    sprintf(blk_name, "%s%d", "block_mmc", card->host->index);

    struct block_device *blk_device = (struct block_device *)device_find(blk_name);
    assert(blk_device != NULL);

#ifdef CONFIG_DFS_HOTPLUG
    int removable = card->host->capacity & MMC_CAP_NONREMOVABLE ? 0 : 1;

    /* 可动态拔插的设备，自动挂载设备/分区 */
    if (removable)
        emmc_device_hotplug_remove_partition_event(blk_device);
#endif

    emmc_device_gpt_partition_deinit(blk_device);

    block_devices_unregister_device(blk_device);

    free(blk_device);

    return 0;
}

#endif
