#include <common.h>
#include <soc/base.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <assert.h>
#include <os.h>
#include <driver/clk.h>
#include <driver/spi_bus.h>
#include <driver/spi.h>
#include "spi_regs.h"

static const unsigned long iobase[] = {
    KSEG1ADDR(SSI0_IOBASE),
    KSEG1ADDR(SSI1_IOBASE),
};

#define SPI_ADDR(id, reg) ((volatile unsigned long *)(iobase[id] + reg))

static inline void spi_write_reg(int id, unsigned int reg, unsigned int value)
{
    *SPI_ADDR(id, reg) = value;
}

static inline unsigned int spi_read_reg(int id, unsigned int reg)
{
    return *SPI_ADDR(id, reg);
}

static inline void spi_set_bits(int id, unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(SPI_ADDR(id, reg), start, end, val);
}

static inline unsigned int spi_get_bits(int id, unsigned int reg, int start, int end)
{
    return get_bit_field_v(SPI_ADDR(id, reg), start, end);
}

/*获取DMA传输的目标地址*/
unsigned long spi_hal_get_dma_ssidr_addr(int id)
{
    return iobase[id] + SSIDR;
}

/////////////////////////////////////////////////////////////////////////////////////

#define SPI_NUM         2

enum spi_status_type {
    SPI_IDLE,
    SPI_BUSY,
};

struct gpio_func {
    short gpio;
    unsigned short func;
    const char *name;
};

struct jz_spi_pin {
    struct gpio_func spi_pins[3];
};

struct jz_spi_drv {
    int id;
    int irq;
    int status;
    int is_enable;
    int spi_miso;
    int spi_mosi;
    int spi_clk;
    int cs_change;
    struct clk *clk;
    const char *irq_name;
    thread_waiter_t waiter;
    struct spi_bus spi_bus;

    int alter_num;
    struct jz_spi_pin *alter_pin;

    int tlen;
    int rlen;
    int len;
    int total_len;
    const void *tbuff;
    void *rbuff;

    struct dma *t_dma;
    struct dma *r_dma;

    struct spi_config_data * config;
};

static struct clk *clk_cgu;

static int cgu_clk_rate;

#ifdef CONFIG_X2000_SPI0
static struct jz_spi_pin jz_spi0_pin[2] = {
    {
        {
            {GPIO_PB(31), GPIO_FUNC_1, "spi0_clk"},
            {GPIO_PB(29), GPIO_FUNC_1, "spi0_miso"},
            {GPIO_PB(30), GPIO_FUNC_1, "spi0_mosi"},
        },
    },

    {
        {
            {GPIO_PD(8), GPIO_FUNC_1, "spi0_clk"},
            {GPIO_PD(10), GPIO_FUNC_1, "spi0_miso"},
            {GPIO_PD(9), GPIO_FUNC_1, "spi0_mosi"},
        },
    },
};
#endif

#ifdef CONFIG_X2000_SPI1
static struct jz_spi_pin jz_spi1_pin[2] = {
    {
        {
            {GPIO_PC(12), GPIO_FUNC_2, "spi1_clk"},
            {GPIO_PC(10), GPIO_FUNC_2, "spi1_miso"},
            {GPIO_PC(11), GPIO_FUNC_2, "spi1_mosi"},
        },
    },

    {
        {
            {GPIO_PD(17), GPIO_FUNC_2, "spi1_clk"},
            {GPIO_PD(19), GPIO_FUNC_2, "spi1_miso"},
            {GPIO_PD(18), GPIO_FUNC_2, "spi1_mosi"},
        },
    },
};
#endif

struct jz_spi_drv jzspi_dev[2] = {
    {
        #ifdef CONFIG_X2000_SPI0
        .is_enable = 1,
        .id = 0,
        .spi_miso = CONFIG_X2000_SPI0_MISO,
        .spi_mosi = CONFIG_X2000_SPI0_MOSI,
        .spi_clk  = CONFIG_X2000_SPI0_CLK,
        .irq = IRQ_SSI0,
        .irq_name = "SSI0",
        .alter_pin = jz_spi0_pin,
        .alter_num = ARRAY_SIZE(jz_spi0_pin),
        #endif
    },
    {
        #ifdef CONFIG_X2000_SPI1
        .is_enable = 1,
        .id = 1,
        .spi_miso = CONFIG_X2000_SPI1_MISO,
        .spi_mosi = CONFIG_X2000_SPI1_MOSI,
        .spi_clk  = CONFIG_X2000_SPI1_CLK,
        .irq = IRQ_SSI1,
        .irq_name = "SSI1",
        .alter_pin = jz_spi1_pin,
        .alter_num = ARRAY_SIZE(jz_spi1_pin),
        #endif
    }
};

