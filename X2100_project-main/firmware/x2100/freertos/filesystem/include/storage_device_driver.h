#ifndef __FS_DISK_STORAGE_DRIVER_H__
#define __FS_DISK_STORAGE_DRIVER_H__

/* U-Disk(Host) block driver */

#include <dfs_device.h>

struct storage_driver_ops;

struct storage_device {
    struct device parent;
    char name[32];

    uint32_t block_size;        /* The Block size in the flash */
    uint64_t block_start;       /* The start of available block (Unit: Block size) */
    uint64_t block_end;         /* The end of available block  (Unit: Block size) */

    /* operations interface */
    const struct storage_driver_ops* ops;
    struct storage_device_partition *device_parts;

    void *dev;      /* U-Disk 底层驱动句柄 */
    int id;         /* U-Disk id编号 */
};

struct storage_driver_ops {
    uint64_t (*read)(struct storage_device* device, uint64_t offset, uint8_t* data, uint64_t length);
    uint64_t (*write)(struct storage_device* device, uint64_t offset, const uint8_t* data, uint64_t length);

    int (*erase_block)(struct storage_device* device, uint64_t offset, uint64_t length);
};

/*
 * Partition
 */
#define PART_FLAG_RDONLY                0x0001
#define PART_FLAG_WRONLY                0x0002
#define PART_FLAG_RDWR                  0x0003

#define PART_TYPE_BLK                   0x0010
#define PART_TYPE_MTD                   0x0020


struct storage_device_partition {
    union {
        struct storage_device device;
        struct device blk;
    };

    char *name;
    uint64_t offset;            /* offset within the master MTD space: unit Byte. */
    uint64_t size;              /* partition size: unit Byte. */
    uint32_t sector_size;       /* sector size: unit Byte. 同一个存储介质不同分区烧录不同镜像sector size可以不相同 */
    uint32_t mask_flags;        /* master MTD flags to mask out for this partition */
    void *user_data;            /* hold parent device */
};

long storage_devices_register_device(const char *name, struct storage_device *device);
long storage_devices_unregister_device(struct storage_device *device);

long storage_device_init_partition_ops(struct storage_device *blk_device, struct storage_device_partition *parts);
long storage_device_deinit_partition_ops(struct storage_device *blk_device);

long storage_device_hotplug_partition_register(struct storage_device *blk_device);
long storage_device_hotplug_partition_unregister(struct storage_device *blk_device);

static  inline uint64_t storage_device_read(struct storage_device* device, uint64_t offset, uint8_t* data, uint64_t length)
{
    return device->ops->read(device, offset, data, length);
}

static inline uint64_t storage_device_write(struct storage_device* device, uint64_t offset, const uint8_t* data, uint64_t length)
{
    return device->ops->write(device, offset, data, length);
}

static inline int storage_device_erase(struct storage_device* device, uint64_t offset, uint64_t length)
{
    return device->ops->erase_block(device, offset, length);
}

#endif /* __FS_DISK_STORAGE_DRIVER_H__ */
