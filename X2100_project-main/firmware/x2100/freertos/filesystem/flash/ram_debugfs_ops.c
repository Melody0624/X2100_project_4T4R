#include <stdlib.h>
#include <dfs_device.h>

#define RAM_SECTOR_SIZE                 (512)

struct ram_device {
    struct device dev;
    char name[16];
    uint8_t *start_address;
    uint32_t sector_count;
};

static inline struct ram_device *to_ramdisk_device_blk(struct device *dev)
{
    return container_of(dev, struct ram_device, dev);
}


/* Device interface */
static int ramdisk_device_blk_init(device_t dev)
{
    return 0;
}

static int ramdisk_device_blk_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int ramdisk_device_blk_close(device_t dev)
{
    return 0;
}

static int ramdisk_device_blk_control(device_t dev, uint8_t cmd, void *args)
{
    assert(dev != NULL);
    struct ram_device *ram_device = to_ramdisk_device_blk(dev);

    switch (cmd) {
    case DEVICE_CTRL_BLK_GETGEOME:
    {
        struct device_blk_geometry *geometry;

        geometry = (struct device_blk_geometry *)args;
        if (geometry == NULL)
            return EINVAL;

        geometry->bytes_per_sector  = RAM_SECTOR_SIZE;
        geometry->sector_count      = ram_device->sector_count;
        geometry->block_size        = RAM_SECTOR_SIZE;

        break;
    }

    default:
        break;
    }

    return 0;
}

static uint32_t ramdisk_device_blk_read(device_t dev, uint64_t pos, void* buffer, uint32_t size)
{
    struct ram_device *ram_device = to_ramdisk_device_blk(dev);

    memcpy(buffer, ram_device->start_address + RAM_SECTOR_SIZE * pos, RAM_SECTOR_SIZE * size);

    return size;
}

static uint32_t ramdisk_device_blk_write(device_t dev, uint64_t pos, const void* buffer, uint32_t size)
{
    struct ram_device *ram_device = to_ramdisk_device_blk(dev);

    memcpy(ram_device->start_address + RAM_SECTOR_SIZE * pos, buffer, RAM_SECTOR_SIZE * size);

    return size;
}


int ramdisk_device_init_partition(char *device_name, void *start_address, uint32_t size)
{
    struct ram_device *ram_device;

    ram_device = (struct ram_device *)malloc(sizeof(struct ram_device));
    assert(ram_device != NULL);

    memset(ram_device, 0x00, sizeof(struct ram_device));

    /* set device interface */
    ram_device->dev.type      = Device_Class_Block;
    ram_device->dev.name      = device_name;
    ram_device->dev.init      = ramdisk_device_blk_init;
    ram_device->dev.open      = ramdisk_device_blk_open;
    ram_device->dev.read      = ramdisk_device_blk_read;
    ram_device->dev.write     = ramdisk_device_blk_write;
    ram_device->dev.close     = ramdisk_device_blk_close;
    ram_device->dev.control   = ramdisk_device_blk_control;
    ram_device->dev.flag      = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;
    ram_device->dev.user_data = ram_device;


    ram_device->start_address = start_address;
    ram_device->sector_count  = size / RAM_SECTOR_SIZE;

    /* register device */
    device_register(&ram_device->dev);

    return 0;
}
