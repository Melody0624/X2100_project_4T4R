#include <stdio.h>
#include <stdio.h>
#include <os.h>
#include <driver/uart.h>
#include <cpu/cpu.h>
#include <ring_mem.h>

void printf_disable_time_stamp(void);

#include <driver/gpio.h>

/* 主机打开MASTER宏 */
// #define MASTER
/* 测试模式主从均打开TESTTTT宏 */
// #define TESTTTT

// 主从双方必须保证下述枚举类型一致
enum {
    CMD_ZERO,
    CMD_WRITE_FIFO,
    CMD_READ_FIFO_CNT,
    CMD_READ_FIFO,
    CMD_WRITE_FIFO_CNT,
    CMD_DETECT,
    CMD_TEST,
};

// 支持x1600和x2600的sslv的功能验证
//              spi          to          sslv
//       rtos x1000/x1600          rtos x1600/x2600
// 实现 spi 读取 sslv的uart 获取到的数据并传输回 sslv
// 速度最高测试过为100MHz(基于本协议基础下的测试)
// 使用时需要将 xburst/init.c 内的 shell_init所在行 及 "console thread"所在行屏蔽
// 在sslv IConfigTool内勾选上CONFIG_X1600_SSLV0，CONFIG_RING_MEM，CONFIG_LIB_XFORMAT_PRINTF
// sslv  x1600仅支持pol=1,pha=1模式,x2600支持四种模式
// 使用时确保sslv先启动
// 使用两块开发板做主从测试时，确保共地线数量在两根以上

#ifdef MASTER
#include <driver/spi.h>
#include <driver/spi_bus.h>
struct spi_device *spi;
struct spi_config_data spi_config = {
    .id = 0,
    .cs_pin = GPIO_PD(1),               /* 指定 GPIO 作为 CS 引脚 */
    .clk_rate = 1 * 100 * 1000,         /* 配置时钟频率 */
    .cs_valid_level = Spi_valid_low,    /* 配置spi有效电平为低 */
    .tx_endian = Spi_endian_msb_first,
    .rx_endian = Spi_endian_msb_first,
    .bits_per_word = 8,     /* 数据位宽 8,即传输过程中的最小数据单位为8bit */
    .spi_pha = 1,
    .spi_pol = 1,
    .loop_mode = 0,
};
#else
#include <driver/sslv.h>
struct sslv_device *sslv;
struct sslv_config_data sslv_config = {
    .id = 0,
    .bits_per_word = 8,
    .sslv_pha = 1,
    .sslv_pol = 1,
    .loop_mode = 0,
};
#endif

#ifdef MASTER
void spi_tx_byte(unsigned char cmd)
{
    struct spi_message msg[] = {
        {
            .tx_buf = &cmd,
            .rx_buf = NULL,
            .tlen  = 1,
            .rlen  = 0,
            .cs_change = 1,
        },
    };

    spi_transfer(spi, msg, sizeof(msg)/sizeof(msg[0]));
}
void spi_rx_byte(unsigned char *data)
{
    struct spi_message msg[] = {
        {
            .tx_buf = NULL,
            .rx_buf = data,
            .tlen  = 0,
            .rlen  = 1,
            .cs_change = 1,
        },
    };

    spi_transfer(spi, msg, sizeof(msg)/sizeof(msg[0]));
}
void spi_tr_byte(unsigned char *cmd, unsigned char *data)
{
    struct spi_message msg[] = {
        {
            .tx_buf = cmd,
            .rx_buf = data,
            .tlen  = 1,
            .rlen  = 1,
            .cs_change = 1,
        },
    };

    spi_transfer(spi, msg, sizeof(msg)/sizeof(msg[0]));
}

// return 0失败 return 1成功
unsigned char transfer_check(unsigned char cmd, unsigned char data)
{
    unsigned char count = 0;
    unsigned char buf = 0;

    while (1) {
        count++;
        spi_tr_byte(&data, &buf);
        if (buf == cmd)
            return 1;
        if (count > 100)
            return 0;
    }
}

void master_test(void)
{
    unsigned char buf = 0;
    unsigned char cmd = CMD_TEST;

    while (1) {
        unsigned char data1;
        spi_tx_byte(cmd);
        if (transfer_check(cmd, 0) == 0)
            continue;
        spi_rx_byte(&data1);
        if (data1 != (buf + 1))
            break;
        buf = data1;
    }
    printf("transfer is down!!!!!!   %x\n", buf);
    buf = 0;
    udelay(1000*100);
}