static inline int to_bytes(int bits_per_word)
{
    if (bits_per_word <= 8)
        return 1;
    else if (bits_per_word <= 16)
        return 2;
    else
        return 4;
}

static inline unsigned int check_cs(int id, unsigned int cs_pin)
{
    assert(gpio_is_valid(cs_pin));
    return 0;
}

static void jzspi_hal_read_rx_fifo(int id, int bits_per_word, void *buf, int len)
{
    int i = 0;
    unsigned char *p8 = buf;
    unsigned short *p16 = buf;
    unsigned int *p32 = buf;
    unsigned int bytes_per_word = to_bytes(bits_per_word);

    switch (bytes_per_word)
    {
        case 1:
            for (i = 0; i < len; i++)
                p8[i] = spi_read_reg(id, SSIDR) & 0xff;/* read_fifo */
            break;

        case 2:
            for (i = 0; i < len; i++)
                p16[i] = spi_read_reg(id, SSIDR) & 0xffff;
            break;

        case 4:
            for (i = 0; i < len; i++)
                p32[i] = spi_read_reg(id, SSIDR);
            break;

        default:
            assert(0);
            break;
    }
}


static void jzspi_hal_write_tx_fifo(int id, int bits_per_word, const void *buf, int len)
{
    int i = 0;
    unsigned char *p8 = (unsigned char *)buf;
    unsigned short *p16 = (unsigned short *)buf;
    unsigned int *p32 = (unsigned int *)buf;
    unsigned int bytes_per_word = to_bytes(bits_per_word);

    switch (bytes_per_word)
    {
        case 1:
            for (i = 0; i < len; i++)
                spi_write_reg(id, SSIDR, p8[i]);/* write_fifo */
            break;

        case 2:
            for (i = 0; i < len; i++)
                spi_write_reg(id, SSIDR, p16[i]);
            break;

        case 4:
            for (i = 0; i < len; i++)
                spi_write_reg(id, SSIDR, p32[i]);
            break;

        default:
            assert(0);
            break;
    }
}

static void jzspi_hal_write_tx_fifo_only(int id, unsigned int len, unsigned int val)
{
    int i = 0;

    for (i = 0; i < len; i++)
        spi_write_reg(id, SSIDR, val);/* write_fifo */
}

volatile unsigned int val_xxxx;

static void jzspi_hal_read_rx_fifo_only(int id, int len)
{
    int i = 0;

    for (i = 0; i < len; i++)
        val_xxxx = spi_read_reg(id, SSIDR);/* read_fifo */
}

static int jzspi_get_cgv(int id, int clk_rate)
{
    int cgv;
    int real_set_rate;
    int prev_rate = clk_rate;
    int cgu_clk_rate = clk_get_rate(clk_cgu);

    if (cgu_clk_rate < clk_rate)
        printf("spi set speed invalid, try to set spi speed %d but max speed is %d\n",
            clk_rate, cgu_clk_rate / 2);

    cgv = cgu_clk_rate / 2 / clk_rate - 1;

    if (cgv < 0)
        cgv = 0;
    if (cgv > 255)
        cgv = 255;

    real_set_rate = cgu_clk_rate / 2 / (cgv + 1);
    if (real_set_rate != clk_rate)
        prev_rate = cgu_clk_rate / 2 / (cgv + 2);

    if ((clk_rate - prev_rate) < (real_set_rate - clk_rate))
        cgv = cgu_clk_rate / 2 / prev_rate - 1;

    if (cgv < 0)
        cgv = 0;
    if (cgv > 255)
        cgv = 255;

    real_set_rate = cgu_clk_rate / 2 / (cgv + 1);

    /* 实际输出频率与设置的频率超过10%的情况才会打印 */
    int diff = real_set_rate > clk_rate ? real_set_rate - clk_rate : -1 * (real_set_rate - clk_rate);
    if (diff * 100 / clk_rate > 10)
        printf("SPI%d: user set freq: %d, the real freq: %d\n", id, clk_rate, real_set_rate);

    return cgv;
}

