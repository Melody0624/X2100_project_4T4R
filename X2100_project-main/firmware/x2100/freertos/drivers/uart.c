#include <driver/uart.h>
#include <os.h>

/*
 * soc 需要实现
 */
extern void soc_uart_start(struct uart_config *config);
extern void soc_uart_stop(struct uart_config *config);
extern int soc_uart_send_timeout(struct uart_config *config, const char *buf, unsigned int len, int timeout_ms);
extern int soc_uart_receive_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms);
extern int soc_uart_read_fifosize(struct uart_config *config, int *total_size);
extern void soc_uart_reset_read_wait(struct uart_config *config);
extern void soc_uart_reset_write_wait(struct uart_config *config);

void uart_start(struct uart_config *config)
{
    soc_uart_start(config);
}

void uart_stop(struct uart_config *config)
{
    soc_uart_stop(config);
}

void uart_send(struct uart_config *config, const char *buf, unsigned int len)
{
    soc_uart_send_timeout(config, buf, len, OS_TIMEOUT_NOT_LIMIT_MS);
}

void uart_receive(struct uart_config *config, char *buf, unsigned int len)
{
    soc_uart_receive_timeout(config, buf, len, OS_TIMEOUT_NOT_LIMIT_MS);
}

int uart_send_timeout(struct uart_config *config, const char *buf, unsigned int len, int timeout_ms)
{
    return soc_uart_send_timeout(config, buf, len, timeout_ms);
}

int uart_receive_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms)
{
    return soc_uart_receive_timeout(config, buf, len, timeout_ms);
}

char uart_get_char(struct uart_config *config)
{
    return uart_get_char_timeout(config, OS_TIMEOUT_NOT_LIMIT_MS);
}

char uart_get_char_timeout(struct uart_config *config, int timeout_ms)
{
    char ch = 0;

    soc_uart_receive_timeout(config, &ch, 1, timeout_ms);

    return ch;
}

int uart_read_fifosize(struct uart_config *config, int *total_size)
{
    return soc_uart_read_fifosize(config, total_size);
}

void uart_reset_read_wait(struct uart_config *config)
{
    soc_uart_reset_read_wait(config);
}

void uart_reset_write_wait(struct uart_config *config)
{
    soc_uart_reset_write_wait(config);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(uart_start);
EXPORT_SYMBOL(uart_stop);
EXPORT_SYMBOL(uart_send);
EXPORT_SYMBOL(uart_receive);
EXPORT_SYMBOL(uart_get_char);

// EXPORT_SYMBOL(uart_dma_cycle_init);
// EXPORT_SYMBOL(uart_dma_cycle_get_recv_count);
// EXPORT_SYMBOL(uart_dma_cycle_send);
// EXPORT_SYMBOL(uart_dma_cycle_recv);
// EXPORT_SYMBOL(uart_dma_cycle_clear_recv);
// EXPORT_SYMBOL(uart_dma_cycle_send_finish);
// EXPORT_SYMBOL(uart_dma_cycle_deinit);
