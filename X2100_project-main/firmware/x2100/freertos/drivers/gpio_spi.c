#include <driver/gpio_spi.h>
#include <driver/gpio.h>
#include <common.h>

struct gpio_spi_drv {
    int flag;
    char name[10];
    int is_enable;
    struct gpio_spi_bus_data bus_data;
    struct spi_bus spi_bus;
};

static struct gpio_spi_drv spi_driver[3] = {
    #ifdef CONFIG_GPIO_SPI0
    {
        .is_enable = 1,
        {
            .spi_bus_id = CONFIG_GPIO_SPI0_ID,
            .gpio_dout = CONFIG_GPIO_SPI0_DOUT,
            .gpio_din = CONFIG_GPIO_SPI0_DIN,
            .gpio_clk = CONFIG_GPIO_SPI0_CLK,
        }
    },
    #endif
    #ifdef CONFIG_GPIO_SPI1
    {
        .is_enable = 1,
        {
            .spi_bus_id = CONFIG_GPIO_SPI1_ID,
            .gpio_dout = CONFIG_GPIO_SPI1_DOUT,
            .gpio_din = CONFIG_GPIO_SPI1_DIN,
            .gpio_clk = CONFIG_GPIO_SPI1_CLK,
        }
    },
    #endif
    #ifdef CONFIG_GPIO_SPI2
    {
        .is_enable = 1,
        {
            .spi_bus_id = CONFIG_GPIO_SPI2_ID,
            .gpio_dout = CONFIG_GPIO_SPI2_DOUT,
            .gpio_din = CONFIG_GPIO_SPI2_DIN,
            .gpio_clk = CONFIG_GPIO_SPI2_CLK,
        }
    },
    #endif
};

static inline int bytes_per_sample(int bits_per_sample)
{
    int n = (bits_per_sample + 7) / 8;
    return n == 3 ? 4 : n;
}

static inline unsigned int read_sample(const unsigned char *value, int bytes_per_sample)
{
    switch (bytes_per_sample) {
    case 1:
        return *(unsigned char *)value;
    case 2:
        return *(unsigned short *)value;
    case 4:
    default:
        return *(unsigned int *)value;
    }
}

static inline void write_sample(const unsigned char *rx_buf, int bytes_per_sample, unsigned int value)
{
    switch (bytes_per_sample) {
    case 1:
        *(unsigned char *)rx_buf = value;
        break;
    case 2:
        *(unsigned short *)rx_buf = value;
        break;
    case 4:
    default:
        *(unsigned int *)rx_buf = value;
        break;
    }
}

static int spi_write_read_bit_pha0(struct gpio_spi_drv *spi,
                        unsigned int pol, unsigned int time_ns, int bit)
{
    if (gpio_is_valid(spi->bus_data.gpio_dout))
        gpio_set_value(spi->bus_data.gpio_dout, !!bit);

    ndelay(time_ns);

    gpio_set_value(spi->bus_data.gpio_clk, !pol);

    ndelay(time_ns);

    if (gpio_is_valid(spi->bus_data.gpio_din))
        bit = gpio_get_value(spi->bus_data.gpio_din);

    gpio_set_value(spi->bus_data.gpio_clk, pol);

    return !!bit;
}

static int spi_write_read_bit_pha1(struct gpio_spi_drv *spi,
                        unsigned int pol, unsigned int time_ns, int bit)
{
    gpio_set_value(spi->bus_data.gpio_clk, !pol);
    if (gpio_is_valid(spi->bus_data.gpio_dout))
        gpio_set_value(spi->bus_data.gpio_dout, !!bit);
    ndelay(time_ns);

    gpio_set_value(spi->bus_data.gpio_clk, pol);
    ndelay(time_ns);

    if (gpio_is_valid(spi->bus_data.gpio_din))
        bit = gpio_get_value(spi->bus_data.gpio_din);

    return !!bit;
}

static unsigned int spi_write_read_sample(struct gpio_spi_drv *spi,
                        struct spi_config_data *config,
                        int bits, unsigned int value)
{
    int bit;
    int i = 0;
    int tmp1, tmp2;
    unsigned int ret = 0;
    unsigned int time_ns = 1e9 / config->clk_rate / 2;

    while (1) {
        tmp1 = config->rx_endian ? (1 << i) : (1 << (bits - 1 - i));

        if (config->spi_pha)
            bit = spi_write_read_bit_pha1(spi, config->spi_pol, time_ns, value & tmp1);
        else
            bit = spi_write_read_bit_pha0(spi, config->spi_pol, time_ns, value & tmp1);

        if (bit) {
            tmp2 = config->tx_endian ? (1 << i) : (1 << (bits - 1 - i));
            ret |= tmp2;
        }

        if (i == bits -1)
            break;

        i++;
    }

    return ret;
}

static void spi_write_read(struct gpio_spi_drv *spi,
                    struct spi_config_data *config,
                    unsigned char *rx_buf, int rlen,
                    const unsigned char *tx_buf, int tlen)
{
    int bits = config->bits_per_word;
    int step = bytes_per_sample(bits);

    rlen = rlen - rlen % step;
    tlen = tlen - tlen % step;

    int value = 0;
    int n = 0;
    int N = rlen > tlen ? rlen : tlen;

    while (n < N) {
        value = read_sample(tx_buf, step);

        value = spi_write_read_sample(spi, config, bits, value);

        if (n < rlen)
            write_sample(rx_buf, step, value);

        n += 1;
        rx_buf += step;
        tx_buf += step;
    }
};


