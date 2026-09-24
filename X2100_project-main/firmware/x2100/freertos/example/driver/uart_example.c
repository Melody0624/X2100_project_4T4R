#include <stdio.h>
#include <malloc.h>
#include <driver/uart.h>
#include <os.h>
#include <string.h>

static struct uart_config uart0_config = {
    .uart_id            = 1,
    .data_bits          = 8,
    .stop_bits          = 1,
    .loop_mode          = 0,
    .tx_poll_mode       = 1,
    .rx_poll_mode       = 0,
//    .rx_dma_mode      = 1,
//    .rxdma_bufsize      = 1024, // must >= 256
    .parity             = UART_PARITY_NONE,
    .follow_contrl      = UART_FC_NONE, //UART_FC_CTS_RTS / UART_FC_NONE / UART_FC_CTS_RTS
    .baud_rate          = 9600,
};

	char *tx_buffer = NULL;
	char *rx_buffer = NULL;
	int buffer_len = 1024;
	unsigned char base = 128;

static void dump_buffer(void *buffer, int count)
{
    int i = 0;
    unsigned char *tmp = (unsigned char *)buffer;

    for (i=0; i<count; i++) {
        if ( (i != 0) && (i%16 == 0)) {
            printf("\n");
        }
        printf("%02x:", tmp[i]);
    }
    printf("\n");
}

static void uart_read_thread_func(void *data)
{
 
	while(1)
	{
	uart_receive(&uart0_config, rx_buffer, buffer_len);
	if (memcmp(tx_buffer, rx_buffer, buffer_len) == 0) {
        } else {
            printf("uart test failed. Send != Recv\n");
            printf("Send Buffer:\n");
            dump_buffer(tx_buffer, buffer_len);

            printf("Recv Buffer:\n");
            dump_buffer(rx_buffer, buffer_len);
        }
        printf("\n");
	}
}


static void uart_write_thread_func(void *data)
{
	while(1){
		
        memset(rx_buffer, 0x00, buffer_len);
        memset(tx_buffer, --base, buffer_len);
	printf("Send Buffer finish =0x%x\n", tx_buffer[0]);
        uart_send(&uart0_config, tx_buffer, buffer_len);
	if(base == 0)
		base = 128;
        msleep(1000);
	}
}


void uart_test(void)
{
	thread_ptr_t uart_read_t;
	thread_ptr_t uart_write_t;

	printf("UART Configurate Infomation:\n");
	printf("UART%d: %d:%d-%c-%d\n",
	        uart0_config.uart_id,
	        uart0_config.baud_rate,
	        uart0_config.data_bits,
	        (uart0_config.parity == UART_PARITY_NONE) ? 'N' : (uart0_config.parity == UART_PARITY_ODD) ? 'O' : 'E',
	        uart0_config.stop_bits);
	printf("HardwareFlow: %s\n", uart0_config.follow_contrl == UART_FC_NONE ? "No" : "Yes");

    tx_buffer = malloc(buffer_len);
    if (!tx_buffer) {
        printf("uart thread malloc tx buffer failed\n");
        return;
    }

    rx_buffer = malloc(buffer_len);
    if (!rx_buffer) {
        printf("uart thread malloc rx_buffer buffer failed\n");
        free(tx_buffer);
        return;
    }

	uart_start(&uart0_config);
	memset(rx_buffer, 0x00, buffer_len);
	memset(tx_buffer, base, buffer_len);

	uart_read_t = thread_create("uart_read_test", 2048, uart_read_thread_func, NULL);
	uart_write_t = thread_create("uart_write_test", 2048, uart_write_thread_func, NULL);

	thread_join(uart_read_t, NULL);
	thread_join(uart_write_t, NULL);
	free(rx_buffer);
	free(tx_buffer);
	uart_stop(&uart0_config);
}
