#ifndef __MTD_NOR_H__
#define __MTD_NOR_H__

#include <dfs_device.h>

struct mtd_nor_driver_ops;

struct mtd_nor_device {
    struct device parent;
    char name[64];

    uint32_t block_size;        /* The Block size in the flash */
    uint32_t block_start;       /* The start of available block*/
    uint32_t block_end;         /* The end of available block */

    /* operations interface */
    const struct mtd_nor_driver_ops* ops;
};

struct mtd_nor_driver_ops {
    uint32_t (*read)(struct mtd_nor_device* device, uint64_t offset, uint8_t* data, uint32_t length);
    uint32_t (*write)(struct mtd_nor_device* device, uint64_t offset, const uint8_t* data, uint32_t length);

    int (*erase_block)(struct mtd_nor_device* device, uint64_t offset, uint32_t length);
};

/*
 * Partition
 */
#define PART_FLAG_RDONLY                0x0001
#define PART_FLAG_WRONLY                0x0002
#define PART_FLAG_RDWR                  0x0003

#define PART_TYPE_BLK                   0x0010
#define PART_TYPE_MTD                   0x0020


struct mtd_nor_partition {
    union {
        struct mtd_nor_device mtd;
        struct device blk;
    };

    const char *name;
    uint32_t offset;            /* offset within the master MTD space */
    uint32_t size;              /* partition size */
    uint32_t sector_size;       /* sector size: unit Byte. 同一个存储介质不同分区烧录不同镜像sector size可以不相同 */
    uint32_t mask_flags;        /* master MTD flags to mask out for this partition */
    void *user_data;            /* hold parent device */
};

long mtd_devices_nor_register_device(const char *name, struct mtd_nor_device *device);

long mtd_nor_init_partition(const char *mtd_name,struct mtd_nor_partition *parts);

static  inline uint32_t mtd_nor_read(struct mtd_nor_device* device, uint32_t offset, uint8_t* data, uint32_t length)
{
    return device->ops->read(device, offset, data, length);
}

static inline uint32_t mtd_nor_write(struct mtd_nor_device* device, uint32_t offset, const uint8_t* data, uint32_t length)
{
    return device->ops->write(device, offset, data, length);
}

static inline long mtd_nor_erase_block(struct mtd_nor_device* device, uint32_t offset, size_t length)
{
    return device->ops->erase_block(device, offset, length);
}

#endif /* __MTD_NOR_H__ */
