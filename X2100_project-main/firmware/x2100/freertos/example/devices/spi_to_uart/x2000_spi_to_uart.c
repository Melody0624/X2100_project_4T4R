#include <stdio.h>
#include <os.h>
#include <driver/uart.h>
#include <cpu/cpu.h>
#include <ring_mem.h>

void printf_disable_time_stamp(void);

// static struct uart_config uart0_config = {
//     .uart_id            = 0,
//     .data_bits          = 8,
//     .stop_bits          = 1,
//     .loop_mode          = 0,
//     .tx_poll_mode       = 1,
//     .rx_poll_mode       = 0,
//     .parity             = UART_PARITY_NONE,
//     .follow_contrl      = UART_FC_CTS_RTS, //UART_FC_NONE / UART_FC_CTS_RTS
//     .baud_rate          = 115200,
// };

void uart_set_rts(struct uart_config *config, int rts);

    // uart_start(&uart0_config);

    // while (1) {
    //     uart_set_rts(&uart0_config, 1);
    //     // msleep(1000);
    //     // uart_set_rts(&uart0_config, 0);
    //     // msleep(1000);
    // }

#include <driver/gpio.h>

enum {
    CS,
    DT,
    DR,
    CLK,
};

unsigned int spi_gpio[][2] =  {
// 如果不需要自测,第一列的值可以不用管
//  x2000 自测 master     x2000 slave
    [CS] = {GPIO_PB(30), GPIO_PB(5)},
    [DT] = {GPIO_PB(31), GPIO_PB(7)},
    [DR] = {GPIO_PB(28), GPIO_PB(0)},
    [CLK] = {GPIO_PB(29), GPIO_PB(1)},
};


// unsigned int spi_gpio[][2] =  {
//        //      x1000       x2000
//     [CS] = {GPIO_PA(24),  GPIO_PB(5)},
//     [DT] = {GPIO_PA(23),  GPIO_PB(7)},
//     [DR] = {GPIO_PA(22),  GPIO_PB(0)},
//     [CLK] = {GPIO_PA(25),  GPIO_PB(1)},
// };


enum {
    CMD_WRITE_FIFO,
    CMD_READ_FIFO_CNT,
    CMD_READ_FIFO,
    CMD_WRITE_FIFO_CNT,
    CMD_DETECT,
};

void test_gpio(void)
{
    int i;
 
    char buf[20];
 
    for (i = 0; i < 4; i++) {
        unsigned int *p = spi_gpio[i];
        gpio_direction_input(p[1]);
        gpio_direction_output(p[0], 1);
        if (gpio_get_value(p[1]) != 1)
            panic("%s is not 1\n", gpio_to_str(p[1], buf, 20));
        gpio_direction_output(p[0], 0);
        if (gpio_get_value(p[1]) != 0)
            panic("%s is not 0\n", gpio_to_str(p[1], buf, 20));
    }
}

void m_gpio_init(void)
{
    gpio_direction_output(spi_gpio[CS][0], 1);
    gpio_direction_output(spi_gpio[DT][0], 0);
    gpio_direction_input(spi_gpio[DR][0]);
    gpio_direction_output(spi_gpio[CLK][0], 0);

    gpio_direction_input(spi_gpio[CS][1]);
    gpio_direction_input(spi_gpio[DT][1]);
    gpio_direction_output(spi_gpio[DR][1], 0);
    gpio_direction_input(spi_gpio[CLK][1]);
}

void send_byte(unsigned int v)
{
    int i;

    int spi_dt = spi_gpio[DT][0];
    int spi_clk = spi_gpio[CLK][0];

    for (i = 0; i < 8; i++) {
        int value = !!(v & (1 << (7 - i)));
        gpio_set_value(spi_dt, value);
        udelay(1);
        gpio_set_value(spi_clk, 1);
        udelay(1);
        gpio_set_value(spi_clk, 0);
    }
    udelay(1);
}

int receive_byte(unsigned char *b)
{
    int i;
    int v = 0;

    int spi_dr = spi_gpio[DR][0];
    int spi_clk = spi_gpio[CLK][0];

    for (i = 0; i < 8; i++) {
        gpio_set_value(spi_clk, 1);
        udelay(1);
        gpio_set_value(spi_clk, 0);
        udelay(1);
        if (gpio_get_value(spi_dr))
            v |= 1 << (7 - i);
    }

    *b = v;

    return 0;
}

void set_cs_low(void)
{
    int spi_cs = spi_gpio[CS][0];

    gpio_set_value(spi_cs, 0);
    udelay(1);
}

