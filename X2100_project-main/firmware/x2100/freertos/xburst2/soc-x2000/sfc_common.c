#include <common.h>
#include <assert.h>
#include <malloc.h>
#include <driver/cache.h>
#include <driver/clk.h>
#include <driver/gpio.h>
#include <soc/base.h>
#include "sfc_common.h"
#include "sfc_regs.h"
#include <driver/irq.h>
#include <errno.h>

#define SFC_ADDR(reg)                   (io_addr(SFC_IOBASE + reg))

#define GPIO_SFC_CLK                    GPIO_PE(16)
#define GPIO_SFC_CE0                    GPIO_PE(17)
#define GPIO_SFC_DR                     GPIO_PE(18)
#define GPIO_SFC_DT                     GPIO_PE(19)
#define GPIO_SFC_WP                     GPIO_PE(20)
#define GPIO_SFC_HOLD                   GPIO_PE(21)

static void jz_sfc_irq_callback(int32_t irq, void *dev);

static inline void sfc_write(unsigned int reg, int val)
{
    *SFC_ADDR(reg) = val;
}

static inline unsigned int sfc_read(unsigned int reg)
{
    return *SFC_ADDR(reg);
}

#ifdef DEBUG
static void sfc_dump_regs(void)
{
    int i = 0;
    printf("SFC_GLB             :%08x\n", sfc_read(SFC_GLB));
    printf("SFC_DEV_CONF        :%08x\n", sfc_read(SFC_DEV_CONF));
    printf("SFC_DEV_STA_EXP     :%08x\n", sfc_read(SFC_DEV_STA_EXP));
    printf("SFC_DEV_STA_RT      :%08x\n", sfc_read(SFC_DEV_STA_RT));
    printf("SFC_DEV_STA_MSK     :%08x\n", sfc_read(SFC_DEV_STA_MSK));
    printf("SFC_TRAN_LEN        :%08x\n", sfc_read(SFC_TRAN_LEN));

    for(i = 0; i < 6; i++)
        printf("SFC_TRAN_CONF(%d)   :%08x\n", i,sfc_read(SFC_TRAN_CONF(i)));

    for(i = 0; i < 6; i++)
        printf("SFC_DEV_ADDR(%d)    :%08x\n", i,sfc_read(SFC_DEV_ADDR(i)));

    printf("SFC_MEM_ADDR        :%08x\n", sfc_read(SFC_MEM_ADDR ));
    printf("SFC_TRIG            :%08x\n", sfc_read(SFC_TRIG));
    printf("SFC_SR              :%08x\n", sfc_read(SFC_SR));
    printf("SFC_SCR             :%08x\n", sfc_read(SFC_SCR));
    printf("SFC_INTC            :%08x\n", sfc_read(SFC_INTC));
    printf("SFC_FSM             :%08x\n", sfc_read(SFC_FSM));
    printf("SFC_CGE             :%08x\n", sfc_read(SFC_CGE));
}
#endif


static void soc_sfc_init_gpio_pa_6bit(void)
{
    gpio_set_func(GPIO_SFC_CLK,    GPIO_FUNC_0);
    gpio_set_func(GPIO_SFC_CE0,    GPIO_FUNC_0);
    gpio_set_func(GPIO_SFC_DR,     GPIO_FUNC_0);
    gpio_set_func(GPIO_SFC_DT,     GPIO_FUNC_0);
    gpio_set_func(GPIO_SFC_WP,     GPIO_FUNC_0);
    gpio_set_func(GPIO_SFC_HOLD ,  GPIO_FUNC_0);
}

static void soc_sfc_set_freq(struct jz_sfc *jz_sfc)
{
    jz_sfc->clk = clk_get("cgu_sfc");
    jz_sfc->clk_gate = clk_get("gate_sfc");
    assert(jz_sfc->clk != NULL);
    assert(jz_sfc->clk_gate != NULL);

    clk_set_rate(jz_sfc->clk, jz_sfc->clk_rate);
    clk_enable(jz_sfc->clk);
    clk_enable(jz_sfc->clk_gate);
}

