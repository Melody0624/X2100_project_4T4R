#include <dfs_device.h>
#include <storage_device_driver.h>
#include "../filesystem_hotplug.h"

//#define STORAGE_DEBUG

#ifdef STORAGE_DEBUG
#define STORAGE_DBG(...)                printf("[STORAGE]"), printf(__VA_ARGS__)
#else
#define STORAGE_DBG(...)
#endif

/* Device interface */
static int storage_device_blk_init(device_t dev)
{
    return 0;
}

static int storage_device_blk_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int storage_device_blk_close(device_t dev)
{
    return 0;
}

static int storage_device_blk_control(device_t dev, uint8_t cmd, void *args)
{
    struct storage_device_partition *device_part;

    assert(dev != NULL);

    device_part = (struct storage_device_partition *)dev;

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
        STORAGE_DBG("geometry->sector_count=%d  block_size=%d  device->size=%lld\n",
                geometry->sector_count, geometry->block_size, geometry->sector_count * geometry->block_size);
        break;
    }

    default:
        break;
    }

    return 0;
}

static uint32_t storage_device_blk_read(device_t dev, uint64_t pos, void *buffer, uint32_t size)
{
    int ret;
    struct storage_device_partition *device_part;
    struct storage_device *storage_device;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;

    assert(dev != NULL);
    assert(size != 0);

    device_part = (struct storage_device_partition *)dev;
    storage_device = (struct storage_device *)device_part->user_data;
    block_size = device_part->sector_size;

    STORAGE_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, device_part->name, pos, size);

    if (!(device_part->mask_flags & PART_FLAG_RDONLY)) {
        printf("ERROR: this device is unreadable,mask_flags = %04x\n", device_part->mask_flags);
        return 0;
    }

    if (((pos + size) * block_size) > (device_part->offset + device_part->size)) {
        printf("ERROR: read overrun!\n");
        return 0;
    }

    ret = storage_device_read(storage_device, pos * block_size + device_part->offset, ptr, size * block_size);
    if (ret) {
        printf("ERROR: read fail ret = %d!\n", ret);
        return 0;
    }

    return size;
}

static uint32_t storage_device_blk_write(device_t dev, uint64_t pos, const void *buffer, uint32_t size)
{
    int ret;
    struct storage_device_partition *device_part;
    struct storage_device *storage_device;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;

    assert(dev != NULL);
    assert(size != 0);

    device_part = (struct storage_device_partition *)dev;
    storage_device = (struct storage_device *)device_part->user_data;
    block_size = device_part->sector_size;

    STORAGE_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, device_part->name, pos, size);
    if (!(device_part->mask_flags & PART_FLAG_WRONLY)) {
        printf("ERROR: this device is unwritable,mask_flags = %04x\n", device_part->mask_flags);
        /* read only partition, ignore this data */
        return size;
    }

    /* It'a BLOCK device */
    if ((pos + size) * block_size > (device_part->offset + device_part->size)) {
        printf("ERROR: write overrun!\n");
        return 0;
    }

    ret = storage_device_write(storage_device, (pos * block_size + device_part->offset), ptr, size * block_size);
    if (ret) {
        printf("ERROR: write fail!\n");
        return 0;
    }

    return size;
}


/******************************************************************************
 *
 * storage_driver_ops
 *
 ******************************************************************************/

static uint64_t storage_partition_blk_read(struct storage_device *dev, uint64_t offset, uint8_t *buffer, uint64_t length)
{
    struct storage_device_partition *device_part;
    struct storage_device *storage_device;

    assert(dev != NULL);

    device_part = (struct storage_device_partition *)dev;
    storage_device = (struct storage_device *)device_part->user_data;

    STORAGE_DBG("%s offset = %08llx,size = %08llx\n",__func__, offset, length);
    if (!(device_part->mask_flags & PART_FLAG_RDONLY)) {
        printf("ERROR: this device is unreadable,mask_flags = %04x\n", device_part->mask_flags);
        return 0;
    }

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > device_part->size) {
            printf("ERROR: read size > partition size, pos=%lld, size=%lld, partition_size=%lld\n", offset, length, device_part->size);
            return 0;
        }

        storage_device_read(storage_device, (device_part->offset + offset), buffer, length);

        return length;
    }

    printf("ERROR: unknown device type..\n");
    return 0;
}

static uint64_t storage_partition_blk_write(struct storage_device *dev, uint64_t offset, const uint8_t *buffer, uint64_t length)
{
    struct storage_device_partition *device_part;
    struct storage_device *storage_device;

    assert(dev != NULL);

    device_part = (struct storage_device_partition *)dev;
    storage_device = (struct storage_device *)device_part->user_data;

    STORAGE_DBG("%s offset = %08llx,size = %08llx\n", __func__, offset, length);

    if (!(device_part->mask_flags & PART_FLAG_WRONLY)) {
        printf("ERROR: this device is unwritable,mask_flags = %04x\n", device_part->mask_flags);
        /* read only partition, ignore this data */
        return length;
    }

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > device_part->size) {
            printf("ERROR: write size > partition size, pos=%lld, size=%lld, partition_size=%lld\n", offset, length, device_part->size);
            return 0;
        }

        /* MTD device skip erase,user do it by himself */
        storage_device_write(storage_device, (device_part->offset + offset), buffer, length);
        return length;
    }

    printf("ERROR: unknown device type..\n");
    return 0;
}

