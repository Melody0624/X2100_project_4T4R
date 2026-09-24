#include <driver/sslv.h>
#include <common.h>
#include <driver/gpio.h>
#include <spinlock.h>
#include <os.h>

static DEFINE_SPINLOCK(lock);

struct sslv_device *soc_sslv_register(struct sslv_config_data *config);
void soc_sslv_unregister(struct sslv_device *sslv);
unsigned int soc_sslv_get_rx_fifo_num(struct sslv_device *sslv);
void soc_sslv_write_tx_fifo(struct sslv_device *sslv, unsigned int data);
unsigned int soc_sslv_read_rx_fifo(struct sslv_device *sslv);
void soc_sslv_transmit(struct sslv_device *sslv, unsigned char *tx_buf, int tx_len);
void soc_sslv_receive(struct sslv_device *sslv, unsigned char *rx_buf, int rx_len);
void soc_sslv_start_cb_receive(struct sslv_device *sslv, unsigned char rx_threshold, void (*cb)(struct sslv_device *dev));
void soc_sslv_stop_cb_receive(struct sslv_device *sslv);

__weak void soc_sslv_init_driver(void)
{

}

struct sslv_device *sslv_register(struct sslv_config_data *config)
{
    struct sslv_device *dev = soc_sslv_register(config);

    assert(dev->status == SSLV_IDLE);

    dev->sslv_id = config->id;
    dev->config = *config;
    dev->status = SSLV_BUSY;
    wake_lock_init(&dev->w_lock, "sslv_wake_lock");
    mutex_init(&dev->lock);

    return dev;
}

void sslv_unregister(struct sslv_device *dev)
{
    assert(dev);
    assert(dev->status == SSLV_BUSY);

    soc_sslv_unregister(dev);
    wake_lock_deinit(&dev->w_lock);
    dev->status = SSLV_IDLE;
}

unsigned int sslv_get_rx_fifo_num(struct sslv_device *dev)
{
    return soc_sslv_get_rx_fifo_num(dev);
}

void sslv_write_tx_fifo(struct sslv_device *dev, unsigned int data)
{
    soc_sslv_write_tx_fifo(dev, data);
}

unsigned int sslv_read_rx_fifo(struct sslv_device *dev)
{
    return soc_sslv_read_rx_fifo(dev);
}

void sslv_transmit(struct sslv_device *dev, unsigned char *tx_buf, int tx_len)
{
    mutex_lock(&dev->lock);

    wake_lock(&dev->w_lock);

    soc_sslv_transmit(dev, tx_buf, tx_len);

    wake_unlock(&dev->w_lock);

    mutex_unlock(&dev->lock);
}

void sslv_receive(struct sslv_device *dev, unsigned char *rx_buf, int rx_len)
{
    unsigned long flags;
    mutex_lock(&dev->lock);

    wake_lock(&dev->w_lock);

    spin_lock_irqsave(&lock, flags);
    assert(!dev->cb_flags);
    dev->rx_flags = 1;
    spin_unlock_irqrestore(&lock, flags);

    soc_sslv_receive(dev, rx_buf, rx_len);

    spin_lock_irqsave(&lock, flags);
    dev->rx_flags = 0;
    spin_unlock_irqrestore(&lock, flags);

    wake_unlock(&dev->w_lock);

    mutex_unlock(&dev->lock);
}

void sslv_start_cb_receive(struct sslv_device *dev, unsigned char rx_threshold, void (*cb)(struct sslv_device *dev))
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);
    assert(!dev->rx_flags && !dev->cb_flags);

    dev->cb_flags = 1;
    soc_sslv_start_cb_receive(dev, rx_threshold, cb);

    spin_unlock_irqrestore(&lock, flags);
}

void sslv_stop_cb_receive(struct sslv_device *dev)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);
    assert(!dev->rx_flags && dev->cb_flags);

    soc_sslv_stop_cb_receive(dev);
    dev->cb_flags = 0;

    spin_unlock_irqrestore(&lock, flags);
}

void sslv_init(void)
{
    soc_sslv_init_driver();
}