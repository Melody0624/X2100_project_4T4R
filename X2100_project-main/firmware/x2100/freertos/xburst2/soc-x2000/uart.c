#include <common.h>
#include <soc/base.h>
#include <driver/uart.h>
#include <driver/dma.h>
#include <driver/cache.h>
#include <driver/hrtimer.h>
#include "uart_baud_data.h"

#define URBR  0x000
#define UTHR  0x000
#define UDLLR 0x000
#define UDLHR 0x004
#define UIER  0x004
#define UIIR  0x008
#define UFCR  0x008
#define ULCR  0x00c
#define UMCR  0x010
#define ULSR  0x014
#define UMSR  0x018
#define USPR  0x01c
#define ISR   0x020
#define UMR   0x024
#define UACR  0x028
#define URCR  0x040
#define UTCR  0x044

#define UIER_RTOIE 4, 4
#define UIER_MSIE  3, 3
#define UIER_RLSIE 2, 2
#define UIER_TDRIE 1, 1
#define UIER_RDRIE 0, 0

#define UIIR_FFMSEL 6, 7
#define UIIR_INID   1, 3
#define UIIR_INPEND 0, 0

#define UFCR_RDTR 6, 7
#define UFCR_UME  4, 4
#define UFCR_DME  3, 3
#define UFCR_TFRT 2, 2
#define UFCR_RFRT 1, 1
#define UFCR_FME  0, 0

#define ULCR_DLAB 7, 7
#define ULCR_SBK  6, 6
#define ULCR_STPAR 5, 5
#define ULCR_PARM 4, 4
#define ULCR_PARE 3, 3
#define ULCR_SBLS 2, 2
#define ULCR_WLS 0, 1

#define UMCR_MDCE 7, 7
#define UMCR_FCM 6, 6
#define UMCR_LOOP 4, 4
#define UMCR_RTS 1, 1

#define ULSR_FIFOE 7, 7
#define ULSR_TEMP 6, 6
#define ULSR_TDRQ 5, 5
#define ULSR_BI 4, 4
#define ULSR_FMER 3, 3
#define ULSR_PARER 2, 2
#define ULSR_OVER 1, 1
#define ULSR_DRY 0, 0

#define UMSR_CTS 4, 4
#define UMSR_CCTS 0, 0

#define UART_FIFO_LEN 64
#define UART_NUMS 10

#include <driver/gpio.h>

struct gpio_func {
    short gpio;
    unsigned short func;
};

struct uart_gpio {
    short is_enable;
    struct gpio_func rx;
    struct gpio_func tx;
    struct gpio_func cts;
    struct gpio_func rts;
    const char rx_name[10];
    const char tx_name[10];
    const char cts_name[10];
    const char rts_name[10];
};

