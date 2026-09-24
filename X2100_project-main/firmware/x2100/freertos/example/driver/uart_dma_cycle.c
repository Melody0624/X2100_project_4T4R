#include <stdio.h>
#include <malloc.h>
#include <common.h>
#include <driver/uart.h>
#include <os.h>
#include <string.h>

static struct uart_config uart0_config = {
    .uart_id = 0,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = UART_PARITY_NONE,
    .follow_contrl = UART_FC_NONE, // UART_FC_CTS_RTS / UART_FC_NONE / UART_FC_CTS_RTS
    .baud_rate = 115200,
};

static struct uart_config uart1_config = {
    .uart_id = 1,
    .data_bits = 8,
    .stop_bits = 1,
    .parity = UART_PARITY_NONE,
    .follow_contrl = UART_FC_NONE, // UART_FC_CTS_RTS / UART_FC_NONE / UART_FC_CTS_RTS
    .baud_rate = 115200,
};

static int test_check_buffer(uint8_t *st, int size)
{
    uint8_t first = st[0];
    if (size < 2)
    {
        return -1;
    }
    for (int i = 1; i < size; i++)
    {
        uint8_t e = first + i;
        if (st[i] != e)
        {
            printf("real: %x expect: %x first: %x pos: %d size: %d\n", st[i], e, first, i, size);
            for (int j = i - 5 > 0 ? i - 5 : 0; j < i + 10; j++)
            {
                printf("%d: %x\n", j, st[j]);
            }

            return i;
        }
    }
    printf("check buffer success\n");
    return 0;
}

#define BUFFERSIZE (4096)

uint8_t *uart0_rx_buf;
uint8_t *uart0_tx_buf;
uint8_t *uart1_rx_buf;
uint8_t *uart1_tx_buf;

/* 测试1 : 硬件 uart0 ===> uart1 外部短接,
   测试接口 :
   uart_dma_cycle_send
   uart_dma_cycle_get_recv_count
   uart_dma_cycle_recv
   测试说明 : uart0 向 uart1 发送一定量数据, uart1 接收并check数据
*/
void test1()
{

    printf("test send 2048 byte data\n");
    uart_dma_cycle_send(&uart0_config, uart0_tx_buf, 2049);

    do
    {
        int count = uart_dma_cycle_get_recv_count(&uart1_config);
        // printf("count : %d\n", count);
        if (count == 2049)
            break;
    } while (1);

    {
        int count = 0;
        while (count < 2049)
        {
            count += uart_dma_cycle_recv(&uart1_config, &uart1_rx_buf[count], BUFFERSIZE - count);
        }
        int ret = test_check_buffer(uart1_rx_buf, count);
        assert(ret == 0);
    }

    msleep(500);

    uart_dma_cycle_clear_recv(&uart1_config);
    printf("test send 4096 byte data\n");
    uart_dma_cycle_send(&uart0_config, uart0_tx_buf, 4096);
    {
        int count = 0;
        while (count < 4096)
        {
            count += uart_dma_cycle_recv(&uart1_config, &uart1_rx_buf[count], BUFFERSIZE - count);
        }
        int ret = test_check_buffer(uart1_rx_buf, count);
        assert(ret == 0);
    }
    printf("test1 : test ok\n");
}

/* 测试2 : 硬件 uart0 ===> uart1 外部短接,
   测试接口 :
   uart_dma_cycle_send
   uart_dma_cycle_send_finish
   uart_dma_cycle_recv
   测试说明 : uart0 向 uart1 循环发送一定量数据， uart1 接收并check数据
*/
void test2()
{
    printf("len test!\n");
    int i = 2;
    while (i < BUFFERSIZE)
    {
        printf("test: len = %d\n", i);
        uart_dma_cycle_send(&uart0_config, uart0_tx_buf, i);

        while (uart_dma_cycle_send_finish(&uart0_config) != 0)
        {
            msleep(10);
        }

        memset(uart1_rx_buf, 0xff, BUFFERSIZE);
        int count = 0;
        int timeout = 0;
        while (count < i)
        {
            count += uart_dma_cycle_recv(&uart1_config, &uart1_rx_buf[count], BUFFERSIZE - count);
            timeout++;
            if (timeout > 5000000)
            {
                break;
            }
        }
        if (timeout > 5000000)
        {
            printf("timeout count: %d\n", count);
            while (1)
                ;
        }
        int ret = test_check_buffer(uart1_rx_buf, count);
        assert(ret == 0);
        i += 127;
    }
    printf("test2 : len test end!\n");
}