static void jzspi_hal_enable_transfer(struct spi_config_data *config)
{
    int cgv;
    int id = config->id;

    assert_range(config->bits_per_word, 2, 32);

    struct jz_spi_drv * drv = &jzspi_dev[id];
    drv->config = config;

    unsigned long ssicr0 = spi_read_reg(id, SSICR0);/* read_ssicr0 */
    unsigned long ssicr1 = spi_read_reg(id, SSICR1);/* read_ssicr1 */

    set_bit_field(&ssicr0, SSICR0_SSIE, 0);/* disable_spi */
    spi_write_reg(id, SSICR0, ssicr0);/* write_ssicr0 */

    set_bit_field(&ssicr0, SSICR0_TFIE, 1);/* enable_transmit_overrun_interrupt */
    set_bit_field(&ssicr0, SSICR0_RFIE, 1);/* enable_receive_underrun_interrupt */
    set_bit_field(&ssicr0, SSICR0_TIE, 0);/* disable_transmit_interrupt */
    set_bit_field(&ssicr0, SSICR0_TEIE, 1);/* enable_transmit_error_interrupt */
    set_bit_field(&ssicr0, SSICR0_RIE, 0);/* disable_receive_interrupt */
    set_bit_field(&ssicr0, SSICR0_REIE, 1);/* enable_receive_error_interrupt */

    /* set_transfer_endian */
    set_bit_field(&ssicr0, SSICR0_TENDIAN, config->tx_endian);
    set_bit_field(&ssicr0, SSICR0_RENDIAN, config->rx_endian);
    /* set_loop_mode */
    set_bit_field(&ssicr0, SSICR0_LOOP, config->loop_mode);

    unsigned int cs = check_cs(id, config->cs_pin);
    /* select_frame_valid_level */
    set_bit_field(&ssicr0, SSICR0_FSEL, cs);
    if (cs == 0)
        set_bit_field(&ssicr1, SSICR1_FRMHL0, config->cs_valid_level);
    else if (cs == 1)
        set_bit_field(&ssicr1, SSICR1_FRMHL1, config->cs_valid_level);

    set_bit_field(&ssicr1, SSICR1_PHA, config->spi_pha);/* set_clk_pha */
    set_bit_field(&ssicr1, SSICR1_POL, config->spi_pol);/* set_clk_pol */

    set_bit_field(&ssicr0, SSICR0_TFLUSH, 1);/* clear_transmit_fifo */
    set_bit_field(&ssicr0, SSICR0_RFLUSH, 1);/* clear_receive_fifo */

    set_bit_field(&ssicr1, SSICR1_TTRG, 0);/* set_transmit_threshold */
    set_bit_field(&ssicr1, SSICR1_RTRG, 0);/* set_receive_threshold */

    /* configure SSICR1_UNFIN before SSICR0_SSIE is enabled */
    set_bit_field(&ssicr1, SSICR1_UNFIN, 0);/* set_transmit_fifo_empty_transfer_finish */

    set_bit_field(&ssicr1, SSICR1_FLEN, config->bits_per_word - 2);/* set_data_unit_bits_len */
    spi_write_reg(id, SSICR1, ssicr1);/* write_ssicr1 */

    if (!config->clk_rate)
        config->clk_rate = 1 * 1000 * 1000;

    if (cgu_clk_rate < config->clk_rate)
        printf("spi set speed invalid , try to set spi speed %d but max speed is %d \n" ,
        config->clk_rate, cgu_clk_rate / 2);

    cgv = jzspi_get_cgv(id, config->clk_rate);

    spi_set_bits(id, SSIGR, SSIGR_CGV, cgv);/* set_clock_dividing_number */

    set_bit_field(&ssicr0, SSICR0_SSIE, 1);/* enable_spi */
    spi_write_reg(id, SSICR0, ssicr0);/* write_ssicr0 */
}

#include <driver/dma.h>
#include <malloc.h>
#include <spinlock.h>
#include <driver/cache.h>

#define MAX_FIFO_LEN 128
#define DMA_MAX_BURST 64

static DEFINE_SPINLOCK(lock);

