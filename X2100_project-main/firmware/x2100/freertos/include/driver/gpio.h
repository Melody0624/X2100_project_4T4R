#ifndef _GPIO_H_
#define _GPIO_H_

#include <soc/gpio.h>
#include<malloc.h>
#include<common.h>

static inline int gpio_is_valid(int gpio)
{
    return soc_gpio_is_valid(gpio);
}

int gpio_request(int gpio, const char *name);
void gpio_release(int gpio);

void gpio_direction_input(int gpio);
void gpio_direction_output(int gpio, int value);

int gpio_get_value(int gpio);
void gpio_set_value(int gpio , int value);
char *gpio_to_str(int gpio, char *buf, size_t size);
int str_to_gpio(const char *str);

enum gpio_function gpio_get_func(int gpio);
void gpio_set_func(int gpio, enum gpio_function func);

#endif /* _GPIO_H_ */