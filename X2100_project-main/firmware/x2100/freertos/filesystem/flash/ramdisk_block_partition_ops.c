#include <dfs_device.h>
#include <ramdisk_device_driver.h>

//#define RAMDISK_DEBUG

#ifdef RAMDISK_DEBUG
#define RAMDISK_DBG(...)      printf("[RAMDISK]"), printf(__VA_ARGS__)
#else
#define RAMDISK_DBG(...)
#endif

/* Device interface */
static int ramdisk_block_device_blk_init(device_t dev)
{
    return 0;
}

static int ramdisk_block_device_blk_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int ramdisk_block_device_blk_close(device_t dev)
{
    return 0;
}

static int ramdisk_block_device_blk_control(device_t dev, uint8_t cmd, void *args)
{
    struct ramdisk_block_partition *device_part;

    assert(dev != NULL);

    device_part = (struct ramdisk_block_partition *)dev;

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
        RAMDISK_DBG("geometry->sector_count=%d  block_size=%d  device->size=%d\n",
                geometry->sector_count, geometry->block_size, geometry->sector_count * geometry->block_size);
        break;
    }

    default:
        break;
    }

    return 0;
}

static uint32_t ramdisk_block_device_blk_read(device_t dev, uint64_t pos, void *buffer, uint32_t size)
{
    struct ramdisk_block_partition *device_part;
    struct ramdisk_block_device *blk_device;
    uint32_t read_count = 0;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;

    assert(dev != NULL);
    assert(size != 0);

    device_part = (struct ramdisk_block_partition *)dev;
    blk_device = (struct ramdisk_block_device *)device_part->user_data;
    block_size = device_part->sector_size;

    RAMDISK_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, device_part->name, pos, size);

    if (!(device_part->mask_flags & PART_FLAG_RDONLY)) {
        RAMDISK_DBG("ERROR: this device is unreadable,mask_flags = %04x\n", device_part->mask_flags);
        return 0;
    }

    while (read_count < size) {
        /* It'a BLOCK device */
        if (((pos + 1) * block_size) > (device_part->offset + device_part->size)) {
            RAMDISK_DBG("ERROR: read overrun!\n");
            break;
        }
        block_device_read(blk_device, pos * block_size + device_part->offset, ptr, block_size);

        pos++;
        ptr += block_size;
        read_count++;
    }

    return read_count;
}

static uint32_t ramdisk_block_device_blk_write(device_t dev, uint64_t pos, const void *buffer, uint32_t size)
{
    struct ramdisk_block_partition *device_part;
    struct ramdisk_block_device *blk_device;
    uint32_t write_count = 0;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;

    assert(dev != NULL);
    assert(size != 0);

    device_part = (struct ramdisk_block_partition *)dev;
    blk_device = (struct ramdisk_block_device *)device_part->user_data;
    block_size = device_part->sector_size;

    RAMDISK_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, device_part->name, pos, size);
    if (!(device_part->mask_flags & PART_FLAG_WRONLY)) {
        RAMDISK_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", device_part->mask_flags);
        /* read only partition, ignore this data */
        return size;
    }

    while (write_count < size) {
        /* It'a BLOCK device */
        if ((pos + 1) * block_size > (device_part->offset + device_part->size)) {
            RAMDISK_DBG("ERROR: write overrun!\n");
            break;
        }

        //block_device_erase(blk_device, (pos * block_size + device_part->offset), block_size);
        block_device_write(blk_device, (pos * block_size + device_part->offset), ptr, block_size);

        pos++;
        ptr += block_size;
        write_count++;
    }

    return write_count;
}


/******************************************************************************
 *
 * ramdisk_block_driver_ops
 *
 ******************************************************************************/

static uint32_t ramdisk_block_partition_blk_read(struct ramdisk_block_device *dev, uint32_t offset, uint8_t *buffer, uint32_t length)
{
    struct ramdisk_block_partition *device_part;
    struct ramdisk_block_device *blk_device;

    assert(dev != NULL);

    device_part = (struct ramdisk_block_partition *)dev;
    blk_device = (struct ramdisk_block_device *)device_part->user_data;

    RAMDISK_DBG("%s offset = %08x,size = %08x\n",__func__, offset, length);
    if (!(device_part->mask_flags & PART_FLAG_RDONLY)) {
        RAMDISK_DBG("ERROR: this device is unreadable,mask_flags = %04x\n", device_part->mask_flags);
        return 0;
    }

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if((offset + length) > device_part->size) {
            RAMDISK_DBG("ERROR: read size > partition size, pos=%d, size=%d, partition_size=%d\n", offset, length, device_part->size);
            return 0;
        }

        block_device_read(blk_device, (device_part->offset + offset), buffer, length);

        return length;
    }

    RAMDISK_DBG("ERROR: unknown device type..\n");
    return 0;
}