static void jzspi_write_fifo(int id, int bits_per_word, int len, const void *buf, int tlen, int n)
{
    if(len < n) {
        n = len;
    }

    if (tlen > 0) {
        if (tlen > n) {
            jzspi_hal_write_tx_fifo(id, bits_per_word, buf, n);
        } else {
            jzspi_hal_write_tx_fifo(id, bits_per_word, buf, tlen);
            jzspi_hal_write_tx_fifo_only(id, n - tlen, 0x0);
        }

    } else {
        jzspi_hal_write_tx_fifo_only(id, n, 0x0);
    }
}

static void jzspi_read_fifo(int id, int bits_per_word, void *buf, int rlen, int n)
{
    if (rlen > 0) {
        if (rlen < n) {
            jzspi_hal_read_rx_fifo(id, bits_per_word, buf, rlen);
            jzspi_hal_read_rx_fifo_only(id, n - rlen);
        } else {
            jzspi_hal_read_rx_fifo(id, bits_per_word, buf, n);
        }

    } else {
        jzspi_hal_read_rx_fifo_only(id, n);
    }
}


static inline void set_status(int id, enum spi_status_type status)
{
    if (status == SPI_BUSY) {
        assert(!jzspi_dev[id].status);
        jzspi_dev[id].status = SPI_BUSY;
    } else {
        jzspi_dev[id].status = SPI_IDLE;
    }
}

static void spi_intr_handler(int irq, void *dev)
{
    int len, n;
    int id = (int)dev;
    struct jz_spi_drv * drv = &jzspi_dev[id];

    int bytes_per_word = to_bytes(drv->config->bits_per_word);

    unsigned long ssisr = spi_read_reg(id, SSISR);/* read_ssisr */
    /* get_transmit_fifo_overrun_flag */
    if (get_bit_field(&ssisr, SSISR_TOVER)) {
        set_bit_field(&ssisr, SSISR_TOVER, 0);/* clear_transmit_fifo_overrun_flag */
        spi_write_reg(id, SSISR, ssisr);/* write_ssisr */
        printf("SPI%d transmit overrun\n", id);
    }

    /* get_receive_fifo_underrun_flag */
    if (get_bit_field(&ssisr, SSISR_RUNDR)) {
        set_bit_field(&ssisr, SSISR_RUNDR, 0);/* clear_receive_fifo_underrun_flag */
        spi_write_reg(id, SSISR, ssisr);/* write_ssisr */
        printf("SPI%d receive underrun\n", id);
    }

    /* get_transmit_fifo_underrun_flag */
    if (get_bit_field(&ssisr, SSISR_TUNDR)) {
        set_bit_field(&ssisr, SSISR_TUNDR, 0);/* clear_transmit_fifo_underrun_flag */
        spi_write_reg(id, SSISR, ssisr);/* write_ssisr */
        printf("SPI%d transmit underrun\n", id);
    }

    /* get_receive_fifo_overrun_flag */
    if (get_bit_field(&ssisr, SSISR_ROVER)) {
        set_bit_field(&ssisr, SSISR_ROVER, 0);/* clear_receive_fifo_overrun_flag */
        spi_write_reg(id, SSISR, ssisr);/* write_ssisr */
        printf("SPI%d receive overrun\n", id);
    }

    /* get_receive_fifo_more_threshold_flag && get_receive_interrupt_control_value */
    if (spi_get_bits(id, SSISR, SSISR_RFHF) && spi_get_bits(id, SSICR0, SSICR0_RIE)) {
        if (drv->len > 0) {
            len = spi_get_bits(id, SSISR, SSISR_RFIFO_NUM);/* get_receive_fifo_number */
            jzspi_read_fifo(id, drv->config->bits_per_word, drv->rbuff, drv->rlen, len);
            drv->rlen -= len;
            drv->total_len -= len;
            drv->rbuff += (len * bytes_per_word);

            jzspi_write_fifo(id, drv->config->bits_per_word, drv->len, drv->tbuff, drv->tlen, len);
            drv->tlen -= len;
            drv->tbuff += (len * bytes_per_word);
            drv->len -= len;

        } else {
            len = spi_get_bits(id, SSISR, SSISR_RFIFO_NUM);/* get_receive_fifo_number */

            jzspi_read_fifo(id, drv->config->bits_per_word, drv->rbuff, drv->rlen, len);
            drv->rlen -= len;
            drv->total_len -= len;
            drv->rbuff += (len * bytes_per_word);

            /* 判断传输完成，设置状态，若传输完成则调用 回调函数 */
            if (drv->total_len <= 0) {
                spi_set_bits(id, SSICR0, SSICR0_RIE, 0);/* disable_receive_interrupt */
                thread_waiter_wakeup(&drv->waiter);
                return ;
            }
        }

        if (drv->total_len < MAX_FIFO_LEN)
            n = drv->total_len;
        else
            n = MAX_FIFO_LEN * 3 / 4;

        /* 根据 len 大小设置 阈值 */
        spi_set_bits(id, SSICR1, SSICR1_RTRG, n / 8);/* set_receive_threshold */
    }
}

