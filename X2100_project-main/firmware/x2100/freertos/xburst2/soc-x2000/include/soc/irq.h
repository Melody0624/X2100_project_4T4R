#ifndef _SOC_IRQ_H_
#define _SOC_IRQ_H_

#include <cpu/irq.h>

enum soc_irq_type {
    IRQ_INTC_0      = IRQ_CPU_END,
    IRQ_INTC_1      = IRQ_CPU_END + 32,

    IRQ_AUDIO       = IRQ_INTC_0 + (0),
    IRQ_OTG         = IRQ_INTC_0 + (1),
    IRQ_RESERVED0_2 = IRQ_INTC_0 + (2),
    IRQ_PDMA        = IRQ_INTC_0 + (3),
    IRQ_PDMAD       = IRQ_INTC_0 + (4),
    IRQ_PDMAM       = IRQ_INTC_0 + (5),
    IRQ_PWM         = IRQ_INTC_0 + (6),
    IRQ_SFC         = IRQ_INTC_0 + (7),
    IRQ_SSI1        = IRQ_INTC_0 + (8),
    IRQ_SSI0        = IRQ_INTC_0 + (9),
    IRQ_MIPI_DSI    = IRQ_INTC_0 + (10),
    IRQ_SADC        = IRQ_INTC_0 + (11),
    IRQ_MIPI_CSI_4  = IRQ_INTC_0 + (12),
    IRQ_GPIO4       = IRQ_INTC_0 + (13),
    IRQ_GPIO3       = IRQ_INTC_0 + (14),
    IRQ_GPIO2       = IRQ_INTC_0 + (15),
    IRQ_GPIO1       = IRQ_INTC_0 + (16),
    IRQ_GPIO0       = IRQ_INTC_0 + (17),
    IRQ_VIC1        = IRQ_INTC_0 + (18),
    IRQ_VIC0        = IRQ_INTC_0 + (19),
    IRQ_ISP1        = IRQ_INTC_0 + (20),
    IRQ_ISP0        = IRQ_INTC_0 + (21),
    IRQ_HASH        = IRQ_INTC_0 + (22),
    IRQ_AES         = IRQ_INTC_0 + (23),
    IRQ_RSA         = IRQ_INTC_0 + (24),
    IRQ_TCU2        = IRQ_INTC_0 + (25),
    IRQ_TCU1        = IRQ_INTC_0 + (26),
    IRQ_TCU0        = IRQ_INTC_0 + (27),
    IRQ_MIPI_CSI2   = IRQ_INTC_0 + (28),
    IRQ_ROTATE      = IRQ_INTC_0 + (29),
    IRQ_CIM         = IRQ_INTC_0 + (30),
    IRQ_LCD         = IRQ_INTC_0 + (31),
    IRQ_RTC         = IRQ_INTC_1 + (0),
    IRQ_SOFT        = IRQ_INTC_1 + (1),
    IRQ_DTRNG       = IRQ_INTC_1 + (2),
    IRQ_SCC         = IRQ_INTC_1 + (3),
    IRQ_MSC1        = IRQ_INTC_1 + (4),
    IRQ_MSC0        = IRQ_INTC_1 + (5),
    IRQ_UART9       = IRQ_INTC_1 + (6),
    IRQ_UART8       = IRQ_INTC_1 + (7),
    IRQ_UART7       = IRQ_INTC_1 + (8),
    IRQ_UART6       = IRQ_INTC_1 + (9),
    IRQ_UART5       = IRQ_INTC_1 + (10),
    IRQ_UART4       = IRQ_INTC_1 + (11),
    IRQ_UART3       = IRQ_INTC_1 + (12),
    IRQ_UART2       = IRQ_INTC_1 + (13),
    IRQ_UART1       = IRQ_INTC_1 + (14),
    IRQ_UART0       = IRQ_INTC_1 + (15),
    IRQ_MSC2        = IRQ_INTC_1 + (16),
    IRQ_HARB2       = IRQ_INTC_1 + (17),
    IRQ_HARB0       = IRQ_INTC_1 + (18),
    IRQ_CPM         = IRQ_INTC_1 + (19),
    IRQ_DDR         = IRQ_INTC_1 + (20),
    IRQ_GMAC1       = IRQ_INTC_1 + (21),
    IRQ_EFUSE       = IRQ_INTC_1 + (22),
    IRQ_GMAC0       = IRQ_INTC_1 + (23),
    IRQ_I2C5        = IRQ_INTC_1 + (24),
    IRQ_I2C4        = IRQ_INTC_1 + (25),
    IRQ_I2C3        = IRQ_INTC_1 + (26),
    IRQ_I2C2        = IRQ_INTC_1 + (27),
    IRQ_I2C1        = IRQ_INTC_1 + (28),
    IRQ_I2C0        = IRQ_INTC_1 + (29),
    IRQ_HELIX       = IRQ_INTC_1 + (30),
    IRQ_FELIX       = IRQ_INTC_1 + (31),

    IRQ_INTC_END = IRQ_CPU_END + 64,

    IRQ_GPIO_START = IRQ_INTC_END,
    IRQ_GPIO_END   = IRQ_INTC_END + 5 * 32,

    IRQ_NUMS = IRQ_GPIO_END,
};

static inline int soc_gpio_to_irq(int gpio)
{
    return gpio >= 0 ? IRQ_GPIO_START + gpio : -1;
}

static inline int soc_irq_to_gpio(int irq)
{
    return irq - IRQ_GPIO_START;
}

#endif /* _SOC_IRQ_H_ */
