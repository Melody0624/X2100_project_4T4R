#ifndef _GPIO_BACKLIGHT_H
#define _GPIO_BACKLIGHT_H
#include <driver/gpio.h>
#include <driver/gpio_pin.h>

struct gpio_backlight;

void gpio_backlight_init(void);
struct gpio_backlight *gpio_backlight_register(struct gpio_pin gpio, const char *name);
void gpio_backlight_unregiser(struct gpio_backlight *backlight);

#endif