static int storage_partition_blk_erase(struct storage_device* dev, uint64_t offset, uint64_t length)
{
    struct storage_device_partition *device_part;
    struct storage_device *storage_device;
    int block_size;

    assert(dev != NULL);

    device_part = (struct storage_device_partition *)dev;
    storage_device = (struct storage_device *)device_part->user_data;
    block_size = device_part->sector_size;

    STORAGE_DBG("%s offset = %08llx,size = %08llx\n",__func__, offset, length);

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > device_part->size) {
            printf("ERROR: erase size > partition size, pos=%lld, size=%lld, partition_size=%lld\n", offset, length, device_part->size);
            return 0;
        }

        if (length % block_size != 0) {
            printf("ERROR: erase size must align to BLOCK SIZE\n");
            return 0;
        }

        storage_device_erase(storage_device, (device_part->offset + offset), length);

        return length;
    }

    printf("ERROR: unknown device type..\n");
    return 0;
}


const static struct storage_driver_ops storage_part_blk_ops =
{
    .read                   = storage_partition_blk_read,
    .write                  = storage_partition_blk_write,
    .erase_block            = storage_partition_blk_erase,
};


long storage_device_init_partition_ops(struct storage_device *storage_device, struct storage_device_partition *parts)
{
    struct storage_device_partition *device_part;

    assert(storage_device);
    assert(parts);

    storage_device->device_parts = parts;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        STORAGE_DBG("part name: %s\n",device_part->name);
        /* get partition type */
        if (device_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* set device interface */
            device_part->blk.type       = Device_Class_Block;
            device_part->blk.name       = device_part->name;
            device_part->blk.init       = storage_device_blk_init;
            device_part->blk.open       = storage_device_blk_open;
            device_part->blk.read       = storage_device_blk_read;
            device_part->blk.write      = storage_device_blk_write;
            device_part->blk.close      = storage_device_blk_close;
            device_part->blk.control    = storage_device_blk_control;
            device_part->blk.flag       = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;

            device_part->user_data     = storage_device;

            /* register device */
            device_register(&device_part->blk);
        } else if (device_part->mask_flags & PART_TYPE_MTD) { /* It's a MTD device */
            STORAGE_DBG("part name: %s\n",device_part->name);

            device_part->user_data = storage_device;

            /* Init MTD NOR device interface ... */
            device_part->device.block_size         = device_part->sector_size;
            device_part->device.block_start        = 0;
            device_part->device.block_end          = device_part->size / device_part->sector_size;
            device_part->device.ops                = &storage_part_blk_ops;

            storage_devices_register_device(device_part->name, &device_part->device);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", device_part->name);
        }
    }

    return 0;
}


long storage_device_deinit_partition_ops(struct storage_device *storage_device)
{
    if (storage_device == NULL)
        return -EIO;

    struct storage_device_partition *parts = storage_device->device_parts;
    struct storage_device_partition *device_part;

    if (parts == NULL)
        return -EIO;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        STORAGE_DBG("part name: %s\n",device_part->name);
        /* get partition type */
        if (device_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* unregister device */
            device_unregister(&device_part->blk);

        } else if (device_part->mask_flags & PART_TYPE_MTD) {
            /* It's a MTD device */
            STORAGE_DBG("part name: %s\n",device_part->name);

            storage_devices_unregister_device(storage_device);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", device_part->name);
        }
    }

    return 0;
}

#ifdef CONFIG_DFS_HOTPLUG
long storage_device_hotplug_partition_register(struct storage_device *storage_device)
{
    if (storage_device == NULL)
        return -EIO;

    struct storage_device_partition *parts = storage_device->device_parts;
    if (parts == NULL)
        return -EIO;

    struct storage_device_partition *device_part;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        STORAGE_DBG("hotplug register part name: %s\n",device_part->name);

        if (device_part->mask_flags & PART_TYPE_BLK) {
            file_system_insert_partition(device_part->name);
        }

    }

    return 0;
}


long storage_device_hotplug_partition_unregister(struct storage_device *storage_device)
{
    if (storage_device == NULL)
        return -EIO;

    struct storage_device_partition *parts = storage_device->device_parts;
    struct storage_device_partition *device_part;

    if (parts == NULL)
        return -EIO;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        STORAGE_DBG("hotplug unregister part name: %s\n",device_part->name);

        if (device_part->mask_flags & PART_TYPE_BLK) {
            file_system_remove_partition(device_part->name);
        }

    }

    return 0;
}
#endif