#ifndef _SOC_REGULATOR_H_
#define _SOC_REGULATOR_H_

#include <os/mutex.h>
#include <list.h>

struct regulator_device;

struct regulator_ops {
    int (*regulator_enable)(struct regulator_device *device);
    int (*regulator_disable)(struct regulator_device *device);
    int (*regulator_is_enable)(struct regulator_device *device);
};

struct regulator_device {
    const char *reg_name;
    int ref_count;
    struct regulator_ops *ops;
    struct list_head node;
    struct mutex lock;
};

void regulator_register(struct regulator_device *regulator,
                        const char *name, struct regulator_ops *ops);
void regulator_unregister(struct regulator_device *regulator);

struct regulator;

struct regulator *regulator_get(const char *name);
void regulator_put(struct regulator *regulator);
int regulator_enable(struct regulator *regulator);
int regulator_disable(struct regulator *regulator);
int regulator_is_enable(struct regulator *regulator);

#endif /* _SOC_REGULATOR_H_ */