#define UART_GPIO_DEF(id) \
    [id] = { \
        .is_enable = 1, \
        .rx = {CONFIG_SOC_X2000_UART##id##_RX}, \
        .tx = {CONFIG_SOC_X2000_UART##id##_TX}, \
        .cts = {CONFIG_SOC_X2000_UART##id##_CTS}, \
        .rts = {CONFIG_SOC_X2000_UART##id##_RTS}, \
        .rx_name = "uart"#id"_rx", \
        .tx_name = "uart"#id"_tx", \
        .cts_name = "uart"#id"_cts", \
        .rts_name = "uart"#id"_rts", \
    }

struct uart_gpio uartgpio[10] = {
#ifdef CONFIG_SOC_X2000_UART0
    UART_GPIO_DEF(0),
#endif
#ifdef CONFIG_SOC_X2000_UART1
    UART_GPIO_DEF(1),
#endif
#ifdef CONFIG_SOC_X2000_UART2
    UART_GPIO_DEF(2),
#endif
#ifdef CONFIG_SOC_X2000_UART3
    UART_GPIO_DEF(3),
#endif
#ifdef CONFIG_SOC_X2000_UART4
    UART_GPIO_DEF(4),
#endif
#ifdef CONFIG_SOC_X2000_UART5
    UART_GPIO_DEF(5),
#endif
#ifdef CONFIG_SOC_X2000_UART6
    UART_GPIO_DEF(6),
#endif
#ifdef CONFIG_SOC_X2000_UART7
    UART_GPIO_DEF(7),
#endif
#ifdef CONFIG_SOC_X2000_UART8
    UART_GPIO_DEF(8),
#endif
#ifdef CONFIG_SOC_X2000_UART9
    UART_GPIO_DEF(9),
#endif
};

static inline void m_gpio_request(int gpio, unsigned int func, const char *name)
{
    if (gpio == -1)
        return;

    if (gpio == 0)
        panic("%s gpio is not defined!\n", name);

    gpio_request(gpio, name);

    gpio_set_func(gpio, func);
}

static void uart_gpio_request(int id)
{
    struct uart_gpio *uio = &uartgpio[id];

    if (!uio->is_enable)
        panic("uart%d is not enabled!\n", id);

    m_gpio_request(uio->rx.gpio, uio->rx.func, uio->rx_name);
    m_gpio_request(uio->tx.gpio, uio->tx.func, uio->tx_name);
    m_gpio_request(uio->cts.gpio, uio->cts.func, uio->cts_name);
    m_gpio_request(uio->rts.gpio, uio->rts.func, uio->rts_name);
}

static const unsigned long iobase[] = {
    KSEG1ADDR(UART0_IOBASE),
    KSEG1ADDR(UART1_IOBASE),
    KSEG1ADDR(UART2_IOBASE),
    KSEG1ADDR(UART3_IOBASE),
    KSEG1ADDR(UART4_IOBASE),
    KSEG1ADDR(UART5_IOBASE),
    KSEG1ADDR(UART6_IOBASE),
    KSEG1ADDR(UART7_IOBASE),
    KSEG1ADDR(UART8_IOBASE),
    KSEG1ADDR(UART9_IOBASE),
};

#define UART_ADDR(id, reg)    ((volatile unsigned long *)(iobase[id] + reg))

static inline void uart_write_reg(int id, unsigned int reg, int val)
{
    *UART_ADDR(id, reg) = val;
}

static inline unsigned int uart_read_reg(int id, unsigned int reg)
{
    return *UART_ADDR(id, reg);
}

static inline void uart_set_bit(int id, unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(UART_ADDR(id, reg), start, end, val);
}

static inline unsigned int uart_get_bit(int id, unsigned int reg, int start, int end)
{
    return get_bit_field_v(UART_ADDR(id, reg), start, end);
}

static inline unsigned int reg_get_bit(unsigned int reg, int start, int end)
{
    return (reg & bit_field_mask(start, end)) >> start;
}

static struct baudtoregs_t *uart_get_bauddata(u32 baud)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(baudtoregs); i++) {
        if (baudtoregs[i].baud == baud)
            return &baudtoregs[i];
    }

    return NULL;
}

static void uart_init_config(struct uart_config *config)
{
    int id = config->uart_id;

    assert_range(config->stop_bits, 1, 2);
    assert_range(config->data_bits, 5, 8);
    assert_range(config->parity, UART_PARITY_NONE, UART_PARITY_EVEN);
    assert_range(config->follow_contrl, UART_FC_NONE, UART_FC_CTS_RTS);

    struct baudtoregs_t *bauddata = uart_get_bauddata(config->baud_rate);
    if (!bauddata)
        panic("failed to get bauddata for rate: %d\n", config->baud_rate);

    /* 关闭uart */
    uart_set_bit(id, UFCR, UFCR_UME, 0);

    /* 设置波特率 div */
    uart_set_bit(id, ULCR, ULCR_DLAB, 1);
    uart_write_reg(id, UDLHR, bauddata->div >> 8);
    uart_write_reg(id, UDLLR, bauddata->div & 0xff);

    /* 设置波特率 M, AC */
    uart_set_bit(id, ULCR, ULCR_DLAB, 0);
    uart_write_reg(id, UMR, bauddata->umr);
    uart_write_reg(id, UACR, bauddata->uacr);

    /* 设置 parity, data bits, stop bits */
    unsigned long ulcr = 0;
    set_bit_field(&ulcr, ULCR_PARE, config->parity != UART_PARITY_NONE);
    set_bit_field(&ulcr, ULCR_PARM, config->parity == UART_PARITY_EVEN);
    set_bit_field(&ulcr, ULCR_SBLS, config->stop_bits == 2);
    set_bit_field(&ulcr, ULCR_WLS, config->data_bits - 5);

    uart_write_reg(id, ULCR, ulcr);

    /* 设置 loop mode, Modem Control/follow control */
    unsigned long umcr = 0;
    set_bit_field(&umcr, UMCR_MDCE, config->follow_contrl == UART_FC_CTS_RTS);
    set_bit_field(&umcr, UMCR_FCM,  config->follow_contrl == UART_FC_CTS_RTS);
    set_bit_field(&umcr, UMCR_LOOP, config->loop_mode);

    uart_write_reg(id, UMCR, umcr);
}

static void uart_disable(int id)
{
    /* 关闭uart */
    uart_set_bit(id, UFCR, UFCR_UME, 0);
}

static void uart_enable_fifo_mode(struct uart_config *config)
{
    int id = config->uart_id;
    /* 设置中断
     * 程序调用接收时动态开启 接收 timeout, rdy, 接收错误 中断
     */
    unsigned long uier = 0;
    set_bit_field(&uier, UIER_RTOIE, 0);
    set_bit_field(&uier, UIER_RDRIE, 0);
    set_bit_field(&uier, UIER_RLSIE, 1);

    /* 发送时动态开启 uart 发送中断 */
    set_bit_field(&uier, UIER_TDRIE, 0);

    uart_write_reg(id, UIER, uier);

    /* 使能uart, 使能fifo 模式, 清接收/发送fifo, 设置接收fifo 32字节触发 */
    unsigned long ufcr = 0;
    set_bit_field(&ufcr, UFCR_UME, 1);
    set_bit_field(&ufcr, UFCR_FME, 1);
    set_bit_field(&ufcr, UFCR_TFRT, 1);
    set_bit_field(&ufcr, UFCR_RFRT, 1);
    if (config->rx_dma_mode) {
        set_bit_field(&ufcr, UFCR_DME, 1);//开启 DMA 模式
        set_bit_field(&ufcr, UFCR_RDTR, 0);//fifo 1 字节触发
    } else
        set_bit_field(&ufcr, UFCR_RDTR, 3);

    uart_write_reg(id, UFCR, ufcr);
}

static void uart_enable_rx_irq(int id)
{
    unsigned long uier = uart_read_reg(id, UIER);
    set_bit_field(&uier, UIER_RTOIE, 1);
    set_bit_field(&uier, UIER_RDRIE, 1);
    //这里默认使能Line status, 不需要配置
    set_bit_field(&uier, UIER_RLSIE, 1);
    uart_write_reg(id, UIER, uier);
}

static void uart_disable_rx_irq(int id)
{
    unsigned long uier = uart_read_reg(id, UIER);
    set_bit_field(&uier, UIER_RTOIE, 0);
    set_bit_field(&uier, UIER_RDRIE, 0);
    //这里默认使能Line status, 不需要配置
    set_bit_field(&uier, UIER_RLSIE, 0);
    uart_write_reg(id, UIER, uier);
}

static void uart_enable_tx_irq(int id)
{
    uart_set_bit(id, UIER, UIER_TDRIE, 1);
}

static void uart_disable_tx_irq(int id)
{
    uart_set_bit(id, UIER, UIER_TDRIE, 0);
}

static int uart_write_fifo(int id, char *buf, unsigned int len)
{
    int n = UART_FIFO_LEN - uart_read_reg(id, UTCR);

    if (n < len)
        len = n;

    for (int i = 0; i < len; i++)
        uart_write_reg(id, UTHR, (unsigned char)buf[i]);

    return len;
}

static int uart_read_fifo(int id, char *buf, unsigned int len)
{
    int n = uart_read_reg(id, URCR);

    if (n < len)
        len = n;

    for (int i = 0; i < len; i++)
        ((unsigned char *)buf)[i] = uart_read_reg(id, URBR);

    return len;
}

#include <driver/clk.h>
#include <driver/irq.h>
#include <os.h>

struct uart_data {
    u8 is_inited;
    u8 is_irq_inited;
    u8 is_gpio_inited;
    u8 tx_busy;
    u8 rx_busy;
    u8 follow_contrl;
    int baud_rate;
    struct mutex tx_mutex;
    struct mutex rx_mutex;
    struct thread_waiter tx_wait;
    struct thread_waiter rx_wait;
    struct hrtimer tx_timer;
    char *rx_buf;
    unsigned int rx_len;
    char *tx_buf;
    unsigned int tx_len;

    unsigned long dst_old_addr;
    struct dma *rx_dma;
    char *rxdma_buf;
};

static struct uart_data uartdata[UART_NUMS];

static const char *uartstr[] = {
    "uart0",
    "uart1",
    "uart2",
    "uart3",
    "uart4",
    "uart5",
    "uart6",
    "uart7",
    "uart8",
    "uart9",
};

static const int uartirq[] = {
    IRQ_UART0,
    IRQ_UART1,
    IRQ_UART2,
    IRQ_UART3,
    IRQ_UART4,
    IRQ_UART5,
    IRQ_UART6,
    IRQ_UART7,
    IRQ_UART8,
    IRQ_UART9,
};

enum uart_int_type {
    INT_moden_status,
    INT_tx_request,
    INT_rx_ready,
    INT_rx_line_status,
    INT_reserve_0,
    INT_reserve_1,
    INT_rx_timeout,
    INT_reserve_2,
};


static void uart_tx_timer_cb(struct hrtimer *timer)
{
    struct uart_data *uart = container_of(timer, struct uart_data, tx_timer);
    int id = uart - uartdata;
    unsigned long ulsr = uart_read_reg(id, ULSR);
    if (!get_bit_field(&ulsr, ULSR_TEMP)) {
        if (uart->follow_contrl != UART_FC_NONE) {
            unsigned int len = uart_read_reg(id, UTCR) + 1;
            unsigned int time = (len*10*1000*1000)/uart->baud_rate;
            hrtimer_restart(&uart->tx_timer, time > 300 ? time : 300);
            return;
        }
        printf("uart: error: ulsr: %lx utcr:%d\n", ulsr, uart_read_reg(id, UTCR));
    }

    thread_waiter_wakeup(&uart->tx_wait);
}

static void write_uart_data_irq(struct uart_data *uart)
{
    int id = uart - uartdata;

    int ret = uart_write_fifo(id, uart->tx_buf, uart->tx_len);
    uart->tx_buf = uart->tx_buf + ret;
    uart->tx_len = uart->tx_len - ret;
    if (!uart->tx_len) {
        uart_disable_tx_irq(id);
        unsigned int len = uart_read_reg(id, UTCR) + 1;
        unsigned int time = (len*10*1000*1000)/uart->baud_rate;
        hrtimer_start(&uart->tx_timer, time);
    }
}

volatile unsigned char ch_;

static void uart_irq_handler(int irq, void *data)
{
    struct uart_data *uart = data;
    int id = uart - uartdata;
    int int_type;
    int ret;

    unsigned int uiir = uart_read_reg(id, UIIR);

    //没有待处理中断
    if (reg_get_bit(uiir, UIIR_INPEND))
        return;

    int_type = reg_get_bit(uiir, UIIR_INID);

    switch (int_type)
    {
    case INT_tx_request:
        if (!uart->tx_busy)
            panic("why tx not busy: %d\n", id);

        write_uart_data_irq(uart);

        break;

    case INT_rx_line_status:
        printf("ulsr error: %x\n", uart_read_reg(id, ULSR));

        while (uart_get_bit(id, ULSR, ULSR_FIFOE)) {
            // 读接收 fifo，直到没有 fifo error
            ch_ = uart_read_reg(id, URBR);
        }

        while (uart_get_bit(id, ULSR, ULSR_DRY) && !uart_read_reg(id, URCR)) {
            // 这种状态是 fifo 出错了，不能体现fifo len，
            // 但 fifo 里面还有数据
            // 读接收 fifo，直到读空
            ch_ = uart_read_reg(id, URBR);
        }

        break;

    case INT_rx_ready:
    case INT_rx_timeout:
        if (!uart->rx_busy)
            panic("why rx not busy: %d\n", id);

        if (uart->rx_len == 0)
            panic("why rx len is 0: %d\n", id);

        ret = uart_read_fifo(id, uart->rx_buf, uart->rx_len);
        uart->rx_buf += ret;
        uart->rx_len -= ret;

        if (uart->rx_len == 0) {
            uart_disable_rx_irq(id);
            thread_waiter_wakeup(&uart->rx_wait);
        }

        break;

    case INT_moden_status:
        printf("irq moden staus: %x\n", uart_read_reg(id, UMSR));

    default:
        break;
    }

}

static inline void check_irq_requested(int id)
{
    struct uart_data *uart = &uartdata[id];

    if (!uart->is_irq_inited) {
        request_irq(uartirq[id], 0, uart_irq_handler,
                    uartstr[id], &uartdata[id]);
        uart->is_irq_inited = 1;
    }
}

static void uart_check_fifo(int id)
{
    while (uart_get_bit(id, ULSR, ULSR_FIFOE)) {
        // 读接收 fifo，直到没有 fifo error
        ch_ = uart_read_reg(id, URBR);
    }

    while (uart_get_bit(id, ULSR, ULSR_DRY)) {
        // 这种状态是 fifo 出错了，不能体现fifo len，
        // 但 fifo 里面还有数据
        // 读接收 fifo，直到读空
        ch_ = uart_read_reg(id, URBR);
    }
}

static int uart_send_poll_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms)
{
    int ret;
    int id = config->uart_id;
    unsigned int size = len;
    uint64_t now = systick_get_time_us();
    struct uart_data *uart = &uartdata[id];

    assert(!uart->tx_busy);
    uart->tx_busy = 1;

    while (size) {
        if (timeout_ms != OS_TIMEOUT_NOT_LIMIT_MS) {
            if (systick_get_time_us() - now >= (uint64_t)timeout_ms * 1000)
                goto unbusy;
        }
        ret = uart_write_fifo(id, buf, size);
        buf += ret;
        size -= ret;
    }

    while (uart_read_reg(id, UTCR));

unbusy:
    uart->tx_busy = 0;

    return len - size;
}

static int uart_receive_poll_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms)
{
    int ret;
    int id = config->uart_id;
    unsigned int size = len;
    uint64_t now = systick_get_time_us();
    struct uart_data *uart = &uartdata[id];

    assert(!uart->rx_busy);
    uart->rx_busy = 1;

    while (size) {
        if (timeout_ms != OS_TIMEOUT_NOT_LIMIT_MS) {
            if (systick_get_time_us() - now >= (uint64_t)timeout_ms * 1000)
                goto unbusy;
        }
        uart_check_fifo(id);
        ret = uart_read_fifo(id, buf, size);
        buf += ret;
        size -= ret;

        if (!ret)
            thread_yield();
    }

unbusy:
    uart->rx_busy = 0;

    return len - size;
}

