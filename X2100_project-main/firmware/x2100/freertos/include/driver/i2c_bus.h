#ifndef _I2C_BUS_H_
#define _I2C_BUS_H_

#include <list.h>

#include <os.h>
#include <driver/i2c.h>

struct i2c_bus;

struct i2c_bus_ops {
    struct i2c_device *(*ops_i2c_register)(
        struct i2c_bus *i2c_bus,
        int i2c_bus_num,
        unsigned short addr,
        enum i2c_addr_type addr_bit,
        char *name);
    void (*ops_i2c_unregister)(
        struct i2c_bus *i2c_bus,
        struct i2c_device *dev);
    int (*ops_i2c_transfer)(
        struct i2c_bus *i2c_bus,
        struct i2c_device *dev, struct i2c_msg *msg, int count);
    int (*ops_i2c_detect_device)(
        struct i2c_bus *i2c_bus,
        struct i2c_device *dev);
};

struct i2c_bus {
/* public members */
    int i2c_bus_id;
    const char *i2c_bus_name;
    const struct i2c_bus_ops *i2c_bus_ops;

/* private members */
    struct mutex lock;
    struct list_head list;
    struct list_head link;
};

int i2c_bus_register(
    struct i2c_bus *i2c_bus,
    int i2c_bus_id,
    const char *i2c_bus_name,
    const struct i2c_bus_ops *i2c_bus_ops
);

void i2c_bus_unregister(struct i2c_bus *i2c_bus);

#endif /* _I2C_BUS_H_ */