static inline void chip_select(struct spi_config_data *config, int chipselect)
{
    int pin = config->cs_pin;

    if (!gpio_is_valid(pin))
        return;

    if (chipselect)
        gpio_set_value(pin, config->cs_valid_level);
    else
        gpio_set_value(pin, !config->cs_valid_level);
}


static inline void gpio_init(int gpio, enum gpio_function func, const char *name)
{
    assert(!gpio_request(gpio, name));
    gpio_set_func(gpio,func);
}


static void ops_gpio_spi_unregister(struct spi_bus *bus, struct spi_device *dev)
{
    struct spi_config_data *config = &(dev->config);

    if (gpio_is_valid(config->cs_pin))
        gpio_release(config->cs_pin);

    free(dev);
}

static void ops_gpio_spi_transfer(struct spi_bus *bus,
                            struct spi_device *dev,
                            struct spi_message *msg,
                            int count)
{
    int i;
    struct spi_config_data *config = &(dev->config);
    unsigned int time_ns = 1e9 / config->clk_rate / 2;
    struct gpio_spi_drv *spi = container_of(bus, struct gpio_spi_drv, spi_bus);

    gpio_set_value(spi->bus_data.gpio_clk, config->spi_pol);
    chip_select(config, 1);
    ndelay(time_ns);

    for (i = 0; i < count; i++)
    {
        struct spi_message *m = msg + i;
        spi_write_read(spi, config, m->rx_buf, m->rlen, m->tx_buf, m->tlen);

        if (i < count - 1) {
            if (m->cs_change) {
                chip_select(config, 0);
                ndelay(time_ns);
                chip_select(config, 1);
                ndelay(time_ns);
            }
        }
    }

    chip_select(config, 0);
    ndelay(time_ns);
}

static struct spi_device* ops_gpio_spi_register(struct spi_bus *bus, struct spi_config_data *config)
{
    char *name;
    int pin, func;

    struct spi_device *spi = malloc(sizeof(struct spi_device));
    if (!spi)
        panic("%s: malloc space failed\n", __func__);

    if (config->name)
        name = config->name;
    else
        name = "gpio_spi_cs";

    pin = config->cs_pin;
    if (gpio_is_valid(pin)) {
        func = config->cs_valid_level ? GPIO_OUTPUT0 : GPIO_OUTPUT1;
        gpio_init(pin, func, name);
    }

    return spi;
}


static struct spi_bus_ops spi_bus_ops = {
    .ops_spi_register = ops_gpio_spi_register,
    .ops_spi_unregister = ops_gpio_spi_unregister,
    .ops_spi_transfer = ops_gpio_spi_transfer,
};

static void gpio_spi_init(struct gpio_spi_drv *spi, int id)
{
    if (gpio_is_valid(spi->bus_data.gpio_din))
        gpio_init(spi->bus_data.gpio_din, GPIO_INPUT, "gpio_spi_din");

    if (gpio_is_valid(spi->bus_data.gpio_dout))
        gpio_init(spi->bus_data.gpio_dout, GPIO_OUTPUT0, "gpio_spi_dout");

    if (gpio_is_valid(spi->bus_data.gpio_clk))
        gpio_init(spi->bus_data.gpio_clk, GPIO_OUTPUT0, "gpio_spi_clk");
    else
        panic("%s : gpio_spi_clk must specify pin\n", __func__);

    sprintf(spi->name, "SSI%d", id);

    int ret = spi_bus_register(&spi->spi_bus, id, spi->name, &spi_bus_ops);

    assert(!ret);
}


////////////////////////////////////////////////////////////////////////

void gpio_spi_remove_bus(struct spi_bus *spi_bus)
{
    struct gpio_spi_drv *drv = container_of(spi_bus, struct gpio_spi_drv, spi_bus);
    spi_bus_unregister(spi_bus);

    if (gpio_is_valid(drv->bus_data.gpio_din))
        gpio_release(drv->bus_data.gpio_din);

    if (gpio_is_valid(drv->bus_data.gpio_dout))
        gpio_release(drv->bus_data.gpio_dout);

    gpio_release(drv->bus_data.gpio_clk);

    if (drv->flag == 1)
        free(drv);
}


struct spi_bus* gpio_spi_add_bus(struct gpio_spi_bus_data *spi)
{
    assert(spi);

    struct gpio_spi_drv *drv = malloc(sizeof(struct gpio_spi_drv));
    if (!drv)
        panic("%s: malloc space failed\n", __func__);

    drv->flag = 1;
    drv->is_enable = 1;
    drv->bus_data = *spi;

    gpio_spi_init(drv, drv->bus_data.spi_bus_id);

    return &drv->spi_bus;
}

void gpio_spi_init_driver(void)
{
    if (spi_driver[0].is_enable)
        gpio_spi_init(&spi_driver[0], spi_driver[0].bus_data.spi_bus_id);

    if (spi_driver[1].is_enable)
        gpio_spi_init(&spi_driver[1], spi_driver[1].bus_data.spi_bus_id);

    if (spi_driver[2].is_enable)
        gpio_spi_init(&spi_driver[2], spi_driver[2].bus_data.spi_bus_id);
}

