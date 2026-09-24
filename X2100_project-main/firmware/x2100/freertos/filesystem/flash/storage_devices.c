#include <dfs_device.h>
#include <storage_device_driver.h>

/*
 * Generic Device Interface
 */
static int _storage_device_init(device_t dev)
{
    return 0;
}

static int _storage_device_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int _storage_device_close(device_t dev)
{
    return 0;
}

static uint32_t _storage_device_read(device_t dev, uint64_t pos, void *buffer, uint32_t size)
{
    return size;
}

static uint32_t _storage_device_write(device_t dev, uint64_t pos, const void *buffer, uint32_t size)
{
    return size;
}

static int _storage_device_control(device_t dev, uint8_t cmd, void *args)
{
    return 0;
}


long storage_devices_register_device(const char *name, struct storage_device *device)
{
    device_t dev;

    dev = (device_t)device;
    assert(dev != NULL);

    /* set device class and generic device interface */
    dev->name        = name;
    dev->type        = Device_Class_Block;

    dev->init        = _storage_device_init;
    dev->open        = _storage_device_open;
    dev->read        = _storage_device_read;
    dev->write       = _storage_device_write;
    dev->close       = _storage_device_close;
    dev->control     = _storage_device_control;

    dev->flag        = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;

    /* register to device system */
    return device_register(dev);
}

long storage_devices_unregister_device(struct storage_device *device)
{
    device_t dev;

    dev = (device_t)device;
    assert(dev != NULL);

    /* unregister to device system */
    return device_unregister(dev);
}