static void spi_write_read_inr(struct spi_config_data *config,
    void * tx_buf, int tlen, void * rx_buf, int rlen)
{
    int len, n;
    int id = config->id;
    int bytes_per_word = to_bytes(config->bits_per_word);

    if (tx_buf == NULL)
        tlen = 0;
    if (rx_buf == NULL)
        rlen = 0;

    len = tlen > rlen ? tlen : rlen;
    if (!len)
        return;

    jzspi_dev[id].config = config;
    jzspi_dev[id].tbuff  = tx_buf;
    jzspi_dev[id].rbuff  = rx_buf;
    jzspi_dev[id].tlen   = tlen;
    jzspi_dev[id].rlen   = rlen;
    jzspi_dev[id].len    = len;
    jzspi_dev[id].total_len = len;

    if (len < MAX_FIFO_LEN)
        n = len;
    else
        n = MAX_FIFO_LEN * 3 / 4;

    /* set_transmit_threshold & set_receive_threshold */
    unsigned long ssicr1 = spi_read_reg(id, SSICR1);/* read_ssicr1 */
    set_bit_field(&ssicr1, SSICR1_TTRG, 0);
    set_bit_field(&ssicr1, SSICR1_RTRG, n / 8);
    spi_write_reg(id, SSICR1, ssicr1);/* write_ssicr1 */

    /* 第一次 写FIFO 写满 */
    jzspi_write_fifo(id, jzspi_dev[id].config->bits_per_word, len, jzspi_dev[id].tbuff, jzspi_dev[id].tlen, MAX_FIFO_LEN);
    jzspi_dev[id].tlen -= MAX_FIFO_LEN;
    jzspi_dev[id].tbuff += (MAX_FIFO_LEN * bytes_per_word);
    jzspi_dev[id].len -= MAX_FIFO_LEN;
    spi_set_bits(id, SSICR0, SSICR0_RIE, 1);/* enable_receive_interrupt */
}

static void spi_write_read_poll(struct spi_config_data *config,
    void * tx_buf, int tlen, void * rx_buf, int rlen)
{
    int len;
    int id = config->id;

    int bytes_per_word = to_bytes(config->bits_per_word);
    int n = MAX_FIFO_LEN * 3 / 4 ;

    if (tx_buf == NULL)
        tlen = 0;
    if (rx_buf == NULL)
        rlen = 0;

    len = tlen > rlen ? tlen : rlen;
    if (!len)
        return;

    /* 第一次 写FIFO 写满 */
    jzspi_write_fifo(id, config->bits_per_word, len, tx_buf, tlen, MAX_FIFO_LEN);
    tx_buf += MAX_FIFO_LEN * bytes_per_word;
    tlen -= MAX_FIFO_LEN;
    len -= MAX_FIFO_LEN;

    /*
     * 循环读写FIFO 每次读写 FIFO 的 3 / 4
    */
    while (len > 0) {
        while (spi_get_bits(id, SSISR, SSISR_RFIFO_NUM) < n);/* get_receive_fifo_number */
        jzspi_read_fifo(id, config->bits_per_word, rx_buf, rlen, n);
        rx_buf += n  * bytes_per_word;
        rlen -= n;

        jzspi_write_fifo(id, config->bits_per_word, len, tx_buf, tlen, n);
        tx_buf += n * bytes_per_word;
        tlen -= n;
        len -= n;

    }

    /* 最后一次 把FIFO读空 */
    while (spi_get_bits(id, SSISR, SSISR_RFIFO_NUM) < rlen);/* get_receive_fifo_number */
    jzspi_hal_read_rx_fifo(id, config->bits_per_word, rx_buf, rlen);
}

static void tdma_cb(void *data){}