static void soc_sfc_clear_reg_init(struct jz_sfc *jz_sfc)
{
    int n;

    for (n = 0; n < N_MAX; n++) {
        sfc_write(SFC_TRAN_CONF(n), 0);
        sfc_write(SFC_DEV_ADDR(n), 0);
        sfc_write(SFC_DEV_ADDR_PLUS(n), 0);
    }

    sfc_write(SFC_DEV_CONF, 0);
    sfc_write(SFC_DEV_STA_EXP, 0);
    sfc_write(SFC_DEV_STA_MSK, 0);
    sfc_write(SFC_TRAN_LEN, 0);
    sfc_write(SFC_MEM_ADDR, 0);
    sfc_write(SFC_TRIG, 0);
    sfc_write(SFC_SCR, 0);
    sfc_write(SFC_INTC, 0);
    sfc_write(SFC_CGE, 0);
    sfc_write(SFC_RM_DR, 0);
}

static void soc_sfc_flush_fifo(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_TRIG, TRIG_FLUSH);
}

static void soc_sfc_clear_all_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_SCR, 0x1f);
}

static void soc_sfc_mask_all_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_INTC, 0x1f);
}

static inline void soc_sfc_enable_all_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_INTC, 0);
}

static void soc_sfc_threshold(struct jz_sfc *jz_sfc, uint32_t value)
{
    uint32_t tmp;

    tmp = sfc_read(SFC_GLB);
    tmp &= ~GLB_THRESHOLD_MSK;
    tmp |= value << GLB_THRESHOLD_OFFSET;
    sfc_write(SFC_GLB, tmp);
}

void soc_sfc_smp_delay(struct jz_sfc *jz_sfc, uint32_t value)
{
    uint32_t tmp;

    tmp = sfc_read(SFC_DEV_CONF);
    tmp &= ~DEV_CONF_SMP_DELAY_MSK;
    tmp |= value << DEV_CONF_SMP_DELAY_OFFSET;
    sfc_write(SFC_DEV_CONF, tmp);
}

int32_t soc_set_flash_timing(struct jz_sfc *jz_sfc,
        uint32_t t_hold, uint32_t t_setup, uint32_t t_shslrd, uint32_t t_shslwr)
{
    uint32_t c_hold;
    uint32_t c_setup;
    uint32_t t_in, c_in;
    uint32_t cycle;
    uint32_t rate, tmp;

    rate = jz_sfc->clk_rate / 1000000;
    cycle = 1000 / rate;

    c_hold = t_hold / cycle;
    c_setup = t_setup / cycle;
    t_in = max(t_shslrd, t_shslwr);
    c_in = t_in / cycle;
    if (c_in > 0xf)
        c_in = 0xf;

    tmp = sfc_read(SFC_DEV_CONF);
    tmp &= ~(DEV_CONF_THOLD_MSK | DEV_CONF_TSETUP_MSK | DEV_CONF_TSH_MSK);

    tmp |= ((c_hold << DEV_CONF_THOLD_OFFSET) |
        (c_setup << DEV_CONF_TSETUP_OFFSET) |
        (c_in << DEV_CONF_TSH_OFFSET));

    sfc_write(SFC_DEV_CONF, tmp);

    return 0;
}

static void soc_sfc_set_length(struct jz_sfc *jz_sfc, uint32_t value)
{
    sfc_write(SFC_TRAN_LEN, value);
}

static void soc_sfc_transfer_mode(struct jz_sfc *jz_sfc, uint32_t value)
{
    uint32_t tmp;

    tmp = sfc_read(SFC_GLB);
    if (value == 0) {
        tmp &= ~GLB_OP_MODE;
    } else {
        tmp |= GLB_OP_MODE;
    }
    sfc_write(SFC_GLB, tmp);
}

static void soc_sfc_read_data(struct jz_sfc *jz_sfc, uint32_t *value)
{
    *value = sfc_read(SFC_RM_DR);
}

static void soc_sfc_write_data(struct jz_sfc *jz_sfc, const uint32_t value)
{
    sfc_write(SFC_RM_DR, value);
}