static int uart_send_irq_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms)
{
    int ret;
    int id = config->uart_id;
    uint64_t end = timeout_to_systick_us(timeout_ms);
    struct uart_data *uart = &uartdata[id];

    assert(!os_in_handler_mode());

    mutex_lock(&uart->tx_mutex);
    hrtimer_init(&uart->tx_timer, uart_tx_timer_cb);

    thread_waiter_init(&uart->tx_wait);

    check_irq_requested(id);

    assert(uart->is_inited);

    uart->tx_busy = 1;

    /* 写入一部分数据到fifo
     */
    uart->tx_buf = buf;
    uart->tx_len = len;
    write_uart_data_irq(uart);
    if (uart->tx_len)
        uart_enable_tx_irq(id);

    /* 等待传输完成
     */
    ret = thread_waiter_wait_until(&uart->tx_wait, end);
    if (ret < 0) {
        uart_disable_tx_irq(id);
        hrtimer_cancel(&uart->tx_timer);
    }

    uart->tx_busy = 0;

    mutex_unlock(&uart->tx_mutex);

    return len - uart->tx_len;
}

static int uart_receive_irq_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms)
{
    int ret;
    int id = config->uart_id;
    uint64_t end = timeout_to_systick_us(timeout_ms);
    struct uart_data *uart = &uartdata[id];

    assert(!os_in_handler_mode());

    mutex_lock(&uart->rx_mutex);

    thread_waiter_init(&uart->rx_wait);

    check_irq_requested(id);

    assert(uart->is_inited);

    uart->rx_busy = 1;

    ret = uart_read_fifo(id, buf, len);
    uart->rx_buf = buf + ret;
    uart->rx_len = len - ret;

    if (uart->rx_len) {
        /* 开启 rx rdy/timeout 的中断
         */
        uart_enable_rx_irq(id);

        /* 等待传输完成
        */
        ret = thread_waiter_wait_until(&uart->rx_wait, end);
        if (ret < 0)
            uart_disable_rx_irq(id);
        if (uart->rx_len) {
            ret = uart_read_fifo(id, uart->rx_buf, uart->rx_len);
            uart->rx_buf += ret;
            uart->rx_len -= ret;
        }
    }

    uart->rx_busy = 0;

    mutex_unlock(&uart->rx_mutex);

    return len - uart->rx_len;
}

