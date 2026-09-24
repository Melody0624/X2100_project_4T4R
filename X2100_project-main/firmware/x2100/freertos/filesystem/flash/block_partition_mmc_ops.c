#include <dfs_device.h>
#include <block_device_driver.h>
#include "../filesystem_hotplug.h"

//#define BLOCK_DEBUG

#ifdef BLOCK_DEBUG
#define BLOCK_DBG(...)      printf("[BLOCK]"), printf(__VA_ARGS__)
#else
#define BLOCK_DBG(...)
#endif

#define PER_RW_MAX_SECTORS 64

/* Device interface */
static int block_device_blk_init(device_t dev)
{
    return 0;
}

static int block_device_blk_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int block_device_blk_close(device_t dev)
{
    return 0;
}

static int block_device_blk_control(device_t dev, uint8_t cmd, void *args)
{
    struct block_device_partition *device_part;

    assert(dev != NULL);

    device_part = (struct block_device_partition *)dev;

    switch (cmd) {
    case DEVICE_CTRL_BLK_GETGEOME:
    {
        struct device_blk_geometry *geometry;

        geometry = (struct device_blk_geometry *)args;
        if (geometry == NULL)
            return EINVAL;

        geometry->bytes_per_sector  = device_part->sector_size;
        geometry->sector_count      = device_part->size / device_part->sector_size;
        geometry->block_size        = device_part->sector_size;
        BLOCK_DBG("geometry->sector_count=%d  block_size=%d  device->size=%lld\n",
                geometry->sector_count, geometry->block_size, geometry->sector_count * geometry->block_size);
        break;
    }

    default:
        break;
    }

    return 0;
}

static uint32_t block_device_blk_read(device_t dev, uint64_t pos, void *buffer, uint32_t size)
{
    struct block_device_partition *device_part;
    struct block_device *blk_device;
    uint32_t read_sectors = 0;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;
    int ret = 0;
    uint32_t current_read_sectors = 0;
    uint32_t current_read_size = 0;

    assert(dev != NULL);
    assert(size != 0);

    device_part = (struct block_device_partition *)dev;
    blk_device = (struct block_device *)device_part->user_data;
    block_size = device_part->sector_size;

    BLOCK_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, device_part->name, pos, size);

    if (!(device_part->mask_flags & PART_FLAG_RDONLY)) {
        BLOCK_DBG("ERROR: this device is unreadable,mask_flags = %04x\n", device_part->mask_flags);
        return 0;
    }

    while (read_sectors < size) {
        current_read_sectors = size - read_sectors;

        if (current_read_sectors > blk_device->per_rw_max_sectors) {
            current_read_sectors = blk_device->per_rw_max_sectors;
        }

        /* It'a BLOCK device */
        if ((pos + read_sectors + current_read_sectors) * block_size > device_part->offset + device_part->size) {
            BLOCK_DBG("ERROR: read overrun!\n");
            break;
        }

        current_read_size = current_read_sectors * block_size;

        ret = block_device_read(blk_device, (pos + read_sectors) * block_size + device_part->offset, ptr + read_sectors * block_size, current_read_size);
        if (!ret) {
            read_sectors += current_read_sectors;
        } else {
            if (current_read_sectors > 1) {
                current_read_sectors /= 2;
                blk_device->per_rw_max_sectors = current_read_sectors;
                continue;
            } else {
                BLOCK_DBG("Read failure with minimum unit 512 Byte\n");
                break;
            }
        }
    }

    return read_sectors;
}