static void soc_sfc_dev_conf_init(struct jz_sfc *jz_sfc)
{
    /* set ce, wp, hold pin for high level */
    sfc_write(SFC_DEV_CONF, DEV_CONF_HOLDDL | DEV_CONF_WPDL | DEV_CONF_CEDL);
}

static void soc_sfc_use_cdt(struct jz_sfc *jz_sfc)
{
    uint32_t tmp = sfc_read(SFC_GLB);

    tmp |= GLB_CDT_EN;
    sfc_write(SFC_GLB, tmp);

    jz_sfc->cdt_addr = SFC_ADDR(SFC_CDT);
}

static void soc_sfc_use_dma(struct jz_sfc *jz_sfc)
{
    uint32_t tmp = sfc_read(SFC_GLB);
    tmp |= GLB_DES_EN;
    sfc_write(SFC_GLB, tmp);
}

static void soc_sfc_controller_init(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_TRIG, TRIG_STOP);       /* sfc stop */
    soc_sfc_clear_reg_init(jz_sfc);

    /*set dev config*/
    soc_sfc_dev_conf_init(jz_sfc);

    soc_sfc_mask_all_intc(jz_sfc);
    soc_sfc_clear_all_intc(jz_sfc);

    soc_sfc_threshold(jz_sfc, jz_sfc->threshold);

    /*config the sfc pin init state*/
    soc_sfc_transfer_mode(jz_sfc, SLAVE_MODE);
    if (jz_sfc->clk_rate >= 200 * 1000000) {
        soc_sfc_smp_delay(jz_sfc, DEV_CONF_SMP_DELAY_180);
    }

    soc_sfc_use_dma(jz_sfc);

    soc_sfc_use_cdt(jz_sfc);
}

static inline void  soc_sfc_clear_end_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_SCR, CLR_END);
}

static inline void soc_sfc_clear_treq_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_SCR, CLR_TREQ);
}

static inline void soc_sfc_clear_rreq_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_SCR, CLR_RREQ);
}

static inline void soc_sfc_clear_over_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_SCR, CLR_OVER);
}

static inline void soc_sfc_clear_under_intc(struct jz_sfc *jz_sfc)
{
    sfc_write(SFC_SCR, CLR_UNDER);
}

static void cpu_write_txfifo(struct jz_sfc *jz_sfc)
{
    int i;
    unsigned long align_len = 0;
    unsigned int fifo_num = 0;
    struct sfc_cdt_xfer *xfer;

    xfer = jz_sfc->xfer;
    align_len = ALIGN(xfer->config.datalen, 4);

    if (((align_len - xfer->config.cur_len) / 4) > SFC_THRESHOLD) {
        fifo_num = SFC_THRESHOLD;
    } else {
        fifo_num = (align_len - xfer->config.cur_len) / 4;
    }

    for (i = 0; i < fifo_num; i++) {
        soc_sfc_write_data(jz_sfc, *(unsigned int *)xfer->config.buf);
        xfer->config.buf += 4;
        xfer->config.cur_len += 4;
    }
}

#ifdef SFC_NOR_DEBUG
void dump_cdt(struct jz_sfc *sfc)
{
    struct sfc_cdt *cdt;
    int i;

    if (sfc->cdt_addr == NULL) {
        sfc_debug("%s error: sfc res not init !\n", __func__);
        return;
    }

    cdt = sfc->cdt_addr;

    for(i = 0; i < 32; i++) {
        sfc_debug("\nnum------->%d\n", i);
        sfc_debug("link:%x, ENDIAN:%x, WORD_UINT:%x, TRAN_MODE:%x, ADDR_KIND:%x\n",
                (cdt[i].link >> 31) & 0x1, (cdt[i].link >> 18) & 0x1,
                (cdt[i].link >> 16) & 0x3, (cdt[i].link >> 4) & 0xf,
                (cdt[i].link >> 0) & 0x3
                );
        sfc_debug("CLK_MODE:%x, ADDR_WIDTH:%x, POLL_EN:%x, CMD_EN:%x,PHASE_FORMAT:%x, DMY_BITS:%x, DATA_EN:%x, TRAN_CMD:%x\n",
                (cdt[i].xfer >> 29) & 0x7, (cdt[i].xfer >> 26) & 0x7,
                (cdt[i].xfer >> 25) & 0x1, (cdt[i].xfer >> 24) & 0x1,
                (cdt[i].xfer >> 23) & 0x1, (cdt[i].xfer >> 17) & 0x3f,
                (cdt[i].xfer >> 16) & 0x1, (cdt[i].xfer >> 0) & 0xffff
                );
        sfc_debug("DEV_STA_EXP:%x\n", cdt[i].staExp);
        sfc_debug("DEV_STA_MSK:%x\n", cdt[i].staMsk);
    }
}
#endif

