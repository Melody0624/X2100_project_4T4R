#include <driver/gpio.h>

/*
 * soc 需要实现
 */
extern int soc_gpio_request(int gpio, const char *name);
extern void soc_gpio_release(int gpio);
extern void soc_gpio_direction_input(int gpio);
extern void soc_gpio_direction_output(int gpio, int value);
extern int soc_gpio_get_value(int gpio);
extern void soc_gpio_set_value(int gpio , int value);
extern enum gpio_function soc_gpio_get_func(int gpio);
extern void soc_gpio_set_func(int gpio, enum gpio_function func);

int gpio_request(int gpio, const char *name)
{
    return gpio_is_valid(gpio) ? soc_gpio_request(gpio, name) : -1;
}

void gpio_release(int gpio)
{
    soc_gpio_release(gpio);
}

void gpio_direction_input(int gpio)
{
    soc_gpio_direction_input(gpio);
}

void gpio_direction_output(int gpio, int value)
{
    soc_gpio_direction_output(gpio, value);
}

int gpio_get_value(int gpio)
{
    return soc_gpio_get_value(gpio);
}

void gpio_set_value(int gpio , int value)
{
    soc_gpio_set_value(gpio, value);
}

enum gpio_function gpio_get_func(int gpio)
{
    return soc_gpio_get_func(gpio);
}

void gpio_set_func(int gpio, enum gpio_function func)
{
    soc_gpio_set_func(gpio, func);
}

char *gpio_to_str(int gpio, char *buf, size_t size)
{
    assert(buf);

    if (gpio == -1) {
        snprintf(buf, size, "-1");
        return buf;
    }

    if (gpio < -1 || gpio >= (('g' - 'a' + 1) * 32)) {
        snprintf(buf, size, "error");
        return buf;
    }

    snprintf(buf, size, "P%c%02d", 'A' + (gpio / 32), gpio % 32);

    return buf;
}

int str_to_gpio(const char *str)
{
    int gpio_n;
    int ret = -1;
    int gpio_num;
    unsigned int str_len;

    str_len = strlen(str);
    if(str[0] != 'p' && str[0] != 'P' && str_len != 3 && str_len != 4)
        return ret;

    if(str[1] >= 'A' && str[1] <= 'G')
        gpio_num = str[1] - 'A';
    else if(str[1] >= 'a' && str[1] <= 'g')
        gpio_num = str[1] - 'a';
    else
        return ret;

    ret = sscanf(str + 2, "%d", &gpio_n);
    if(ret < 0 || gpio_n <0 || gpio_n > 31)
        return ret;

    ret = gpio_num * 32 + gpio_n;
    return ret;
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(str_to_gpio);
EXPORT_SYMBOL(gpio_request);
EXPORT_SYMBOL(gpio_release);
EXPORT_SYMBOL(gpio_direction_input);
EXPORT_SYMBOL(gpio_direction_output);
EXPORT_SYMBOL(gpio_get_value);
EXPORT_SYMBOL(gpio_set_value);
EXPORT_SYMBOL(gpio_to_str);
EXPORT_SYMBOL(gpio_get_func);
EXPORT_SYMBOL(gpio_set_func);