static void rdma_cb(void *data)
{
    thread_waiter_t *waiter = data;
    thread_waiter_wakeup(waiter);
}

static void spi_dma_request(struct spi_config_data *config)
{
    int id = config->id;
    enum DMA_request_type dma_request_t;
    enum DMA_request_type dma_request_r;

    int bytes_per_word = to_bytes(config->bits_per_word);

    if (id) {
        dma_request_t = DMA_RQ_SSI1_TX;
        dma_request_r = DMA_RQ_SSI1_RX;
    } else {
        dma_request_t = DMA_RQ_SSI0_TX;
        dma_request_r = DMA_RQ_SSI0_RX;
    }
    jzspi_dev[id].t_dma = dma_request(dma_request_t, tdma_cb,
                        config, bytes_per_word, config->dma_unit);
    jzspi_dev[id].r_dma = dma_request(dma_request_r, rdma_cb,
                        &jzspi_dev[id].waiter, bytes_per_word, config->dma_unit);
}

static void spi_dma_release(struct spi_config_data *config)
{
    int id = config->id;
    dma_release(jzspi_dev[id].r_dma);
    dma_release(jzspi_dev[id].t_dma);
}

static void spi_write_read_dma(struct spi_config_data *config,
    void * tx_buf, int tlen, void * rx_buf, int rlen)
{
    int id = config->id;
    unsigned long addr[SPI_NUM];
    int bytes_per_word = to_bytes(config->bits_per_word);
    int n = MAX_FIFO_LEN - (config->dma_unit / bytes_per_word);
    addr[id] = spi_hal_get_dma_ssidr_addr(id);
    int tlen_total = tlen;
    assert(tlen == rlen);

    if (tlen == 0)
        return;

    /* set_transmit_threshold & set_receive_threshold */
    unsigned long ssicr1 = spi_read_reg(id, SSICR1);/* read_ssicr1 */
    set_bit_field(&ssicr1, SSICR1_TTRG, n / 8);
    set_bit_field(&ssicr1, SSICR1_RTRG, (config->dma_unit / bytes_per_word) / 8);
    spi_write_reg(id, SSICR1, ssicr1);/* write_ssicr1 */

    /* 发送速率小于10Mhz或发送长度少于MAX_FIFO_LEN时提前写发送数据 以节省时间 */
    if (tlen <= MAX_FIFO_LEN) {
        jzspi_hal_write_tx_fifo(id, config->bits_per_word, tx_buf, tlen);
    } else if (config->clk_rate <= 10000000) {
        jzspi_hal_write_tx_fifo(id, config->bits_per_word, tx_buf, DMA_MAX_BURST);
        tlen -= DMA_MAX_BURST;
        tx_buf += DMA_MAX_BURST * bytes_per_word;
    }

    dma_start(jzspi_dev[id].r_dma, (unsigned long *)addr[id], rx_buf, rlen * bytes_per_word);

    if (tlen_total > MAX_FIFO_LEN)
        dma_start(jzspi_dev[id].t_dma, tx_buf, (unsigned long *)addr[id], tlen * bytes_per_word);
}

static inline void gpio_init(int gpio, enum gpio_function func, const char *name)
{
    assert(!gpio_request(gpio, name));
    gpio_set_func(gpio , func);
}

static void m_gpio_request(int gpio, struct jz_spi_drv *drv, int id)
{
    int i;
    char buf[10];
    int num = drv->alter_num;
    struct jz_spi_pin *spi_pin_alter = drv->alter_pin;
    struct gpio_func *spi_pin;

    if (gpio < 0)
        return;

    assert(num >= 1);
    for (i = 0; i < num; i++) {
        spi_pin = spi_pin_alter[i].spi_pins;
        if (gpio == spi_pin[id].gpio) {
            gpio_init(gpio, spi_pin[id].func, spi_pin[id].name);
            return;
        }
    }

    panic("SPI %s(%s) is invaild!\n", spi_pin[id].name, gpio_to_str(gpio, buf, sizeof(buf)));
}

static void spi_gpio_request(struct jz_spi_drv *drv)
{
    int id = drv->id;

    if (drv->spi_clk < 0)
        panic("SPI%d spi_clk no be set.\n", id);

    m_gpio_request(drv->spi_clk, drv, 0);

    m_gpio_request(drv->spi_miso, drv, 1);

    m_gpio_request(drv->spi_mosi, drv, 2);

}

