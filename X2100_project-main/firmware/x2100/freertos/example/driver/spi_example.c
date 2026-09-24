#include <common.h>
#include <driver/spi.h>
#include <driver/gpio.h>
#include <os.h>

struct spi_config_data config = {
    .id = 0,
    .cs_pin = GPIO_PC(16),              /* 指定 GPIO 作为 CS 引脚 */
    .clk_rate = 1 * 100 * 1000,         /* 配置时钟频率 */
    .cs_valid_level = Spi_valid_high,   /* 配置spi有效电平为高 */
    .tx_endian = Spi_endian_msb_first,
    .rx_endian = Spi_endian_msb_first,
    .bits_per_word = 8,     /* 数据位宽 8,即传输过程中的最小数据单位为8bit */
    .spi_pha = 0,
    .spi_pol = 0,
    .loop_mode = 0,
};

void spi_test(void *data)
{
    u8 tx_buf1[] = {0x11, 0x22, 0x33 ,0X44, 0x55, 0x66, 0x77 ,0x88};
    u8 rx_buf1[64];

    u8 tx_buf2[] = {0x11, 0x22, 0x33 ,0X44, 0x55, 0x66, 0x77 ,0x88};
    u8 rx_buf2[64];

    int len1 = sizeof(tx_buf1)/sizeof(char);
    int len2 = sizeof(tx_buf2)/sizeof(char);
    struct spi_message msg[] = {
        {
            .tx_buf = tx_buf1,  /* tx_buf可以为空,表示只接收不发送 */
            .rx_buf = rx_buf1,  /* rx_buf可以为空,表示只发送不接收 */
            .tlen  = len1,      /* tlen 可以为 0,表示只接收不发送 */
            .rlen  = len1,      /* rlen 可以为 0,表示只发送不接收 */
            .cs_change = 1,     /* 第一个transfer结束改变 cs 电平, 0 表示传输结束不改变 */
        },
        {
            .tx_buf = tx_buf2,
            .rx_buf = rx_buf2,
            .tlen  = len2,
            .rlen  = len2,
            .cs_change = 0,     /* 最后一次传输结束都将改变 cs 电平 */
        },
    };

    struct spi_device *spi = spi_register(&config);

    spi_transfer(spi, msg, sizeof(msg)/sizeof(msg[0]));

    printf("len1 = %d, rx_buf = %x, %x\n", len1, rx_buf1[0],rx_buf1[len1-1]);
    printf("len2 = %d, rx_buf = %x, %x\n", len2, rx_buf2[0],rx_buf2[len2-1]);

    spi_unregister(spi);
}

void test_main(void)
{
    thread_create("spi_test", 2048, spi_test, NULL);
}