static uint32_t block_device_blk_write(device_t dev, uint64_t pos, const void *buffer, uint32_t size)
{
    struct block_device_partition *device_part;
    struct block_device *blk_device;
    uint32_t write_sectors = 0;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;
    int ret = 0;
    uint32_t current_write_sectors = 0;
    uint32_t current_write_size = 0;

    assert(dev != NULL);
    assert(size != 0);

    device_part = (struct block_device_partition *)dev;
    blk_device = (struct block_device *)device_part->user_data;
    block_size = device_part->sector_size;

    BLOCK_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, device_part->name, pos, size);
    if (!(device_part->mask_flags & PART_FLAG_WRONLY)) {
        BLOCK_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", device_part->mask_flags);
        /* read only partition, ignore this data */
        return size;
    }

    while (write_sectors < size) {
        current_write_sectors = size - write_sectors;

        if (current_write_sectors > blk_device->per_rw_max_sectors) {
            current_write_sectors = blk_device->per_rw_max_sectors;
        }

        /* It'a BLOCK device */
        if ((pos + write_sectors + current_write_sectors) * block_size > device_part->offset + device_part->size) {
            printf("ERROR: write overrun!\n");
            break;
        }

        current_write_size = current_write_sectors * block_size;

        ret = block_device_write(blk_device, (pos + write_sectors) * block_size + device_part->offset, ptr + write_sectors * block_size, current_write_size);

        if (!ret) {
            write_sectors += current_write_sectors;
        } else {
            if (current_write_sectors > 1) {
                current_write_sectors /= 2;
                blk_device->per_rw_max_sectors = current_write_sectors;
                continue;
            } else {
                BLOCK_DBG("Write failure with minimum unit 512 Byte\n");
                break;
            }
        }
    }

    return write_sectors;
}


/******************************************************************************
 *
 * block_driver_ops
 *
 ******************************************************************************/

static uint32_t block_partition_blk_read(struct block_device *dev, uint64_t offset, uint8_t *buffer, uint32_t length)
{
    struct block_device_partition *device_part;
    struct block_device *blk_device;

    assert(dev != NULL);

    device_part = (struct block_device_partition *)dev;
    blk_device = (struct block_device *)device_part->user_data;

    BLOCK_DBG("%s offset = %08llx,size = %08x\n",__func__, offset, length);
    if (!(device_part->mask_flags & PART_FLAG_RDONLY)) {
        BLOCK_DBG("ERROR: this device is unreadable,mask_flags = %04x\n", device_part->mask_flags);
        return 0;
    }

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if((offset + length) > device_part->size) {
            BLOCK_DBG("ERROR: read size > partition size, pos=%lld, size=%d, partition_size=%lld\n", offset, length, device_part->size);
            return 0;
        }

        block_device_read(blk_device, (device_part->offset + offset), buffer, length);

        return length;
    }

    BLOCK_DBG("ERROR: unknown device type..\n");
    return 0;
}

static uint32_t block_partition_blk_write(struct block_device *dev, uint64_t offset, const uint8_t *buffer, uint32_t length)
{
    struct block_device_partition *device_part;
    struct block_device *blk_device;

    assert(dev != NULL);

    device_part = (struct block_device_partition *)dev;
    blk_device = (struct block_device *)device_part->user_data;

    BLOCK_DBG("%s offset = %08llx,size = %08x\n",__func__, offset, length);

    if (!(device_part->mask_flags & PART_FLAG_WRONLY)) {
        BLOCK_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", device_part->mask_flags);
        /* read only partition, ignore this data */
        return length;
    }

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > device_part->size) {
            BLOCK_DBG("ERROR: write size > partition size, pos=%lld, size=%d, partition_size=%lld\n", offset, length, device_part->size);
            return 0;
        }

        /* MTD device skip erase,user do it by himself */
        block_device_write(blk_device, (device_part->offset + offset), buffer, length);
        return length;
    }

    BLOCK_DBG("ERROR: unknown device type..\n");
    return 0;
}

static int block_partition_blk_erase(struct block_device* dev, uint64_t offset, uint32_t length)
{
    struct block_device_partition *device_part;
    struct block_device *blk_device;
    int block_size;

    assert(dev != NULL);

    device_part = (struct block_device_partition *)dev;
    blk_device = (struct block_device *)device_part->user_data;
    block_size = device_part->sector_size;

    BLOCK_DBG("%s offset = %08llx,size = %08x\n",__func__, offset, length);

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > device_part->size) {
            BLOCK_DBG("ERROR: erase size > partition size, pos=%lld, size=%d, partition_size=%lld\n", offset, length, device_part->size);
            return 0;
        }

        if (length % block_size != 0) {
            BLOCK_DBG("ERROR: erase size must align to BLOCK SIZE\n");
            return 0;
        }

        block_device_erase(blk_device, (device_part->offset + offset), length);

        return length;
    }

    BLOCK_DBG("ERROR: unknown device type..\n");
    return 0;
}


