#include <os.h>
#include <stdio.h>
#include <spinlock.h>
#include <driver/uart.h>
#include <driver/uart_console.h>
#include <driver/console.h>

static DEFINE_SPINLOCK(lock);

static struct uart_config config = {
    .uart_id = CONFIG_UART_CONSOLE_UART_ID,
    .data_bits = 8,
    .stop_bits = 1,
    .loop_mode = 0,
    .tx_poll_mode = 1,
#ifdef CONFIG_UART_CONSOLE_RX_MODE_POLL
    .rx_poll_mode = 1,
#else
    .rx_poll_mode = 0,
#endif
    .parity = UART_PARITY_NONE,
    .follow_contrl = UART_FC_NONE,
    .baud_rate = CONFIG_UART_CONSOLE_BAUD_RATE,
};

static volatile int is_disable = 0;

void uart_console_put_str(struct console_device *dev, const char *str, int len)
{
    unsigned long flags;

    if (is_disable)
        return;

    spin_lock_irqsave(&lock, flags);

    uart_send(&config, str, len);

    spin_unlock_irqrestore(&lock, flags);
}

char uart_console_get_char(struct console_device *dev)
{
    while (is_disable)
        usleep(1000);

    return uart_get_char(&config);
}

void uart_console_disconnect_to_console(void)
{
    is_disable = 1;
}

void uart_console_connect_to_console(void)
{
    is_disable = 0;
}

void uart_console_receive(char *buf, int len)
{
    uart_receive(&config, buf, len);
}

int uart_console_receive_timeout(char *buf, int len, int timeout_ms)
{
    return uart_receive_timeout(&config, buf, len, timeout_ms);
}

void uart_console_send(const char *str, int len)
{
    uart_send(&config, str, len);
}

static struct console_device uart_con_device = {
    .con_put_str = uart_console_put_str,
    .con_get_char = uart_console_get_char,
};

void uart_console_init(void)
{
    uart_start(&config);
    console_set_device(&uart_con_device);
}