static void jz_spi_init_setting(int id)
{
    unsigned long ssicr0 = spi_read_reg(id, SSICR0);/* read_ssicr0 */

    set_bit_field(&ssicr0, SSICR0_SSIE, 0);/* disable_spi */
    spi_write_reg(id, SSICR0, ssicr0);/* write_ssicr0 */

    set_bit_field(&ssicr0, SSICR0_EACLRUN, 1);/* set_auto_clear_transmit_fifo_empty_flag */
    set_bit_field(&ssicr0, SSICR0_TFLUSH, 1);/* clear_transmit_fifo */
    set_bit_field(&ssicr0, SSICR0_RFLUSH, 1);/* clear_receive_fifo */
    set_bit_field(&ssicr0, SSICR0_TIE, 0);/* disable_transmit_interrupt */

    set_bit_field(&ssicr0, SSICR0_RFINE, 0);/* disable_receive_finish_control */
    set_bit_field(&ssicr0, SSICR0_RFINC, 0);/* set_receive_continue */
    set_bit_field(&ssicr0, SSICR0_VRCNT, 0);/* receive_counter_unvalid */
    set_bit_field(&ssicr0, SSICR0_TFMODE, 0);/* set_transmit_fifo_empty_mode */

    set_bit_field(&ssicr0, SSICR0_SSIE, 1);/* enable_spi */
    spi_write_reg(id, SSICR0, ssicr0);/* write_ssicr0 */

    /* 设置 TTRG RTRG */
    unsigned long ssicr1 = spi_read_reg(id, SSICR1);/* read_ssicr1 */
    set_bit_field(&ssicr1, SSICR1_TTRG, 0);/* set_transmit_threshold */
    set_bit_field(&ssicr1, SSICR1_RTRG, 0);/* set_receive_threshold */

    set_bit_field(&ssicr1, SSICR1_TFVCK, 0);/* set_start_delay_clk */
    set_bit_field(&ssicr1, SSICR1_TCKFI, 0);/* set_stop_delay_clk */

    /* standard spi format */
    set_bit_field(&ssicr1, SSICR1_FMAT, 0);/* set_standard_transfer_format */
    spi_write_reg(id, SSICR1, ssicr1);/* write_ssicr1 */

    /* disable interval mode */
    spi_set_bits(id, SSIITR, SSIITR_IVLTM, 0);/* set_interval_time */
}

static inline void chip_select(struct spi_config_data *config, int chipselect)
{
    int pin = config->cs_pin;

    if (!gpio_is_valid(pin))
        return;

    if (chipselect) {
        gpio_set_value(pin, config->cs_valid_level);
    } else {
        gpio_set_value(pin, !config->cs_valid_level);
    }
}

static void jz_spi_enable(struct spi_config_data *config)
{
    int id = config->id;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);
    set_status(id, SPI_BUSY);
    spin_unlock_irqrestore(&lock, flags);

    clk_enable(jzspi_dev[id].clk);
    jzspi_hal_enable_transfer(config);
}

static void jz_spi_disable(struct spi_config_data *config)
{
    int id = config->id;
    unsigned long flags;

    spi_set_bits(id, SSICR0, SSICR0_SSIE, 0);/* disable_spi */
    clk_disable(jzspi_dev[id].clk);

    spin_lock_irqsave(&lock, flags);
    set_status(id, SPI_IDLE);
    spin_unlock_irqrestore(&lock, flags);
}

static void jz_spi_transfer_poll(struct spi_device *spi, struct spi_message *msg, int count)
{
    struct spi_config_data *config = &(spi->config);
    int id = config->id;
    int i;

    jz_spi_enable(config);
    chip_select(config, 1);
    for (i = 0; i < count; i++) {
        struct spi_message *m = msg + i;
        jzspi_dev[id].cs_change = m->cs_change;
        spi_write_read_poll(config, (void *)m->tx_buf, m->tlen, m->rx_buf, m->rlen);
        if (i < count - 1) {
            if (m->cs_change) {
                chip_select(config, 0);
                chip_select(config, 1);
            }
        }
    }
    chip_select(config, 0);
    jz_spi_disable(config);
}