int soc_uart_send_timeout(struct uart_config *config, const char *buf, unsigned int len, int timeout_ms)
{
    if (config->tx_poll_mode)
        return uart_send_poll_timeout(config, (char *)buf, len, timeout_ms);
    else
        return uart_send_irq_timeout(config, (char *)buf, len, timeout_ms);
}

int soc_uart_read_fifosize(struct uart_config *config, int *total_size)
{
    int totalsize;
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];
    int readable_size;

    totalsize = 64;
    readable_size = uart_read_reg(id, URCR);

    if (config->rx_dma_mode) {
        totalsize = config->rxdma_bufsize;

        readable_size = dma_read_dst_addr(uart->rx_dma) - uart->dst_old_addr;
        if (readable_size < 0)
            readable_size = config->rxdma_bufsize + readable_size;
    }

    if (total_size != NULL)
        *total_size = totalsize;

    return readable_size;
}

int uart_receive_dma_timeout(struct uart_config *config, char *dst, unsigned int len, int timeout_ms)
{
    int n, n0;
    unsigned long dst_now_addr;
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];
    unsigned long start_addr = virt_to_phys(uart->rxdma_buf);
    unsigned long end_addr = start_addr + config->rxdma_bufsize;
    unsigned int size = len;
    uint64_t now = systick_get_time_us();

    while (size) {
        if (timeout_ms != OS_TIMEOUT_NOT_LIMIT_MS) {
            if (systick_get_time_us() - now >= (uint64_t)timeout_ms * 1000)
                return len - size;
        }

        do {
            dst_now_addr = dma_read_dst_addr(uart->rx_dma);
            usleep(300);
        } while (dst_now_addr == uart->dst_old_addr);

        if (dst_now_addr > uart->dst_old_addr) {
            n = dst_now_addr - uart->dst_old_addr;
            if (n > size)
                n = size;

            invalidate_dcache_force((unsigned long)KSEG0ADDR(uart->dst_old_addr), n);
            memcpy(dst, (void *)KSEG0ADDR(uart->dst_old_addr), n);
        } else {
            n = config->rxdma_bufsize - (uart->dst_old_addr - dst_now_addr);
            n0 = end_addr - uart->dst_old_addr;
            if (n0 > size) {
                n = size;

                invalidate_dcache_force((unsigned long)KSEG0ADDR(uart->dst_old_addr), n);
                memcpy(dst, (void *)KSEG0ADDR(uart->dst_old_addr), n);
            } else {
                if (n > size)
                    n = size;

                invalidate_dcache_force((unsigned long)KSEG0ADDR(uart->dst_old_addr), n0);
                memcpy(dst, (void *)KSEG0ADDR(uart->dst_old_addr), n0);

                invalidate_dcache_force((unsigned long)KSEG0ADDR(start_addr), n - n0);
                memcpy(dst + n0, (void *)KSEG0ADDR(start_addr), n - n0);
            }

        }

        uart->dst_old_addr = start_addr + (uart->dst_old_addr - start_addr + n) % config->rxdma_bufsize;
        dst += n;
        size -= n;
    }

    return len - size;
}

