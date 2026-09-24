#ifndef _SOC_DMA_H_
#define _SOC_DMA_H_

enum DMA_request_type {
    DMA_RQ_MEM = 0b001000, /* Auto-request. (external address --> external address) */
    DMA_RQ_UART5_TX = 0b001010, /* UART5 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART5_RX = 0b001011, /* UART5 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART4_TX = 0b001100, /* UART4 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART4_RX = 0b001101, /* UART4 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART3_TX = 0b001110, /* UART3 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART3_RX = 0b001111, /* UART3 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART2_TX = 0b010000, /* UART2 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART2_RX = 0b010001, /* UART2 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART1_TX = 0b010010, /* UART1 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART1_RX = 0b010011, /* UART1 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART0_TX = 0b010100, /* UART0 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART0_RX = 0b010101, /* UART0 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_SSI0_TX = 0b010110, /* SSI0 transmit-fifo-empty transfer request. */
    DMA_RQ_SSI0_RX = 0b010111, /* SSI0 receive-fifo-full transfer request. */
    DMA_RQ_SSI1_TX = 0b011000, /* SSI1 transmit-fifo-empty transfer request. */
    DMA_RQ_SSI1_RX = 0b011001, /* SSI1 receive-fifo-full transfer request. */
    DMA_RQ_I2C0_TX = 0b100100, /* I2C0 transmit-fifo-empty transfer request. */
    DMA_RQ_I2C0_RX = 0b100101, /* I2C0 receive-fifo-full transfer request. */
    DMA_RQ_I2C1_TX = 0b100110, /* I2C1 transmit-fifo-empty transfer request. */
    DMA_RQ_I2C1_RX = 0b100111, /* I2C 1 receive-fifo-full transfer request. */
    DMA_RQ_I2C2_TX = 0b101000, /* I2C 2 transmit-fifo-empty transfer request. */
    DMA_RQ_I2C2_RX = 0b101001, /* I2C 2 receive-fifo-full transfer request. */
    DMA_RQ_I2C3_TX = 0b101010, /* I2C 3 transmit-fifo-empty transfer request. */
    DMA_RQ_I2C3_RX = 0b101011, /* I2C 3 receive-fifo-full transfer request. */
    DMA_RQ_I2C4_TX = 0b101100, /* I2C 4 transmit-fifo-empty transfer request. */
    DMA_RQ_I2C4_RX = 0b101101, /* I2C 4 receive-fifo-full transfer request. */
    DMA_RQ_I2C5_TX = 0b101110, /* I2C 5 transmit-fifo-empty transfer request. */
    DMA_RQ_I2C5_RX = 0b101111, /* I2C 5 receive-fifo-full transfer request. */
    DMA_RQ_UART6_TX = 0b110000, /* UART6 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART6_RX = 0b110001, /* UART6 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART7_TX = 0b110010, /* UART7 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART7_RX = 0b110011, /* UART7 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART8_TX = 0b110100, /* UART8 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART8_RX = 0b110101, /* UART8 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_UART9_TX = 0b110110, /* UART9 transmit-fifo-empty transfer request. (external address --> UTHR) */
    DMA_RQ_UART9_RX = 0b110111, /* UART9 receive-fifo-full transfer request. (URBR --> external address) */
    DMA_RQ_SADC_RX = 0b111000, /* SADC receive request */
};

#endif /* _SOC_DMA_H_ */