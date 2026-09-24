#ifndef _SOC_GPIO_REGULATOR_H_
#define _SOC_GPIO_REGULATOR_H_

#include <driver/regulator.h>

struct gpio_regulator;

struct gpio_regulator *gpio_regulator_register(int gpio, int active_level, const char *name);
void gpio_regulator_unregister(struct gpio_regulator *regulator);
void gpio_regulator_init(void);

#endif /* _SOC_GPIO_REGULATOR_H_ */