int soc_uart_receive_timeout(struct uart_config *config, char *buf, unsigned int len, int timeout_ms)
{
    if (config->rx_dma_mode)
        return uart_receive_dma_timeout(config, buf, len, timeout_ms);
    else if (config->rx_poll_mode)
        return uart_receive_poll_timeout(config, buf, len, timeout_ms);
    else
        return uart_receive_irq_timeout(config, buf, len, timeout_ms);
}

static void rdma_cb(void *data)
{
}

int uart_rdma[10] = {
    [0] = DMA_RQ_UART0_RX,
    [1] = DMA_RQ_UART1_RX,
    [2] = DMA_RQ_UART2_RX,
    [3] = DMA_RQ_UART3_RX,
    [4] = DMA_RQ_UART4_RX,
    [5] = DMA_RQ_UART5_RX,
    [6] = DMA_RQ_UART6_RX,
    [7] = DMA_RQ_UART7_RX,
    [8] = DMA_RQ_UART8_RX,
    [9] = DMA_RQ_UART9_RX,
};

static void uart_rdma_request(struct uart_config *config)
{
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];
    enum DMA_request_type dma_request_r;

    dma_request_r = uart_rdma[id];

    uart->rx_dma = dma_request(dma_request_r, rdma_cb, NULL, DMA_bus_8bit, 1);
}

