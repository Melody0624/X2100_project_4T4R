#include <assert.h>
#include <driver/gpio.h>
#include <driver/gpio_pin.h>
#include <driver/gpio_regulator.h>

struct gpio_regulator {
    struct regulator_device dev;
    int gpio;
    int active_level;
};

struct gpio_regulator_data {
    struct gpio_pin gpio;
    const char *name;
};

static struct gpio_regulator_data regulator_data [] = {
#ifdef CONFIG_GPIO_REGULATOR0
    {
        .gpio = {CONFIG_GPIO_REGULATOR0_GPIO},
        .name = CONFIG_GPIO_REGULATOR0_NAME,
    },
#endif

#ifdef CONFIG_GPIO_REGULATOR1
    {
        .gpio = {CONFIG_GPIO_REGULATOR1_GPIO},
        .name = CONFIG_GPIO_REGULATOR1_NAME,
    },
#endif

#ifdef CONFIG_GPIO_REGULATOR2
    {
        .gpio = {CONFIG_GPIO_REGULATOR2_GPIO},
        .name = CONFIG_GPIO_REGULATOR2_NAME,
    },
#endif
};


static int gpio_regulator_enable(struct regulator_device *device)
{
    struct gpio_regulator *data = (void *)device;

    gpio_direction_output(data->gpio, data->active_level);
    return 0;
}

static int gpio_regulator_disable(struct regulator_device *device)
{
    struct gpio_regulator *data = (void *)device;

    gpio_direction_output(data->gpio, !data->active_level);
    return 0;
}

static int gpio_regulator_is_enable(struct regulator_device *device)
{
    struct gpio_regulator *data = (void *)device;

    return gpio_get_value(data->gpio) == data->active_level;
}

static struct regulator_ops gpio_regulator_ops = {
    .regulator_enable = gpio_regulator_enable,
    .regulator_disable = gpio_regulator_disable,
    .regulator_is_enable = gpio_regulator_is_enable,
};

struct gpio_regulator *gpio_regulator_register(int gpio, int active_level, const char *name)
{
    int ret;
    struct gpio_regulator *regulator = malloc(sizeof(struct gpio_regulator));
    assert(regulator);

    ret = gpio_request(gpio, name);
    assert(!ret);

    regulator->gpio = gpio;
    regulator->active_level = active_level;

    regulator_register(&regulator->dev, name, &gpio_regulator_ops);

    return regulator;
}

void gpio_regulator_unregister(struct gpio_regulator *regulator)
{
    regulator_unregister(&regulator->dev);

    gpio_release(regulator->gpio);

    free(regulator);
}

void gpio_regulator_init(void)
{
    int i, len;
    len = ARRAY_SIZE(regulator_data);

    for (i = 0; i < len; i++) {
        if (regulator_data[i].gpio.gpio == -1)
            continue;

        gpio_regulator_register(regulator_data[i].gpio.gpio, regulator_data[i].gpio.enable_level, regulator_data[i].name);
    }

}
