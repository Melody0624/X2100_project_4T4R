#ifndef _SPI_BUS_H_
#define _SPI_BUS_H_

#include <list.h>

#include <os.h>
#include <driver/spi.h>

struct spi_bus;

struct spi_bus_ops {
    struct spi_device *(*ops_spi_register)(
        struct spi_bus *spi_bus,
        struct spi_config_data *config);
    void (*ops_spi_unregister)(
        struct spi_bus *spi_bus,
        struct spi_device *dev);
    void (*ops_spi_transfer)(
        struct spi_bus *spi_bus,
        struct spi_device *dev, struct spi_message *msg, int count);
    void (*ops_spi_dma_start)(
        struct spi_bus *spi_bus,
        struct spi_config_data *config, struct spi_message *msg, dma_async_cb dma_cb);
    void (*ops_spi_dma_stop)(
        struct spi_bus *spi_bus,
        struct spi_config_data *config);
    void (*ops_spi_dma_async_transfer)(
        struct spi_bus *bus,
        struct spi_device *dev, struct spi_message *msg);
};

struct spi_bus {
/* public members */
    int spi_bus_id;
    const char *spi_bus_name;
    const struct spi_bus_ops *spi_bus_ops;

/* private members */
    struct mutex lock;
    struct list_head list;
    struct list_head link;
};

int spi_bus_register(
    struct spi_bus *spi_bus,
    int spi_bus_id,
    const char *spi_bus_name,
    const struct spi_bus_ops *spi_bus_ops
);

void spi_bus_unregister(struct spi_bus *spi_bus);

#endif /* _SPI_BUS_H_ */