static void uart_rdma_release(struct uart_config *config)
{
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];

    dma_release(uart->rx_dma);
}

static unsigned long uart_get_dma_urbr_addr(int id)
{
    return iobase[id] + URBR;
}

void soc_uart_start(struct uart_config *config)
{
    char *rxdma_buf = NULL;
    unsigned long addr;
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];

    os_enter_critical();

    assert(id < UART_NUMS);
    assert(!uart->is_inited);

    mutex_init(&uart->tx_mutex);
    mutex_init(&uart->rx_mutex);

    uart->baud_rate = config->baud_rate;
    uart->follow_contrl = config->follow_contrl;

    uart_clk_enable(id, 1);

    uart_init_config(config);

    uart_enable_fifo_mode(config);

    if (!uart->is_gpio_inited)
        uart_gpio_request(id);

    uart->is_gpio_inited = 1;
    uart->is_inited = 1;

    if (config->rx_dma_mode) {
        assert(config->rxdma_bufsize >= 256);

        unsigned long uier = uart_read_reg(id, UIER);
        set_bit_field(&uier, UIER_RLSIE, 1);
        uart_write_reg(id, UIER, uier);

        check_irq_requested(id);

        uart_rdma_request(config);

        rxdma_buf = (char *)cache_align_malloc(config->rxdma_bufsize);
        uart->rxdma_buf = rxdma_buf;

        addr = uart_get_dma_urbr_addr(id);
        dma_start_cyclic(uart->rx_dma, (unsigned long*)addr, rxdma_buf, config->rxdma_bufsize, 1);

        uart->dst_old_addr = dma_read_dst_addr(uart->rx_dma);
    }

    os_exit_critical();
}

