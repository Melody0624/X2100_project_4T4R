#ifndef _UART_H_
#define _UART_H_

#include <soc/uart.h>
#include <stdio.h>

enum uart_parity {
    UART_PARITY_NONE,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
};

enum uart_follow_contrl {
    UART_FC_NONE,
    UART_FC_CTS_RTS,    // 硬件流控
    UART_FC_SW_CTS_RTS, // 软件流控
};

struct uart_config {
    unsigned char uart_id;
    unsigned char data_bits;
    unsigned char stop_bits;
    unsigned char loop_mode;
    unsigned char rx_poll_mode;
    unsigned char tx_poll_mode;
    enum uart_parity parity;
    enum uart_follow_contrl follow_contrl;
    unsigned int baud_rate;

    int rxdma_bufsize;
    unsigned char rx_dma_mode;
    void *uart_dma_cycle;
};

void uart_start(struct uart_config *config);

void uart_stop(struct uart_config *config);

void uart_send(struct uart_config *config, const char *buf, unsigned int len);

void uart_receive(struct uart_config *config, char *buf, unsigned int len);

int uart_send_timeout(struct uart_config *config, const char *buf, unsigned int len, int timeout_ms);

int uart_receive_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms);

char uart_get_char(struct uart_config *config);

char uart_get_char_timeout(struct uart_config *config, int timeout_ms);

int uart_read_fifosize(struct uart_config *config, int *total_size);

/**
 * @brief 重置 UART 接收等待状态
 *
 * 在挂起 UART 接收任务后调用，清理接收任务可能持有的内部状态
 * （rx_mutex、rx_wait、rx_busy），防止后续操作因 mutex 被
 * 挂起任务持有而阻塞。
 *
 * 用于处理任务被挂起时正阻塞在 uart_receive_timeout 内部的场景。
 *
 * @param config UART 配置指针
 */
void uart_reset_read_wait(struct uart_config *config);

/**
 * @brief 重置 UART 发送等待状态
 *
 * 在挂起 UART 发送线程后调用，清理发送线程可能持有的内部状态
 * （tx_mutex、tx_wait、tx_busy），防止 OTA 升级等后续操作因
 * mutex 被挂起任务持有而阻塞。
 *
 * 用于处理发送线程被挂起时正阻塞在 uart_send 内部的场景。
 *
 * @param config UART 配置指针
 */
void uart_reset_write_wait(struct uart_config *config);

#if 0
/* uart dma cycle interface */
/* uart dma cycle 模式初始化 */
void uart_dma_cycle_init(struct uart_config *uart_cfg);

/* 获取 uart dma cycle 接收数据个数
 * return : 接收数据个数， 当接收缓冲区溢出时，此结果为负值
 */
int uart_dma_cycle_get_recv_count(struct uart_config *uart_cfg);

/* 获取 uart dma cycle 接收数据
 * return : 读取到的字节数
 */
int uart_dma_cycle_recv(struct uart_config *uart_cfg, uint8_t *data, int size);

/* uart dma cycle 发送数据 */
int uart_dma_cycle_send(struct uart_config *uart_cfg, uint8_t *data, int size);

/* 清空接收缓冲区 */
void uart_dma_cycle_clear_recv(struct uart_config *uart_cfg);

/* 查询 uart dma cycle 发送完成 */
int uart_dma_cycle_send_finish(struct uart_config *uart_cfg);

/* 停止 uart dma cycle, 释放资源 */
void uart_dma_cycle_deinit(struct uart_config *uart_cfg);
#endif

#endif /* _UART_H_ */