const static struct block_driver_ops block_part_blk_ops =
{
    .read                   = block_partition_blk_read,
    .write                  = block_partition_blk_write,
    .erase_block            = block_partition_blk_erase,
};


long block_device_init_partition_ops(const char *device_name,struct block_device_partition *parts)
{
    struct block_device_partition *device_part;
    struct block_device *blk_device;

    blk_device = (struct block_device *)device_find(device_name);
    if (blk_device == NULL)
        return -EIO;

    if (parts == NULL)
        return -EIO;

    blk_device->device_parts = parts;

    blk_device->per_rw_max_sectors = PER_RW_MAX_SECTORS;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        BLOCK_DBG("part name: %s\n",device_part->name);
        /* get partition type */
        if (device_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* set device interface */
            device_part->blk.type       = Device_Class_Block;
            device_part->blk.name       = device_part->name;
            device_part->blk.init       = block_device_blk_init;
            device_part->blk.open       = block_device_blk_open;
            device_part->blk.read       = block_device_blk_read;
            device_part->blk.write      = block_device_blk_write;
            device_part->blk.close      = block_device_blk_close;
            device_part->blk.control    = block_device_blk_control;
            device_part->blk.flag       = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;

            device_part->user_data     = blk_device;

            /* register device */
            device_register(&device_part->blk);
        } else if (device_part->mask_flags & PART_TYPE_MTD) { /* It's a MTD device */
            BLOCK_DBG("part name: %s\n",device_part->name);

            device_part->user_data = blk_device;

            /* Init MTD NOR device interface ... */
            device_part->device.block_size         = device_part->sector_size;
            device_part->device.block_start        = 0;
            device_part->device.block_end          = device_part->size / device_part->sector_size;
            device_part->device.ops                = &block_part_blk_ops;

            block_devices_register_device(device_part->name, &device_part->device);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", device_part->name);
        }
    }

    return 0;
}


long block_device_deinit_partition_ops(struct block_device *blk_device)
{

    if (blk_device == NULL)
        return -EIO;

    struct block_device_partition *parts = blk_device->device_parts;
    struct block_device_partition *device_part;

    if (parts == NULL)
        return -EIO;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        BLOCK_DBG("part name: %s\n",device_part->name);
        /* get partition type */
        if (device_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* unregister device */
            device_unregister(&device_part->blk);

        } else if (device_part->mask_flags & PART_TYPE_MTD) {
            /* It's a MTD device */
            BLOCK_DBG("part name: %s\n",device_part->name);

            block_devices_unregister_device(blk_device);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", device_part->name);
        }
    }

    return 0;
}

#ifdef CONFIG_DFS_HOTPLUG
long block_device_hotplug_partition_register(struct block_device *blk_device)
{
    if (blk_device == NULL)
        return -EIO;

    struct block_device_partition *parts = blk_device->device_parts;
    struct block_device_partition *device_part;

    if (parts == NULL)
        return -EIO;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        BLOCK_DBG("hotplug register part name: %s\n",device_part->name);

        if (device_part->mask_flags & PART_TYPE_BLK) {
            file_system_insert_partition(device_part->name);
        }

    }

    return 0;
}


long block_device_hotplug_partition_unregister(struct block_device *blk_device)
{
    if (blk_device == NULL)
        return -EIO;

    struct block_device_partition *parts = blk_device->device_parts;
    struct block_device_partition *device_part;

    if (parts == NULL)
        return -EIO;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        BLOCK_DBG("hotplug unregister part name: %s\n",device_part->name);

        if (device_part->mask_flags & PART_TYPE_BLK) {
            file_system_remove_partition(device_part->name);
        }

    }

    return 0;
}
#endif