/* 测试3 : 硬件 uart0 ===> uart1 外部短接,
   测试接口 :
   uart_dma_cycle_clear_recv
   uart_dma_cycle_send
   uart_dma_cycle_send_finish
   uart_dma_cycle_recv
   测试说明 : uart0 向 uart1 循环发送一定量数据， 同时uart1 向 uart0 循环发送一定量数据，
    uart0 和 uart1 分别接收和check数据
*/

void test3()
{
    int txPos0 = 0;
    int txPos1 = 0;

    int rxPos0 = 0;
    int rxPos1 = 0;
    int rxLen0 = 0;
    int rxLen1 = 0;

    int runCount = 0;
    printf("cycle test\n");

    uart_dma_cycle_clear_recv(&uart0_config);
    uart_dma_cycle_clear_recv(&uart1_config);

    int count;
    int tx0Len = 0;
    int tx1Len = 0;
    while (1)
    {
        if (uart_dma_cycle_send_finish(&uart0_config) == 0)
        {
            tx0Len = 101;
            if (txPos0 + tx0Len >= BUFFERSIZE)
            {
                tx0Len = BUFFERSIZE - txPos0;
            }
            uart_dma_cycle_send(&uart0_config, &uart0_tx_buf[txPos0], tx0Len);
            txPos0 += tx0Len;
            if (txPos0 >= BUFFERSIZE)
            {
                txPos0 = 0;
            }
        }

        if (uart_dma_cycle_send_finish(&uart1_config) == 0)
        {
            tx1Len = 101;
            if (txPos1 + tx1Len >= BUFFERSIZE)
            {
                tx1Len = BUFFERSIZE - txPos1;
            }
            uart_dma_cycle_send(&uart1_config, &uart1_tx_buf[txPos1], tx1Len);
            txPos1 += tx1Len;
            if (txPos1 >= BUFFERSIZE)
            {
                txPos1 = 0;
            }
        }

        while (uart_dma_cycle_get_recv_count(&uart0_config) != tx1Len)
        {
            // printf("count : %d\n", uart_dma_cycle_get_recv_count(&uart0_config));
        }
        count = uart_dma_cycle_recv(&uart0_config, &uart0_rx_buf[rxLen0], tx0Len - rxLen0);
        rxLen0 += count;
        // printf("rxLen0 = %d\n", rxLen0);

        if (rxLen0 >= tx0Len)
        {
            printf("\n");
            int ret = test_check_buffer(uart0_rx_buf, rxLen0);
            assert(ret == 0);
            rxLen0 = 0;
            rxPos0 = 0;
            runCount++;
            printf("test Count: %d\n", runCount);
        }

        while (uart_dma_cycle_get_recv_count(&uart1_config) != tx1Len)
        {
            // printf("count : %d\n", uart_dma_cycle_get_recv_count(&uart1_config));
        }

        count = uart_dma_cycle_recv(&uart1_config, &uart1_rx_buf[rxLen1], tx1Len - rxLen1);
        rxLen1 += count;

        if (rxLen1 >= tx1Len)
        {
            int ret = test_check_buffer(uart1_rx_buf, rxLen1);
            assert(ret == 0);
            rxLen1 = 0;
            rxPos1 = 0;
            if ((runCount % 10) == 0)
            {
                msleep(10);
            }
        }

        if (runCount > 1000)
        {
            printf("test Finish\n");
            break;
        }
    }
}

int uart_dma_cycle_test(void)
{

    uart0_rx_buf = malloc(BUFFERSIZE);
    uart0_tx_buf = malloc(BUFFERSIZE);

    uart1_rx_buf = malloc(BUFFERSIZE);
    uart1_tx_buf = malloc(BUFFERSIZE);

    uart_dma_cycle_init(&uart0_config);
    uart_dma_cycle_init(&uart1_config);

    for (int i = 0; i < BUFFERSIZE; i++)
    {
        uart0_tx_buf[i] = i & 0xff;
        uart1_tx_buf[i] = i & 0xff;
        uart0_rx_buf[i] = 0xff;
        uart1_rx_buf[i] = 0xff;
    }

    test1();
    test2();
    test3();

    uart_dma_cycle_deinit(&uart0_config);
    uart_dma_cycle_deinit(&uart1_config);

    free(uart0_rx_buf);
    free(uart0_tx_buf);
    free(uart1_rx_buf);
    free(uart1_tx_buf);

    printf("uart dma cycle test end\n");

    while (1)
        msleep(1000);
}