struct jz_sfc *soc_sfc_init(unsigned long rate_clk)
{
    struct jz_sfc *jz_sfc = NULL;

    jz_sfc = (struct jz_sfc *)malloc(sizeof(struct jz_sfc));
    assert(jz_sfc != NULL);

    soc_sfc_init_gpio_pa_6bit();

    jz_sfc->clk_rate = rate_clk;
    jz_sfc->threshold = SFC_THRESHOLD;
    jz_sfc->irq = IRQ_SFC;
    jz_sfc->retry_count = 0;
    soc_sfc_set_freq(jz_sfc);

    soc_set_flash_timing(jz_sfc,  DEF_TCHSH, DEF_TSLCH, DEF_TSHSL_R, DEF_TSHSL_W);

    request_irq(jz_sfc->irq, 0, jz_sfc_irq_callback, "jz-sfc" , jz_sfc);

    thread_waiter_init(&jz_sfc->data_wait);

    soc_sfc_controller_init(jz_sfc);

#ifdef DEBUG
    sfc_dump_regs();
#endif

    return jz_sfc;
}

void soc_sfc_deinit(struct jz_sfc *jz_sfc)
{
    release_irq(jz_sfc->irq);
    free(jz_sfc);
}

static inline void soc_sfc_dev_sta_exp(struct jz_sfc *sfc, uint32_t value)
{
    sfc_write(SFC_DEV_STA_EXP, value);
}

static inline void soc_sfc_dev_sta_msk(struct jz_sfc *sfc, uint32_t value)
{
    sfc_write(SFC_DEV_STA_MSK, value);
}

static inline void soc_sfc_set_mem_addr(struct jz_sfc *sfc, uint32_t addr)
{
    sfc_write(SFC_MEM_ADDR, addr);
}

static void sfc_set_desc_addr(struct jz_sfc *sfc, unsigned int addr)
{
    sfc_write(SFC_DES_ADDR, addr);
}

static inline void soc_sfc_start(struct jz_sfc *sfc)
{
    uint32_t tmp;
    tmp = sfc_read(SFC_TRIG);
    tmp |= TRIG_START;
    sfc_write(SFC_TRIG, tmp);
}

static int32_t soc_sfc_stop(struct jz_sfc *sfc)
{
    sfc_write(SFC_TRIG, TRIG_STOP);
    return 0;
}

static unsigned int cpu_read_rxfifo(struct jz_sfc *sfc)
{
    int i;
    unsigned long align_len = 0;
    unsigned int fifo_num = 0;
    unsigned int data[1] = {0};
    unsigned int last_word = 0;
    struct sfc_cdt_xfer *xfer;

    xfer = sfc->xfer;
    align_len = ALIGN(xfer->config.datalen, 4);

    if (((align_len - xfer->config.cur_len) / 4) > SFC_THRESHOLD) {
        fifo_num = SFC_THRESHOLD;
        last_word = 0;
    } else {
        /* last aligned THRESHOLD data */
        if (xfer->config.datalen % 4) {
            fifo_num = (align_len - xfer->config.cur_len) / 4 - 1;
            last_word = 1;
        } else {
            fifo_num = (align_len - xfer->config.cur_len) / 4;
            last_word = 0;
        }
    }

    for (i = 0; i < fifo_num; i++) {
        soc_sfc_read_data(sfc, (unsigned int *)xfer->config.buf);
        xfer->config.buf += 4;
        xfer->config.cur_len += 4;
    }

    /* last word */
    if (last_word == 1) {
        soc_sfc_read_data(sfc, data);
        memcpy((void *)xfer->config.buf, data, xfer->config.datalen % 4);

        xfer->config.buf += xfer->config.datalen % 4;
        xfer->config.cur_len += 4;
    }
    return 0;
}