static inline int is_use_dma(struct spi_config_data *config, struct spi_message *msg, int count)
{
    int i;
    for (i = 0; i < count; i++) {
        struct spi_message *m = msg + i;
        if (m->use_dma) {
            switch (m->use_dma) {
                case 1:
                    config->dma_unit = to_bytes(config->bits_per_word);
                    break;
                case 2:
                    config->dma_unit = 16;
                    break;
                case 3:
                    config->dma_unit = 32;
                    break;
                case 4:
                    config->dma_unit = 64;
                    break;
                default:
                    panic("spi use_data %d parameter not supported\n", m->use_dma);
                    break;
            }
            return 1;
        }
    }
    return 0;
}

static void jz_spi_transfer(struct spi_device *spi, struct spi_message *msg, int count)
{
    int i;
    int use_dma_flag;
    struct spi_config_data *config = &(spi->config);
    int id = config->id;

    jz_spi_enable(config);

    thread_waiter_init(&jzspi_dev[id].waiter);

    use_dma_flag = is_use_dma(config, msg, count);
    if (use_dma_flag)
        spi_dma_request(config);

    chip_select(config, 1);
    for (i = 0; i < count; i++) {
        struct spi_message *m = msg + i;
        jzspi_dev[id].cs_change = m->cs_change;
        if (m->use_dma) {
            spi_write_read_dma(config, (void *)m->tx_buf, m->tlen, m->rx_buf, m->rlen);
        } else {
            spi_write_read_inr(config, (void *)m->tx_buf, m->tlen, m->rx_buf, m->rlen);
        }
        thread_waiter_wait(&jzspi_dev[id].waiter);
        if (i < count - 1) {
            if (m->cs_change) {
                chip_select(config, 0);
                chip_select(config, 1);
            }
        }
    }
    chip_select(config, 0);
    if (use_dma_flag)
        spi_dma_release(config);

    jz_spi_disable(config);
}

void soc_spi_transfer(struct spi_bus *bus, struct spi_device *spi, struct spi_message *msg, int count)
{
    if (os_in_handler_mode()) {
        jz_spi_transfer_poll(spi, msg, count);
    } else {
        jz_spi_transfer(spi, msg, count);
    }
}

struct spi_device *soc_spi_register(struct spi_bus *bus, struct spi_config_data *config)
{
    struct spi_device *spi = malloc(sizeof(struct spi_device));
    char *name;

    spi->config = *config;

    if (config->name)
        name = config->name;
    else
        name = "spi_cs";

    int pin = config->cs_pin;
    if (gpio_is_valid(pin)) {
        int func = config->cs_valid_level ? GPIO_OUTPUT0 : GPIO_OUTPUT1;
        gpio_init(pin, func, name);
    }

    return spi;
}

void soc_spi_unregister(struct spi_bus *bus, struct spi_device *spi)
{
    struct spi_config_data *config = &(spi->config);
    if (gpio_is_valid(config->cs_pin))
        gpio_release(config->cs_pin);
    free(spi);
}

static struct spi_bus_ops spi_bus_ops = {
    .ops_spi_register = soc_spi_register,
    .ops_spi_unregister = soc_spi_unregister,
    .ops_spi_transfer = soc_spi_transfer,
};

static void jz_spi_init(struct jz_spi_drv *spi, int id)
{
    clk_enable(spi->clk);
    spi_gpio_request(spi);
    jz_spi_init_setting(id);
    clk_disable(spi->clk);
    request_irq(spi->irq, 0, spi_intr_handler, spi->irq_name , (void *)id);
    int ret = spi_bus_register(&spi->spi_bus,
         id, spi->irq_name, &spi_bus_ops);
    assert(!ret);
}

void soc_spi_init_driver(void)
{
    jzspi_dev[0].clk = clk_get("gate_ssi0");
    assert(jzspi_dev[0].clk);
    jzspi_dev[1].clk = clk_get("gate_ssi1");
    assert(jzspi_dev[1].clk);
    clk_cgu = clk_get("cgu_ssi");
    assert(clk_cgu);

    clk_enable(clk_cgu);
    clk_set_rate(clk_cgu,CONFIG_X2000_SPI_CLK_RATE); //100Mhz
    cgu_clk_rate = clk_get_rate(clk_cgu);

    if (jzspi_dev[0].is_enable)
        jz_spi_init(&jzspi_dev[0], 0);
    if (jzspi_dev[1].is_enable)
        jz_spi_init(&jzspi_dev[1], 1);
}
