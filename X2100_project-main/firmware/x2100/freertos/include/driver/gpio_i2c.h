#ifndef __GPIO_I2C_H__
#define __GPIO_I2C_H__

#include <driver/i2c_bus.h>

struct gpio_i2c_bus_data {
    int i2c_bus_id;
    int gpio_scl;
    int gpio_sda;
    unsigned int clk_rate;
};

void gpio_i2c_init_driver(void);
void gpio_i2c_remove_bus(struct i2c_bus *i2c_bus);
struct i2c_bus* gpio_i2c_add_bus(struct gpio_i2c_bus_data *i2c);

#endif
