#include <common.h>
#include <driver/spi.h>
#include <driver/gpio.h>
#include <driver/cache.h>
#include <os.h>

struct spi_config_data config = {
    .id = 0,
    .cs_pin = GPIO_PC(16),
    .clk_rate = 1 * 100 * 1000,
    .cs_valid_level = Spi_valid_high,
    .tx_endian = Spi_endian_msb_first,
    .rx_endian = Spi_endian_msb_first,
    .bits_per_word = 8,
    .spi_pha = 0,
    .spi_pol = 0,
    .loop_mode = 0,
};

void spi_dma_test(void *data)
{
    /* NOTE1:在君正平台下,为了执行cache操作,需要满足以下两个条件：
     * 1、需要保证buf地址 32 字节对齐(使用 __align(32) 对 buf 进行处理)
     * 2、buf的大小也需要 32字节对齐,如: dma_tx_buf[32] 的大小为sizeof(dma_tx_buf)=32,已对齐.
     * 3、对于 x2600 平台，buf 地址 和 buf 大小都需要 64 对齐 */
    __align(32) u8 dma_tx_buf1[32] = {0x11, 0x22, 0x33 ,0X44, 0x55, 0x66, 0x77 ,0x88};
    __align(32) u8 dma_rx_buf1[32];

    __align(32) u8 dma_tx_buf2[32] = {0x11, 0x22, 0x33 ,0X44, 0x55, 0x66, 0x77 ,0x88};
    __align(32) u8 dma_rx_buf2[32];

    __align(32) u8 dma_tx_buf3[32] = {0x11, 0x22, 0x33 ,0X44, 0x55, 0x66, 0x77 ,0x88};
    __align(32) u8 dma_rx_buf3[32];

    /* len 为 发送和接收数据的个数，在君正平台下,dma传输需要保证发送和接收的个数相等.*/
    int len1 = sizeof(dma_tx_buf1)/sizeof(char);
    int len2 = sizeof(dma_tx_buf2)/sizeof(char);
    int len3 = sizeof(dma_tx_buf3)/sizeof(char);

    /* 以下是一个msg 多个 transfer 传输的例子
     * NOTE2:多个 transfer 的最后一次传输,无论cs_change是否为1,都将改变cs电平.*/
    struct spi_message msg[] = {
        {
            // transfer 1
            .tx_buf = dma_tx_buf1,
            .rx_buf = dma_rx_buf1,
            .tlen  = len1,
            .rlen  = len1,
            .cs_change = 1, /* 第一个transfer结束改变 cs 电平 */
            .use_dma = 1,   /* 使用 dma 方式传输 */
        },
        {
            // transfer 2
            .tx_buf = dma_tx_buf2,
            .rx_buf = dma_rx_buf2,
            .tlen  = len2,
            .rlen  = len2,
            .cs_change = 0, /* 第二个transfer结束不改变 cs 电平 */
            .use_dma = 1,
        },
        {
            // transfer 3
            .tx_buf = dma_tx_buf3,
            .rx_buf = dma_rx_buf3,
            .tlen  = len3,
            .rlen  = len3,
            .cs_change = 0, /* 最后一次传输结束都将改变 cs 电平 */
            .use_dma = 1,
        },
    };

    struct spi_device *spi = spi_register(&config);

    /* 每次传输都必须进行如下 cache 操作(cache操作的条件见 NOTE1): */
    flush_dcache((unsigned long)dma_tx_buf1, sizeof(dma_tx_buf1));
    invalidate_dcache((unsigned long)dma_rx_buf1, sizeof(dma_tx_buf1));

    flush_dcache((unsigned long)dma_tx_buf2, sizeof(dma_tx_buf2));
    invalidate_dcache((unsigned long)dma_rx_buf2, sizeof(dma_tx_buf2));

    flush_dcache((unsigned long)dma_tx_buf3, sizeof(dma_tx_buf3));
    invalidate_dcache((unsigned long)dma_rx_buf3, sizeof(dma_tx_buf3));

    /* SPI 传输 */
    spi_transfer(spi, msg, sizeof(msg)/sizeof(msg[0]));

    printf("rx_buf1 = %x, %x\n", dma_rx_buf1[0],dma_rx_buf1[len1 - 1]);
    printf("rx_buf2 = %x, %x\n", dma_rx_buf2[0],dma_rx_buf2[len2 - 1]);
    printf("rx_buf3 = %x, %x\n", dma_rx_buf3[0],dma_rx_buf3[len3 - 1]);

    spi_unregister(spi);
}
void test_main(void)
{
    thread_create("spi_dma_test", 2048, spi_dma_test, NULL);
}