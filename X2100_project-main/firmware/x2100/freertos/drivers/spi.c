#include <driver/spi.h>
#include <common.h>
#include <driver/gpio.h>

#include <os.h>
#include <driver/gpio_spi.h>
#include <driver/spi_bus.h>

__weak void soc_spi_init_driver(void)
{

}

__weak void gpio_spi_init_driver(void)
{

}

static LIST_HEAD(spi_bus_list);

static struct spi_bus *find_spi_bus(int id)
{
    struct list_head *pos;

    list_for_each(pos, &spi_bus_list) {
        struct spi_bus *spi_bus = list_entry(pos, struct spi_bus, link);
        if (spi_bus->spi_bus_id == id)
            return spi_bus;
    }

    return NULL;
}

int spi_bus_register(
    struct spi_bus *spi_bus,
    int spi_bus_id,
    const char *spi_bus_name,
    const struct spi_bus_ops *spi_bus_ops
)
{
    int ret = 0;

    os_enter_critical();

    assert(spi_bus);
    assert(spi_bus_name);
    assert(spi_bus_ops);
    assert(spi_bus_ops->ops_spi_register);
    assert(spi_bus_ops->ops_spi_unregister);
    assert(spi_bus_ops->ops_spi_transfer);

    struct spi_bus *tmp = find_spi_bus(spi_bus_id);
    if (tmp) {
        printf("spi_bus: %d has been registered by %s\n", spi_bus_id, tmp->spi_bus_name);
        ret = -EBUSY;
        goto unlock;
    }

    spi_bus->spi_bus_id = spi_bus_id;
    spi_bus->spi_bus_name = spi_bus_name;
    spi_bus->spi_bus_ops = spi_bus_ops;

    list_add_tail(&spi_bus->link, &spi_bus_list);
    INIT_LIST_HEAD(&spi_bus->list);
    mutex_init(&spi_bus->lock);

unlock:
    os_exit_critical();
    return ret;
}

void spi_bus_unregister(struct spi_bus *spi_bus)
{
    os_enter_critical();

    assert(spi_bus);

    struct spi_bus *tmp = find_spi_bus(spi_bus->spi_bus_id);
    if (!tmp)
        panic("spi_bus: %d has not been registered\n", spi_bus->spi_bus_id);

    if (tmp != spi_bus)
        panic("spi_bus: %d not equal, %p %p\n", spi_bus->spi_bus_id, tmp, spi_bus);

    list_del(&spi_bus->link);

    os_exit_critical();

    mutex_lock(&spi_bus->lock);

    if (!list_empty(&spi_bus->list)) {
        struct list_head *pos;

        printf("spi_bus: %d(%s) has devices\n",
             spi_bus->spi_bus_id, spi_bus->spi_bus_name);
        list_for_each(pos, &spi_bus->list) {
            struct spi_device *dev = list_entry(pos, struct spi_device, link);
            printf("%s\n", dev->config.name);
        }
        panic("\n");
    }

    mutex_unlock(&spi_bus->lock);
}

static struct spi_device *find_spi_dev(struct spi_bus *spi_bus, struct spi_device *dev)
{
    struct list_head *pos;

    list_for_each(pos, &spi_bus->list) {
        struct spi_device *tmp = list_entry(pos, struct spi_device, link);
        if (tmp == dev)
            return tmp;
    }

    return NULL;
}

struct spi_device *spi_register(struct spi_config_data *config)
{
    struct spi_bus *spi_bus;

    os_enter_critical();
    spi_bus = find_spi_bus(config->id);
    os_exit_critical();

    if (!spi_bus) {
        printf("spi_bus: %d not find\n", config->id);
        return NULL;
    }

    mutex_lock(&spi_bus->lock);

    struct spi_device *dev =
        spi_bus->spi_bus_ops->ops_spi_register(spi_bus, config);
    if (dev) {
        dev->spi_bus_id = spi_bus->spi_bus_id;
        dev->spi_bus = spi_bus;
        dev->config = *config;
        list_add_tail(&dev->link, &spi_bus->list);
        wake_lock_init(&dev->w_lock, "spi_wake_lock");
    }

    mutex_unlock(&spi_bus->lock);

    return dev;
}

void spi_unregister(struct spi_device *dev)
{
    assert(dev);

    os_enter_critical();
    struct spi_bus *spi_bus = find_spi_bus(dev->spi_bus_id);
    os_exit_critical();

    if (spi_bus != dev->spi_bus) {
        printf("spi_unregister err %p %p\n", spi_bus, dev->spi_bus);
        return;
    }

    mutex_lock(&spi_bus->lock);

    if (find_spi_dev(spi_bus, dev)) {
        list_del(&dev->link);
        wake_lock_deinit(&dev->w_lock);
        spi_bus->spi_bus_ops->ops_spi_unregister(spi_bus, dev);
    }

    mutex_unlock(&spi_bus->lock);
}

void spi_transfer(struct spi_device *dev, struct spi_message *msg, int count)
{
    struct spi_bus *spi_bus = dev->spi_bus;

    mutex_lock(&spi_bus->lock);

    wake_lock(&dev->w_lock);

    spi_bus->spi_bus_ops->ops_spi_transfer(spi_bus, dev, msg, count);

    wake_unlock(&dev->w_lock);

    mutex_unlock(&spi_bus->lock);
}

void spi_dma_start(struct spi_device *dev, struct spi_message *msg, dma_async_cb dma_cb)
{
    struct spi_bus *spi_bus = dev->spi_bus;

    spi_bus->spi_bus_ops->ops_spi_dma_start(spi_bus, &dev->config, msg, dma_cb);
}

void spi_dma_stop(struct spi_device *dev)
{
    struct spi_bus *spi_bus = dev->spi_bus;

    spi_bus->spi_bus_ops->ops_spi_dma_stop(spi_bus, &dev->config);
}

void spi_dma_async_transfer(struct spi_device *dev, struct spi_message *msg)
{
    struct spi_bus *spi_bus = dev->spi_bus;

    spi_bus->spi_bus_ops->ops_spi_dma_async_transfer(spi_bus, dev, msg);
}

void spi_init(void)
{
    soc_spi_init_driver();
    gpio_spi_init_driver();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(spi_dma_start);
EXPORT_SYMBOL(spi_dma_stop);
EXPORT_SYMBOL(spi_dma_async_transfer);
EXPORT_SYMBOL(spi_transfer);
EXPORT_SYMBOL(spi_register);
EXPORT_SYMBOL(spi_unregister);
EXPORT_SYMBOL(spi_bus_register);
EXPORT_SYMBOL(spi_bus_unregister);