#include <common.h>
#include <os.h>
#include <string.h>
#include "dfs_device.h"

static struct list_head dev_list;
static struct mutex dev_list_lock;

void dfs_device_init(void)
{
    INIT_LIST_HEAD(&dev_list);
    mutex_init(&dev_list_lock);
}

struct list_head *get_device_list(void)
{
    return &dev_list;
}

device_t device_find(const char *name)
{
    struct device *dev = NULL;

    if (name == NULL)
        return NULL;

    os_enter_critical();

    struct list_head *pos;
    list_for_each(pos, &dev_list) {
        dev = list_entry(pos, struct device, link);
        if (!strcmp(name, dev->name))
            break;

        dev = NULL;
    }

    os_exit_critical();

    return dev;
}

int device_register(device_t dev)
{
    assert(dev);
    assert(dev->name);

    os_enter_critical();

    assert(!device_find(dev->name));

    dev->ref_count = 0;
    list_add_tail(&dev->link, &dev_list);

    os_exit_critical();

    return EOK;
}

int device_unregister(device_t dev)
{
    assert(dev);

    os_enter_critical();

    if (device_find(dev->name) != dev)
        panic("device is not registered\n");

    if (dev->ref_count)
        printf ("device is still opened while unregister\n");

    list_del(&dev->link);

    os_exit_critical();

    return EOK;
}

int device_open(device_t dev, uint16_t oflag)
{
    int ret = EOK;

    mutex_lock(&dev_list_lock);

    if (dev->ref_count++ == 0)
        ret = dev->open(dev, oflag);

    if (ret != EOK)
        dev->ref_count--;

    mutex_unlock(&dev_list_lock);

    return ret;
}

int device_close(device_t dev)
{
    int ret = EOK;

    mutex_lock(&dev_list_lock);

    assert(dev->ref_count);

    if (--dev->ref_count == 0)
        ret = dev->close(dev);

    mutex_unlock(&dev_list_lock);

    return ret;
}

uint32_t device_read(device_t dev, uint64_t pos, void *buffer, uint32_t size)
{
    if (!dev->ref_count)
        return -EINVAL;

    if (dev->read)
        return dev->read(dev, pos, buffer, size);
    else
        return -ENODEV;
}

uint32_t device_write(device_t dev, uint64_t pos, const void *buffer, uint32_t size)
{
    if (!dev->ref_count)
        return -EINVAL;

    if (dev->write)
        return dev->write(dev, pos, buffer, size);
    else
        return -ENODEV;
}

int  device_control(device_t dev, uint8_t cmd, void *arg)
{
    if (!dev->ref_count)
        return -EINVAL;

    if (dev->control)
        return dev->control(dev, cmd, arg);
    else
        return -ENODEV;
}
