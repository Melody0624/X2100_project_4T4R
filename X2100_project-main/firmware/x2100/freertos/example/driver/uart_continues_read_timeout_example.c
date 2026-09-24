#include <driver/uart.h>
#include <os.h>
#include <stdio.h>
#include <string.h>

static struct uart_config uart_cfg = {
    .uart_id = 1,
    .data_bits = 8,
    .stop_bits = 1,
    .loop_mode = 0,
    .tx_poll_mode = 1,
    .rx_poll_mode = 0,
    //    .rx_dma_mode      = 1,
    //    .rxdma_bufsize      = 1024, // must >= 256
    .parity = UART_PARITY_NONE,
    .follow_contrl =
        UART_FC_NONE, // UART_FC_CTS_RTS / UART_FC_NONE / UART_FC_CTS_RTS
    .baud_rate = 115200,
};

// 5s 未接收到数据则超时
#define RECEIVE_TIMEOUT 5 * 1000

#define RX_BUFFER_PERLINE 1024
static char rx_buffer[RX_BUFFER_PERLINE];

static void dump_buffer(void *buffer, int count)
{
    int i = 0;
    unsigned char *tmp = (unsigned char *)buffer;

    for (i=0; i<count; i++) {
        if ( (i != 0) && (i%16 == 0))
            printf("\n");
        printf("%02x:", tmp[i]);
    }
    printf("\n");
}

/**
 * 将测试的uart的rx口与电脑连接，在电脑串口终端中敲字符，当长时间无数据，将接收到的数据通过printf打印出来。
 */
void uart_timeout_test(void)
{
    int count = 0, ret;
    char ch;

    printf("UART Configurate Infomation:\n");
    printf("UART%d: %d:%d-%c-%d\n", uart_cfg.uart_id, uart_cfg.baud_rate, uart_cfg.data_bits,
        (uart_cfg.parity == UART_PARITY_NONE)  ? 'N' :
        (uart_cfg.parity == UART_PARITY_ODD) ? 'O' : 'E', uart_cfg.stop_bits);
    printf("HardwareFlow: %s\n", uart_cfg.follow_contrl == UART_FC_NONE ? "No" : "Yes");

    uart_start(&uart_cfg);

    while (1) {
        uart_receive(&uart_cfg, &ch, 1);
        rx_buffer[count ++] = ch;
        while (1) {
            ret = uart_receive_timeout(&uart_cfg, &ch, 1, RECEIVE_TIMEOUT);
            if (ret == 0 || count == RX_BUFFER_PERLINE) {
                printf("received:\n");
                dump_buffer(rx_buffer, count);
                count = 0;
                break;
            }
            rx_buffer[count++] = ch;
        }
    }

    uart_stop(&uart_cfg);
}