static int32_t soc_sfc_start_transfer(struct jz_sfc *sfc)
{
    int32_t err;
    soc_sfc_clear_all_intc(sfc);

    soc_sfc_flush_fifo(sfc);

    soc_sfc_enable_all_intc(sfc);

    soc_sfc_start(sfc);

    err = thread_waiter_wait_timeout(&sfc->data_wait, SFC_TRANSFER_TIMEOUT);
    if (err) {
        soc_sfc_mask_all_intc(sfc);
        soc_sfc_clear_all_intc(sfc);

        soc_sfc_stop(sfc);
        soc_sfc_flush_fifo(sfc);

        return -ETIMEDOUT;
    }
    return 0;
}

static void soc_sfc_set_index(struct jz_sfc *sfc, unsigned short index)
{
    uint32_t tmp = sfc_read(SFC_CMD_IDX);

    tmp &= ~CMD_IDX_MSK;
    tmp |= index;
    sfc_write(SFC_CMD_IDX, tmp);
}

static void soc_sfc_set_addr(struct jz_sfc *sfc, struct sfc_cdt_xfer *xfer)
{
    sfc_write(SFC_COL_ADDR, xfer->columnaddr);
    sfc_write(SFC_ROW_ADDR, xfer->rowaddr);
    sfc_write(SFC_STA_ADDR0, xfer->staaddr0);
    sfc_write(SFC_STA_ADDR1, xfer->staaddr1);
}

static void soc_sfc_set_dataen(struct jz_sfc *sfc, uint8_t dataen)
{
    uint32_t tmp = sfc_read(SFC_CMD_IDX);
    tmp &= ~CDT_DATAEN_MSK;
    tmp |= (dataen << CDT_DATAEN_OFF);
    sfc_write(SFC_CMD_IDX, tmp);
}

static void soc_sfc_set_datadir(struct jz_sfc *sfc, uint8_t datadir)
{
    uint32_t tmp = sfc_read(SFC_CMD_IDX);
    tmp &= ~CDT_DIR_MSK;
    tmp |= (datadir << CDT_DIR_OFF);
    sfc_write(SFC_CMD_IDX, tmp);
}

static void jz_sfc_irq_callback(int32_t irq, void *dev)
{
    struct jz_sfc *sfc = dev;
    uint32_t val;
    uint8_t err_flag = 0;

    val = sfc_read(SFC_SR) & 0x1f;

    if (val & CLR_RREQ) {
        sfc_write(SFC_SCR, CLR_RREQ);
        cpu_read_rxfifo(sfc);
    } else if (val & CLR_TREQ) {
        sfc_write(SFC_SCR, CLR_TREQ);
        cpu_write_txfifo(sfc);
    } else if (val & CLR_OVER) {
        sfc_write(SFC_SCR, CLR_OVER);
        printf("sfc OVER\n");
        err_flag = 1;
    } else if (val & CLR_UNDER) {
        sfc_write(SFC_SCR, CLR_UNDER);
        printf("sfc UNDER\n");
        err_flag = 1;
    } else if (val & CLR_END) {
        soc_sfc_mask_all_intc(sfc);
        soc_sfc_clear_end_intc(sfc);
        sfc->retry_count = 0;
        thread_waiter_wakeup(&sfc->data_wait);
    }

    if (err_flag) {
        if (sfc->retry_count > 0) {
            sfc->retry_count --;
        } else if (sfc->retry_count == 0) {
            soc_sfc_clear_all_intc(sfc);
            soc_sfc_mask_all_intc(sfc);
            return ;
        }

        soc_sfc_clear_all_intc(sfc);
        soc_sfc_mask_all_intc(sfc);
        thread_waiter_wakeup(&sfc->data_wait);
    }
}