void soc_uart_stop(struct uart_config *config)
{
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];

    os_enter_critical();

    assert(id < UART_NUMS);
    assert(uart->is_inited);
    assert(!uart->tx_busy);
    assert(!uart->rx_busy);

    if (config->rx_dma_mode) {
        dma_stop(uart->rx_dma);
        free(uart->rxdma_buf);
        uart_rdma_release(config);
    }

    uart_disable(id);
    uart_clk_enable(id, 0);

    if (uart->is_irq_inited) {
        disable_irq(uartirq[id]);
        release_irq(uartirq[id]);
        uart->is_irq_inited = 0;
    }

    uart->is_inited = 0;

    os_exit_critical();
}

void soc_uart_reset_read_wait(struct uart_config *config)
{
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];

    os_enter_critical();

    if (uart->is_inited) {
        /* 禁用 RX IRQ，防止挂起的接收任务使能的中断在清除 rx_busy 后误触发 */
        uart_disable_rx_irq(id);

        /* 唤醒等待 rx_wait 的线程，保证能够执行后续的解锁操作 */
        thread_waiter_wakeup(&uart->rx_wait);

        /* 重置 rx_wait，清除挂起任务在等待队列中的记录 */
        thread_waiter_init(&uart->rx_wait);

        /* 清除接收忙标志，允许后续接收操作重新开始 */
        uart->rx_busy = 0;
    }

    os_exit_critical();

    /* 在临界区外等待 mutex 释放 */
    if (uart->is_inited) {
        uint32_t timeout_ms = 3000; // 等待锁释放超时时间
        uint64_t start_us = systick_get_time_us();

        while (mutex_is_locked(&uart->rx_mutex)) {
            if ((systick_get_time_us() - start_us) / 1000 >= timeout_ms) {
                printf("uart%d: timeout waiting for rx_mutex\n", id);
                break;
            }
            usleep(100);
        }

        os_enter_critical();
        if (!mutex_is_locked(&uart->rx_mutex)) {
            mutex_init(&uart->rx_mutex);
        }
        os_exit_critical();
    }
}

