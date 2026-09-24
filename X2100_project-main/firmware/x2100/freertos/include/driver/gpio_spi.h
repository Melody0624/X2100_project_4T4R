#ifndef _GPIO_SPI_H_
#define _GPIO_SPI_H_

#include <driver/spi_bus.h>

struct gpio_spi_bus_data {
    int spi_bus_id;
    int gpio_dout;
    int gpio_din;
    int gpio_clk;
};

void gpio_spi_init_driver(void);
void gpio_spi_remove_bus(struct spi_bus *spi_bus);
struct spi_bus* gpio_spi_add_bus(struct gpio_spi_bus_data *spi);

#endif