void master_thread(void *arg)
{
    printf("master is init\n");
    unsigned char transmit_flag = 0;

    #ifdef TESTTTT
        while (1) master_test();
    #endif

    while (1) {
        if (transmit_flag == 0) {
            while (1) {
                unsigned char cmd = CMD_WRITE_FIFO;
                spi_tx_byte(cmd);
                if (transfer_check(cmd, 'c'))
                    break;
            }
            transmit_flag = 1;
        }

        while (1) {
            unsigned char cmd = CMD_READ_FIFO_CNT;
            unsigned char data1;
            spi_tx_byte(cmd);
            if (transfer_check(cmd, 0)) {
                spi_rx_byte(&data1);
                if (data1 > 0)
                    break;
            }
            msleep(10);
        }

        while (1) {
            unsigned char cmd = CMD_READ_FIFO;
            unsigned char data1;
            spi_tx_byte(cmd);
            if (transfer_check(cmd, 0)) {
                spi_rx_byte(&data1);
                if (data1 > 0) {
                    printf("%c", data1);
                    while (1) {
                        unsigned char cmd = CMD_WRITE_FIFO;
                        spi_tx_byte(cmd);
                        if (transfer_check(cmd, data1))
                            break;
                    }
                    break;
                }
            }
        }
    }
    spi_unregister(spi);
}
#else
void uart_console_receive(char *buf, int len);
void uart_console_send(const char *str, int len);

unsigned char tx_buf[255];
DEFINE_RING_MEM(txfifo, tx_buf);

unsigned char rx_buf[255];
DEFINE_RING_MEM(rxfifo, rx_buf);

void slave_put_char_thread(void *data)
{
    while (1) {
        unsigned char c;
        if (ring_mem_read(&txfifo, &c, 1)) {
            uart_console_send((void *)&c, 1);
        } else {
            usleep(100);
        }
    }
}

void slave_get_char_thread(void *data)
{
    while (1) {
        unsigned char c;

        uart_console_receive((void *)&c, 1);

        if (c != 0) {
            ring_mem_write(&rxfifo, &c, 1);
        }
    }
}

unsigned char num = 1;
unsigned char flagssssss;
unsigned char rx_avild_num;
void sslv_test(unsigned char cmd)
{
    if (cmd == CMD_TEST) {
        sslv_write_tx_fifo(sslv, cmd);
        sslv_write_tx_fifo(sslv, num++);
        sslv_write_tx_fifo(sslv, 0);
        if (num == 0xff) {
            flagssssss = 1;
        }
        if (num == 1 && flagssssss != 0) {
            printf("return from slave_thread\n");
        }
    }
}

void sslv_protocol(unsigned char cmd)
{
    switch (cmd)
    {
    case CMD_DETECT: {
        sslv_write_tx_fifo(sslv, cmd);
        sslv_write_tx_fifo(sslv, 0xa5);
        sslv_write_tx_fifo(sslv, 0);//清fifo的作用
        break;
    }
    case CMD_WRITE_FIFO_CNT: {
        unsigned data = ring_mem_writable_size(&txfifo);
        sslv_write_tx_fifo(sslv, cmd);
        sslv_write_tx_fifo(sslv, data);
        sslv_write_tx_fifo(sslv, 0);
        break;
    }
    case CMD_WRITE_FIFO: {
        unsigned char data;
        sslv_write_tx_fifo(sslv, cmd);
        sslv_write_tx_fifo(sslv, 0);
        while (!sslv_get_rx_fifo_num(sslv));
        data = sslv_read_rx_fifo(sslv);
        ring_mem_write(&txfifo, &data, 1);
        break;
    }
    case CMD_READ_FIFO_CNT: {
        num = ring_mem_readable_size(&rxfifo);
        sslv_write_tx_fifo(sslv, cmd);
        sslv_write_tx_fifo(sslv, num);
        sslv_write_tx_fifo(sslv, 0);
        break;
    }
    case CMD_READ_FIFO: {
        unsigned char data;
        ring_mem_read(&rxfifo, &data, 1);
        sslv_write_tx_fifo(sslv, cmd);
        sslv_write_tx_fifo(sslv, data);
        sslv_write_tx_fifo(sslv, 0);
        break;
    }
    default:
        break;
    }
}

unsigned char rxdata;
void sslv_rx_intr(struct sslv_device *dev)
{
    unsigned int len = sslv_get_rx_fifo_num(sslv);
    if (len > 0) {
        rxdata = sslv_read_rx_fifo(sslv);
        #ifdef TESTTTT
            sslv_test(rxdata);
        #else
            sslv_protocol(rxdata);
        #endif
    }
}
#endif

void sslv_to_uart_init(void)
{
    printf_disable_time_stamp();

#ifdef MASTER
    spi = spi_register(&spi_config);
    msleep(10);
    thread_create("master", 4096, master_thread, NULL);
#else
    sslv = sslv_register(&sslv_config);
    thread_create("slave-put-char", 4096, slave_put_char_thread, NULL);
    thread_create("slave-get-char", 4096, slave_get_char_thread, NULL);
    msleep(10);
    sslv_start_cb_receive(sslv, 0, sslv_rx_intr);
    msleep(10);
#endif
}