/**
 * @brief 重置 UART 发送等待状态
 *
 * 注意：必须同时禁用 TX IRQ 和取消 TX 定时器，否则挂起的发送线程
 * 使能的中断会在清除 tx_busy 后误触发，导致中断处理函数 panic。
 *
 * @param config UART 配置指针
 */
void soc_uart_reset_write_wait(struct uart_config *config)
{
    int id = config->uart_id;
    struct uart_data *uart = &uartdata[id];

    os_enter_critical();

    if (uart->is_inited) {
        /* 禁用 TX IRQ，防止挂起的发送线程使能的中断在清除 tx_busy 后误触发 */
        uart_disable_tx_irq(id);

        /* 取消 TX 定时器回调 */
        hrtimer_cancel(&uart->tx_timer);

        /* 唤醒等待 tx_wait 的线程，保证能够执行后续的解锁操作 */
        thread_waiter_wakeup(&uart->tx_wait);
        /* 重置 tx_wait，清除挂起任务在等待队列中的记录 */
        thread_waiter_init(&uart->tx_wait);

        /* 清除发送忙标志，允许后续发送操作重新开始 */
        uart->tx_busy = 0;
    }

    os_exit_critical();

        /* 在临界区外等待 mutex 释放 */
    if (uart->is_inited) {
        uint32_t timeout_ms = 3000; // 等待锁释放超时时间
        uint64_t start_us = systick_get_time_us();

        while (mutex_is_locked(&uart->tx_mutex)) {
            if ((systick_get_time_us() - start_us) / 1000 >= timeout_ms) {
                printf("uart%d: timeout waiting for tx_mutex\n", id);
                break;
            }
            usleep(100);
        }

        os_enter_critical();
        if (!mutex_is_locked(&uart->tx_mutex)) {
            mutex_init(&uart->tx_mutex);
        }
        os_exit_critical();
    }
}