void sfc_dma_cache_sync_from_device(void *buf, uint32_t len)
{
    uint32_t start_addr = 0, end_addr = 0;
    uint32_t ALIGN_SIZE = cache_line_size();
    uint32_t size_extra = 0, size_inv = 0;

    if ( (uint32_t)buf & (ALIGN_SIZE - 1)) {
        // 向下对齐
        start_addr = (uint32_t)buf & ~(ALIGN_SIZE - 1);
        end_addr = (uint32_t)(buf + len - 1) & ~(ALIGN_SIZE - 1);
        size_extra = (uint32_t)buf & (ALIGN_SIZE - 1);

        // 将对齐后的上部额外的cache写回mem并取消cache
        if (start_addr == end_addr) {
            flush_dcache(start_addr, ALIGN_SIZE);
            return;
        }
        else {
            flush_dcache(start_addr, ALIGN_SIZE);
            flush_dcache(end_addr, ALIGN_SIZE);
        }

        if (end_addr - start_addr > ALIGN_SIZE) {
            //除去首尾的cache大小的cache大小
            size_inv = (len + size_extra - ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);
            invalidate_dcache(start_addr + ALIGN_SIZE, size_inv);
        }

    } else {
        start_addr = (uint32_t)buf;

        size_inv = len & ~(ALIGN_SIZE - 1);

        //如果len的长度也对齐，就直接清除len长度的cache
        if (!(len & (ALIGN_SIZE - 1))) {
            invalidate_dcache(start_addr, size_inv);
            return;
        }

        // 清除DMA操作mem的len向下对齐的部分cache
        if (size_inv)
            invalidate_dcache(start_addr, size_inv);

        end_addr = start_addr + size_inv;
        //将对齐后的上部额外的cache写回mem并取消cache
        flush_dcache(end_addr, ALIGN_SIZE);
    }
}

void sfc_dma_cache_sync_to_device(void *buf, uint32_t len)
{
    uint32_t start_addr = 0;
    uint32_t ALIGN_SIZE = cache_line_size();
    uint32_t size_extra = 0, size_flush = 0;

    if ( (uint32_t)buf & (ALIGN_SIZE -1)) {
        // buf地址未对齐，向下对齐
        start_addr = (uint32_t)buf & ~(ALIGN_SIZE - 1);

        size_extra = (uint32_t)buf & (ALIGN_SIZE - 1);

        size_flush = (len + size_extra + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);

        flush_dcache(start_addr, size_flush);
    } else {
        start_addr = (uint32_t)buf;

        size_flush = (len + ALIGN_SIZE - 1) & ~(ALIGN_SIZE - 1);

        flush_dcache(start_addr, size_flush);
    }
}

void sfc_set_data_config(struct jz_sfc *sfc, struct sfc_cdt_xfer *xfer)
{
    soc_sfc_set_dataen(sfc, xfer->dataen);

    soc_sfc_set_length(sfc, 0);
    if (xfer->dataen) {
        soc_sfc_set_datadir(sfc, xfer->config.data_dir);
        soc_sfc_transfer_mode(sfc, xfer->config.ops_mode);
        soc_sfc_set_length(sfc, xfer->config.datalen);

        /* Memory address for DMA when do not use DMA descriptor */
        soc_sfc_set_mem_addr(sfc, 0);
        if (xfer->config.ops_mode == DMA_OPS) {
            if (xfer->config.data_dir == GLB_TRAN_DIR_READ) {
                sfc_dma_cache_sync_from_device(xfer->config.buf, xfer->config.datalen);
            }else{
                sfc_dma_cache_sync_to_device(xfer->config.buf, xfer->config.datalen);
            }
            /* Set Descriptor address for DMA */
            sfc_set_desc_addr(sfc, virt_to_phys(sfc->desc));
        }
        sfc->xfer = xfer;
    }
}

int32_t sfc_sync(struct jz_sfc *sfc, struct sfc_cdt_xfer *xfer)
{
    // struct sfc_cdt_xfer *xfer = head;

    /* 1. set cmd index */
    soc_sfc_set_index(sfc, xfer->cmd_index);

    /* 2. set addr */
    soc_sfc_set_addr(sfc, xfer);

    /* 3. config data config */
    sfc_set_data_config(sfc, xfer);

    return soc_sfc_start_transfer(sfc);
}
