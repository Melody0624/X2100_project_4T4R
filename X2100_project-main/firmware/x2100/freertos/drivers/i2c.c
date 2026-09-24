#include <driver/i2c.h>
#include <common.h>
#include <os.h>
#include <driver/i2c_bus.h>

__weak void soc_i2c_init_driver(void)
{

}

__weak void gpio_i2c_init_driver(void)
{

}

static LIST_HEAD(i2c_bus_list);

static struct i2c_bus *find_i2c_bus(int id)
{
    struct list_head *pos;

    list_for_each(pos, &i2c_bus_list) {
        struct i2c_bus *i2c_bus = list_entry(pos, struct i2c_bus, link);
        if (i2c_bus->i2c_bus_id == id)
            return i2c_bus;
    }

    return NULL;
}

int i2c_bus_register(
    struct i2c_bus *i2c_bus,
    int i2c_bus_id,
    const char *i2c_bus_name,
    const struct i2c_bus_ops *i2c_bus_ops
)
{
    int ret = 0;

    os_enter_critical();

    assert(i2c_bus);
    assert(i2c_bus_name);
    assert(i2c_bus_ops);
    assert(i2c_bus_ops->ops_i2c_register);
    assert(i2c_bus_ops->ops_i2c_unregister);
    assert(i2c_bus_ops->ops_i2c_transfer);
    assert(i2c_bus_ops->ops_i2c_detect_device);

    struct i2c_bus *tmp = find_i2c_bus(i2c_bus_id);
    if (tmp) {
        printf("i2c_bus: %d has been registered by %s\n", i2c_bus_id, tmp->i2c_bus_name);
        ret = -EBUSY;
        goto unlock;
    }

    i2c_bus->i2c_bus_id = i2c_bus_id;
    i2c_bus->i2c_bus_name = i2c_bus_name;
    i2c_bus->i2c_bus_ops = i2c_bus_ops;

    list_add_tail(&i2c_bus->link, &i2c_bus_list);
    INIT_LIST_HEAD(&i2c_bus->list);
    mutex_init(&i2c_bus->lock);

unlock:
    os_exit_critical();
    return ret;
}

void i2c_bus_unregister(struct i2c_bus *i2c_bus)
{
    os_enter_critical();

    assert(i2c_bus);

    struct i2c_bus *tmp = find_i2c_bus(i2c_bus->i2c_bus_id);
    if (!tmp)
        panic("i2c_bus: %d has not been registered\n", i2c_bus->i2c_bus_id);

    if (tmp != i2c_bus)
        panic("i2c_bus: %d not equal, %p %p\n", i2c_bus->i2c_bus_id, tmp, i2c_bus);

    list_del(&i2c_bus->link);

    os_exit_critical();

    mutex_lock(&i2c_bus->lock);

    if (!list_empty(&i2c_bus->list)) {
        struct list_head *pos;

        printf("i2c_bus: %d(%s) has devices\n",
             i2c_bus->i2c_bus_id, i2c_bus->i2c_bus_name);
        list_for_each(pos, &i2c_bus->list) {
            struct i2c_device *dev = list_entry(pos, struct i2c_device, link);
            printf("%s\n", dev->i2c_bus->i2c_bus_name);
        }
        panic("\n");
    }

    mutex_unlock(&i2c_bus->lock);
}

static struct i2c_device *find_i2c_dev(struct i2c_bus *i2c_bus, struct i2c_device *dev)
{
    struct list_head *pos;

    list_for_each(pos, &i2c_bus->list) {
        struct i2c_device *tmp = list_entry(pos, struct i2c_device, link);
        if (tmp == dev)
            return tmp;
    }

    return NULL;
}

struct i2c_device *i2c_register(int i2c_bus_num, unsigned short addr, enum i2c_addr_type addr_bit, char *name)
{
    struct i2c_bus *i2c_bus;
    struct i2c_device *tmp;
    unsigned short tmp_addr;

    os_enter_critical();
    i2c_bus = find_i2c_bus(i2c_bus_num);
    os_exit_critical();

    if (!i2c_bus) {
        printf("i2c_bus: %d not find\n", i2c_bus_num);
        return NULL;
    }

    mutex_lock(&i2c_bus->lock);

    list_for_each_entry(tmp, &i2c_bus->list, link) {
        tmp_addr = tmp->addr;

        if (addr_bit != tmp->addr_bit) {
            if (addr_bit == I2C_ADDR_BIT_10)
                addr = addr >> 3 & 0x7f;

            if (tmp->addr_bit == I2C_ADDR_BIT_10)
                tmp_addr = tmp->addr >> 3 & 0x7f;
        }

        if (tmp_addr == addr) {
            mutex_unlock(&i2c_bus->lock);
            printf("i2c_bus: %s register faild.i2c_bus%d exist the dev:%s with the same address %x.\n", name, i2c_bus_num, tmp->name, tmp_addr);
            return NULL;
        }
    }

    struct i2c_device *dev =
        i2c_bus->i2c_bus_ops->ops_i2c_register(i2c_bus, i2c_bus_num, addr, addr_bit, name);
    if (dev) {
        dev->bus_num = i2c_bus_num;
        dev->addr = addr;
        dev->addr_bit = addr_bit;
        dev->name = name;
        dev->i2c_bus = i2c_bus;
        list_add_tail(&dev->link, &i2c_bus->list);
        wake_lock_init(&dev->w_lock, "i2c_wake_lock");
    }

    mutex_unlock(&i2c_bus->lock);
    return dev;
}

void i2c_unregister(struct i2c_device *dev)
{
    assert(dev);

    os_enter_critical();
    struct i2c_bus *i2c_bus = find_i2c_bus(dev->bus_num);
    os_exit_critical();

    if (i2c_bus != dev->i2c_bus) {
        printf("i2c_unregister err %p %p\n", i2c_bus, dev->i2c_bus);
        return;
    }

    mutex_lock(&i2c_bus->lock);

    if (find_i2c_dev(i2c_bus, dev)) {
        list_del(&dev->link);
        wake_lock_deinit(&dev->w_lock);
        i2c_bus->i2c_bus_ops->ops_i2c_unregister(i2c_bus, dev);
    }

    mutex_unlock(&i2c_bus->lock);
}

int i2c_transfer(struct i2c_device *dev, struct i2c_msg *msg, int count)
{
    int ret;
    struct i2c_bus *i2c_bus = dev->i2c_bus;

    mutex_lock(&i2c_bus->lock);

    wake_lock(&dev->w_lock);

    ret = i2c_bus->i2c_bus_ops->ops_i2c_transfer(i2c_bus, dev, msg, count);

    wake_unlock(&dev->w_lock);

    mutex_unlock(&i2c_bus->lock);

    return ret;
}

int i2c_detect_device(struct i2c_device *dev)
{
    int ret;

    struct i2c_bus *i2c_bus = dev->i2c_bus;

    mutex_lock(&i2c_bus->lock);

    wake_lock(&dev->w_lock);

    ret = i2c_bus->i2c_bus_ops->ops_i2c_detect_device(i2c_bus, dev);

    wake_unlock(&dev->w_lock);

    mutex_unlock(&i2c_bus->lock);

    return ret;
}

void i2c_init(void)
{
    soc_i2c_init_driver();
    gpio_i2c_init_driver();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(i2c_register);
EXPORT_SYMBOL(i2c_transfer);
EXPORT_SYMBOL(i2c_unregister);
EXPORT_SYMBOL(i2c_detect_device);