static uint32_t ramdisk_block_partition_blk_write(struct ramdisk_block_device *dev, uint32_t offset, const uint8_t *buffer, uint32_t length)
{
    struct ramdisk_block_partition *device_part;
    struct ramdisk_block_device *blk_device;

    assert(dev != NULL);

    device_part = (struct ramdisk_block_partition *)dev;
    blk_device = (struct ramdisk_block_device *)device_part->user_data;

    RAMDISK_DBG("%s offset = %08x,size = %08x\n",__func__, offset, length);

    if (!(device_part->mask_flags & PART_FLAG_WRONLY)) {
        RAMDISK_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", device_part->mask_flags);
        /* read only partition, ignore this data */
        return length;
    }

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if((offset + length) > device_part->size) {
            RAMDISK_DBG("ERROR: write size > partition size, pos=%d, size=%d, partition_size=%d\n", offset, length, device_part->size);
            return 0;
        }

        /* MTD device skip erase,user do it by himself */
        block_device_write(blk_device, (device_part->offset + offset), buffer, length);
        return length;
    }

    RAMDISK_DBG("ERROR: unknown device type..\n");
    return 0;
}

static int ramdisk_block_partition_blk_erase(struct ramdisk_block_device* dev, uint32_t offset, uint32_t length)
{
    struct ramdisk_block_partition *device_part;
    struct ramdisk_block_device *blk_device;
    int block_size;

    assert(dev != NULL);

    device_part = (struct ramdisk_block_partition *)dev;
    blk_device = (struct ramdisk_block_device *)device_part->user_data;
    block_size = device_part->sector_size;

    RAMDISK_DBG("%s offset = %08x,size = %08x\n",__func__, offset, length);

    if (device_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > device_part->size) {
            RAMDISK_DBG("ERROR: erase size > partition size, pos=%d, size=%d, partition_size=%d\n", offset, length, device_part->size);
            return 0;
        }

        if (length % block_size != 0) {
            RAMDISK_DBG("ERROR: erase size must align to BLOCK SIZE\n");
            return 0;
        }

        block_device_erase(blk_device, (device_part->offset + offset), length);

        return length;
    }

    RAMDISK_DBG("ERROR: unknown device type..\n");
    return 0;
}


const static struct ramdisk_block_driver_ops block_part_blk_ops =
{
    .read                   = ramdisk_block_partition_blk_read,
    .write                  = ramdisk_block_partition_blk_write,
    .erase_block            = ramdisk_block_partition_blk_erase,
};


long ramdisk_block_device_init_partition_ops(const char *device_name,struct ramdisk_block_partition *parts)
{
    struct ramdisk_block_partition *device_part;
    struct ramdisk_block_device *blk_device;

    blk_device = (struct ramdisk_block_device *)device_find(device_name);
    if (blk_device == NULL)
        return -EIO;

    if (parts == NULL)
        return -EIO;

    blk_device->device_parts = parts;
    for (device_part = parts; device_part->name != NULL; device_part++) {
        RAMDISK_DBG("part name: %s\n",device_part->name);
        /* get partition type */
        if(device_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* set device interface */
            device_part->blk.type       = Device_Class_Block;
            device_part->blk.name       = device_part->name;
            device_part->blk.init       = ramdisk_block_device_blk_init;
            device_part->blk.open       = ramdisk_block_device_blk_open;
            device_part->blk.read       = ramdisk_block_device_blk_read;
            device_part->blk.write      = ramdisk_block_device_blk_write;
            device_part->blk.close      = ramdisk_block_device_blk_close;
            device_part->blk.control    = ramdisk_block_device_blk_control;
            device_part->blk.flag       = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;

            device_part->user_data     = blk_device;

            /* register device */
            device_register(&device_part->blk);
        } else if (device_part->mask_flags & PART_TYPE_MTD) { /* It's a MTD device */
            RAMDISK_DBG("part name: %s\n",device_part->name);

            device_part->user_data = blk_device;

            /* Init MTD NOR device interface ... */
            device_part->device.block_size         = device_part->sector_size;
            device_part->device.block_start        = 0;
            device_part->device.block_end          = device_part->size / device_part->sector_size;
            device_part->device.ops                = &block_part_blk_ops;

            ramdisk_block_register_device(device_part->name, &device_part->device);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", device_part->name);
        }
    }

    return 0;
}

long ramdisk_block_device_deinit_partition_ops(struct ramdisk_block_device *blk_device)
{
    if (blk_device == NULL)
        return -EIO;

    struct ramdisk_block_partition *parts = blk_device->device_parts;
    struct ramdisk_block_partition *device_part;

    if (parts == NULL)
        return -EIO;

    for (device_part = parts; device_part->name != NULL; device_part++) {
        RAMDISK_DBG("part name: %s\n",device_part->name);
        /* get partition type */
        if (device_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* unregister device */
            device_unregister(&device_part->blk);

        } else if (device_part->mask_flags & PART_TYPE_MTD) {
            /* It's a MTD device */
            RAMDISK_DBG("part name: %s\n",device_part->name);

            ramdisk_block_unregister_device(blk_device);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", device_part->name);
        }
    }

    return 0;
}