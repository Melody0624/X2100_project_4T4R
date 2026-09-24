#ifndef __FS_RAMDISK_BLOCK_DRIVER_H__
#define __FS_RAMDISK_BLOCK_DRIVER_H__

#include <dfs_device.h>

struct ramdisk_block_driver_ops;

struct ramdisk_block_device {
    struct device parent;
    char name[64];

    uint32_t block_size;     /* The Block size in the ramdisk uint:Byte */
    uint32_t block_start;    /* The start of available address */
    uint32_t block_end;      /* The end of available address */

    /* operations interface */
    const struct ramdisk_block_driver_ops* ops;
    struct ramdisk_block_partition *device_parts;
};

struct ramdisk_block_driver_ops {
    uint32_t (*read)(struct ramdisk_block_device* device, uint32_t offset, uint8_t* data, uint32_t length);
    uint32_t (*write)(struct ramdisk_block_device* device, uint32_t offset, const uint8_t* data, uint32_t length);

    int (*erase_block)(struct ramdisk_block_device* device, uint32_t offset, uint32_t length);
};

/*
 * Partition
 */
#define PART_FLAG_RDONLY                0x0001
#define PART_FLAG_WRONLY                0x0002
#define PART_FLAG_RDWR                  0x0003

#define PART_TYPE_BLK                   0x0010
#define PART_TYPE_MTD                   0x0020


struct ramdisk_block_partition {
    union {
        struct ramdisk_block_device device;
        struct device blk;
    };

    char *name;
    uint32_t offset;            /* offset within the master RAM space */
    uint32_t size;              /* partition size */
    uint32_t sector_size;       /* sector size: unit Byte. 同一个存储介质不同分区烧录不同镜像sector size可以不相同 */
    uint32_t mask_flags;        /* master MTD flags to mask out for this partition */
    void *user_data;            /* hold parent device */
};

long ramdisk_block_register_device(const char *name, struct ramdisk_block_device *device);
long ramdisk_block_unregister_device(struct ramdisk_block_device *device);

long ramdisk_block_device_init_partition_ops(const char *device_name,struct ramdisk_block_partition *parts);
long ramdisk_block_device_deinit_partition_ops(struct ramdisk_block_device *blk_device);

static  inline uint32_t block_device_read(struct ramdisk_block_device* device, uint32_t offset, uint8_t* data, uint32_t length)
{
    return device->ops->read(device, offset, data, length);
}

static inline uint32_t block_device_write(struct ramdisk_block_device* device, uint32_t offset, const uint8_t* data, uint32_t length)
{
    return device->ops->write(device, offset, data, length);
}

static inline long block_device_erase(struct ramdisk_block_device* device, uint32_t offset, uint32_t length)
{
    return device->ops->erase_block(device, offset, length);
}

#endif /* __FS_RAMDISK_BLOCK_DRIVER_H__ */