void set_cs_high(void)
{
    int spi_cs = spi_gpio[CS][0];

    udelay(1);
    gpio_set_value(spi_cs, 1);
    udelay(1);
}

void send_char(unsigned char c)
{
    send_byte(CMD_WRITE_FIFO);
    send_byte(c);
}

void receive_char(unsigned char cmd,  unsigned char *c)
{
    send_byte(cmd);
    receive_byte(c);
}

/*
 * x2000 自测 master 函数,仅用于测试,和功能无关
 */
void master_thread(void *data)
{
    while (1) {
        set_cs_low();
        send_char('c');
        set_cs_high();

        while (1) {
            unsigned char cnt;
            set_cs_low();
            receive_char(CMD_READ_FIFO_CNT, &cnt);
            set_cs_high();
            if (cnt)
                break;
        }

        unsigned char c;
        set_cs_low();
        receive_char(CMD_READ_FIFO, &c);
        set_cs_high();
        printf("recive: %c\n", c);
    }
}

int read_cs(void)
{
    int spi_cs = spi_gpio[CS][1];

    return gpio_get_value(spi_cs);
}

void write_byte(unsigned int v)
{
    int i;

    int spi_dt = spi_gpio[DR][1];
    int spi_clk = spi_gpio[CLK][1];

    for (i = 0; i < 8; i++) {
        while (1) {
            if (read_cs())
                return;
            if (gpio_get_value(spi_clk) == 1)
                break;
        }
        int value = !!(v & (1 << (7 - i)));
        gpio_set_value(spi_dt, value);
        while (1) {
            if (read_cs())
                return;
            if (gpio_get_value(spi_clk) == 0)
                break;
        }
    }
}

int read_byte(unsigned char *b)
{
    int i;
    int v = 0;

    if (read_cs())
        return -1;

    int spi_dr = spi_gpio[DT][1];
    int spi_clk = spi_gpio[CLK][1];

    for (i = 0; i < 8; i++) {
        while (1) {
            if (read_cs())
                return -1;
            if (gpio_get_value(spi_clk) == 1)
                break;
        }
        if (gpio_get_value(spi_dr))
            v |= 1 << (7 - i);
        while (1) {
            if (read_cs())
                return -1;
            if (gpio_get_value(spi_clk) == 0)
                break;
        }
    }

    *b = v;

    return 0;
}

unsigned char tx_buf[255];
DEFINE_RING_MEM(txfifo, tx_buf);

unsigned char rx_buf[255];
DEFINE_RING_MEM(rxfifo, rx_buf);

void uart_console_receive(char *buf, int len);
void uart_console_send(const char *str, int len);

void slave_thread(void *data)
{
    while (1) {
        if (read_cs())
            continue;
     
        unsigned char cmd;
        if (read_byte(&cmd) != 0)
            continue;

        if (cmd == CMD_DETECT) {
            write_byte(0xa5);
        }

        if (cmd == CMD_WRITE_FIFO_CNT) {
            write_byte(ring_mem_writable_size(&txfifo));
        }

        if (cmd == CMD_WRITE_FIFO) {
            unsigned char value;
            if (read_byte(&value) != 0)
                continue;

            ring_mem_write(&txfifo, &value, 1);
        }

        if (cmd == CMD_READ_FIFO_CNT) {
            write_byte(ring_mem_readable_size(&rxfifo));
        }

        if (cmd == CMD_READ_FIFO) {
            unsigned char value = 0;
            ring_mem_read(&rxfifo, &value, 1);
            write_byte(value);
        }

        while (read_cs() == 0);
    }
}

void slave_put_char_thread(void *data)
{
    while (1) {
        unsigned char c;
        if (ring_mem_read(&txfifo, &c, 1))
            uart_console_send((void *)&c, 1);
        udelay(1);
    }
}

void slave_get_char_thread(void *data)
{
    while (1) {
        unsigned char c;
        uart_console_receive((void *)&c, 1);
        ring_mem_write(&rxfifo, &c, 1);
    }
}

/**
 * 不仅仅可以用于x2000,其它拥有对称多核的芯片都可以
 * 原理是一个核跑freertos,一个核跑裸机的gpio spi slave
*/
void x2000_spi_to_uart_init(void)
{
    printf_disable_time_stamp();

    m_gpio_init();

    arch_startup_cpu(1, (unsigned long)slave_thread);

    msleep(10);

    // thread_create("master", 4096, master_thread, NULL);
    thread_create("slave-put-char", 4096, slave_put_char_thread, NULL);
    thread_create("slave-get-char", 4096, slave_get_char_thread, NULL);

    printf("x2000_spi_to_uart_init...\n");
}
