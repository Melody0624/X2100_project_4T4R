#include <dfs_device.h>
#include <mtd_driver_nor.h>

/*
 * Generic Device Interface
 */
static int _mtd_init(device_t dev)
{
    return 0;
}

static int _mtd_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int _mtd_close(device_t dev)
{
    return 0;
}

static uint32_t _mtd_read(device_t dev, uint64_t pos, void *buffer, uint32_t size)
{
    return size;
}

static uint32_t _mtd_write(device_t dev, uint64_t pos, const void *buffer, uint32_t size)
{
    return size;
}

static int _mtd_control(device_t dev, uint8_t cmd, void *args)
{
    return 0;
}



long mtd_devices_nor_register_device(const char *name, struct mtd_nor_device *device)
{
    device_t dev;

    dev = (device_t)device;
    assert(dev != NULL);

    /* set device class and generic device interface */
    dev->name        = name;
    dev->type        = Device_Class_MTD;

    dev->init        = _mtd_init;
    dev->open        = _mtd_open;
    dev->read        = _mtd_read;
    dev->write       = _mtd_write;
    dev->close       = _mtd_close;
    dev->control     = _mtd_control;

    dev->flag        = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;

    /* register to device system */
    return device_register(dev);
}
