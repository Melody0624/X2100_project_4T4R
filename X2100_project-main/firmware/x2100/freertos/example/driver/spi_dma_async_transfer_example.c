#include <common.h>
#include <driver/spi.h>
#include <driver/gpio.h>
#include <driver/cache.h>
#include <os.h>
#include <driver/irq.h>
#include <assert.h>

struct spi_config_data config = {
    .id = 0,
    .cs_pin = GPIO_PA(28),
    .clk_rate = 1 * 100 * 1000,
    .cs_valid_level = Spi_valid_high,
    .tx_endian = Spi_endian_msb_first,
    .rx_endian = Spi_endian_msb_first,
    .bits_per_word = 8,
    .spi_pol = 0,
    .spi_pha = 0,
    .loop_mode = 0,
};

#define SPI_MAX_DMA_SIZE 4096

#define BUTTION_GPIO    GPIO_PC(28)

/* NOTE1:在君正平台下,为了执行cache操作,需要满足以下两个条件：
 * 1、需要保证buf地址 32 字节对齐(使用 __align(32) 对 buf 进行处理)
 * 2、buf的大小也需要 32字节对齐,如: dma_tx_buf[32] 的大小为sizeof(dma_tx_buf)=32,已对齐.
 * 3、对于 x2600 平台，buf 地址 和 buf 大小都需要 64 对齐 */
__align(32) u8 dma_tx_buf1[SPI_MAX_DMA_SIZE];
__align(32) u8 dma_rx_buf1[SPI_MAX_DMA_SIZE];

/* 以下是中断内使用dma传输的例子 */
struct spi_message msg = {
    .tx_buf = dma_tx_buf1,
    .rx_buf = dma_rx_buf1,
    .tlen  = SPI_MAX_DMA_SIZE,
    .rlen  = SPI_MAX_DMA_SIZE,
    .cs_change = 1, /* transfer结束改变 cs 电平 */
    .use_dma = 1,   /* 使用 dma 方式传输 */
};

struct spi_device *spi = NULL;
static thread_waiter_t spi_dma_waiter;

void spi_dma_async_cb(struct spi_config_data *config)
{
    /* NOTE: 本次传输完成才可以开始下一次传输 */
    thread_waiter_wakeup(&spi_dma_waiter);
}

static void button_gpio_irq_handler(int irq, void *data)
{
    spi_dma_async_transfer(spi, &msg);
    printf("irq: %d %d\n", irq, gpio_get_value(irq_to_gpio(irq)));
}

void spi_dma_in_irq_test(void)
{
    int tx_value = 0;
    int gpio_level = 1;

    spi = spi_register(&config);
    spi_dma_start(spi, &msg, spi_dma_async_cb);

    thread_waiter_init(&spi_dma_waiter);

    /* 按下boot按键即可开始传输一次 */
    assert(!gpio_request(BUTTION_GPIO, "button"));
    gpio_set_func(BUTTION_GPIO , (GPIO_INPUT | GPIO_PULL_HIZ));

    request_irq(gpio_to_irq(BUTTION_GPIO), IRQ_TYPE_EDGE_FALLING, button_gpio_irq_handler, "button", NULL);

    while (1) {
        /* 每次传输都必须进行如下 cache 操作(cache操作的条件见 NOTE1): */
        memset(dma_tx_buf1, tx_value++, SPI_MAX_DMA_SIZE);
        memset(dma_rx_buf1, 0, SPI_MAX_DMA_SIZE);
        flush_dcache((unsigned long)dma_tx_buf1, SPI_MAX_DMA_SIZE);
        invalidate_dcache((unsigned long)dma_rx_buf1, SPI_MAX_DMA_SIZE);

        thread_waiter_wait(&spi_dma_waiter);

        invalidate_dcache((unsigned long)dma_rx_buf1, SPI_MAX_DMA_SIZE);
        if (memcmp(dma_tx_buf1, dma_rx_buf1, SPI_MAX_DMA_SIZE))
            printf("err: rx_buf1 = %x, %x\n", dma_rx_buf1[0], dma_rx_buf1[SPI_MAX_DMA_SIZE - 1]);
        else
            printf("data right!\n");
    }

    spi_dma_stop(spi);
    spi_unregister(spi);
}