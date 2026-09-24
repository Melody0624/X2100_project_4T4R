#include <list.h>
#include <assert.h>
#include <os.h>
#include <driver/gpio.h>
#include <driver/regulator.h>

static LIST_HEAD(regulator_list_head);

struct regulator *regulator_get(const char *name)
{
    struct list_head *p = NULL;
    struct regulator *regulator = NULL;

    os_enter_critical();

    list_for_each(p, &regulator_list_head) {
        struct regulator_device *device = list_entry(p, struct regulator_device, node);
        if (strcmp(name, device->reg_name) == 0) {
            regulator = (struct regulator *)device;
            break;
        }
    }

    os_exit_critical();

    return regulator;
}

void regulator_put(struct regulator *regulator)
{
    (void)regulator;
}

int regulator_enable(struct regulator *regulator)
{
    int ret = 0;
    struct regulator_device *device = (void *)regulator;

    mutex_lock(&device->lock);

    if (!device->ops->regulator_enable) {
        ret = -ENODEV;
        goto unlock;
    }

    if (!device->ref_count) {
        ret = device->ops->regulator_enable(device);
        if (ret)
            goto unlock;
    }

    device->ref_count++;

unlock:
    mutex_unlock(&device->lock);
    return ret;
}

int regulator_disable(struct regulator *regulator)
{
    int ret = 0;
    struct regulator_device *device = (void *)regulator;

    mutex_lock(&device->lock);

    if (--device->ref_count > 0)
        goto unlock;

    device->ref_count = 0;

    if (device->ops->regulator_disable)
        ret = device->ops->regulator_disable(device);
    else
        ret = -ENODEV;

unlock:
    mutex_unlock(&device->lock);
    return ret;
}

int regulator_is_enable(struct regulator *regulator)
{
    int ret = 0;
    struct regulator_device *device = (void *)regulator;

    mutex_lock(&device->lock);

    if (device->ops->regulator_is_enable)
        ret = device->ops->regulator_is_enable(device);

    if (device->ref_count) {
        ret = 1;
        goto unlock;
    }

unlock:
    mutex_unlock(&device->lock);
    return ret;
}

void regulator_register(struct regulator_device *regulator,
                        const char *name, struct regulator_ops *ops)
{
    os_enter_critical();

    if (regulator == NULL)
        panic("error!! regulator is NULL");

    if (name == NULL || !strlen(name))
        panic("error!! regulator's reg_name is NULL or empty\n");

    if (ops == NULL)
        panic("error!! regulator's ops is NULL");

    if (regulator_get(name))
        panic("error!! regulator's %s has been registered\n", name);

    regulator->reg_name = name;
    regulator->ops = ops;
    regulator->ref_count = 0;

    mutex_init(&regulator->lock);

    list_add_tail(&regulator->node, &regulator_list_head);

    os_exit_critical();
}

void regulator_unregister(struct regulator_device *regulator)
{
    os_enter_critical();

    list_del(&regulator->node);

    os_exit_critical();
}
