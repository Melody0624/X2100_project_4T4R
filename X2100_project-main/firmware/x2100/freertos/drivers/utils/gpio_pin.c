#include <driver/gpio_pin.h>

int gpio_pin_request(struct gpio_pin pin, const char *name)
{
    if (!gpio_pin_is_valid(pin))
        return 0;

    return gpio_request(pin.gpio, name);
}

void gpio_pin_release(struct gpio_pin pin)
{
    if (!gpio_pin_is_valid(pin))
        return;

    gpio_release(pin.gpio);
}

void gpio_pin_output_enable(struct gpio_pin pin)
{
    if (gpio_pin_is_valid(pin))
        gpio_direction_output(pin.gpio, !!pin.enable_level);
}

void gpio_pin_output_disable(struct gpio_pin pin)
{
    if (gpio_pin_is_valid(pin))
        gpio_direction_output(pin.gpio, !pin.enable_level);
}

void gpio_pin_as_input(struct gpio_pin pin)
{
    if (gpio_pin_is_valid(pin))
        gpio_direction_input(pin.gpio);
}

int gpio_pin_is_enable(struct gpio_pin pin)
{
    if (!gpio_pin_is_valid(pin))
        return 0;

    return gpio_get_value(pin.gpio) == !!pin.enable_level;
}
