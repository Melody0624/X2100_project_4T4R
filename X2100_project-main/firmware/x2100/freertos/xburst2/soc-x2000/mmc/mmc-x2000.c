/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */
#include <common.h>
#include <driver/clk.h>
#include <driver/cache.h>
#include <malloc.h>
#include <os.h>
#include <list.h>
#include <driver/irq.h>
#include <soc/cpm.h>
#include "mmc-core.h"
#include "mmc-sdio-irq.h"

#include "mmc-x2000-gpio.h"
#include "mmc-x2000.h"
#include "mmc-x2000-regs.h"
#include "mmc-x2000-hal.h"
#include "mmc_devices.h"
#include "mmc_cpm.h"

//#define MMC_DEBUG

#ifdef MMC_DEBUG
#define MSC_DBG(...)                    printf("[MSC]"), printf(__VA_ARGS__)
#define MSC_WARN(...)                   printf("[MSC]"), printf(__VA_ARGS__)
#else
#define MSC_DBG(...)
#define MSC_WARN(...)
#endif

#define X2000_MSC_MAX_FREQ              (200 * 1000 * 1000)


#define SOC_MSC_DMA_ALIGN               (cache_line_size())    /* 大于等于soc cache line 大小 */

enum {
    EVENT_CMD_RESPONSE                  = 0,
    EVENT_DATA_TRAN_DONE                = 1,
    EVENT_PROGTAMMING_DONE              = 2,
    EVENT_DMA_DONE                      = 3,
};

/*
 * Host Mode Flag(Bit)
 */
#define SOC_MSC_USE_POLL                (1 << 0)  /* Host poll mode */
#define SOC_MSC_DEVICE_DEAD             (1 << 3)  /* Device unresponsive */
#define SOC_MSC_SDIO_IRQ_ENABLED        (1 << 9)  /* SDIO irq enabled */
#define SOC_MSC_SDIO_THREAD_IRQ         (1 << 31) /* create thread handle for claim irq */

/*
 *  function export
 */
struct jz_mmc_host {
    int is_enable;
    int index;

    int irq;
    const char *irq_name;

    struct clk *clk;
    struct clk *clk_gate;

    uint32_t max_freq;

    struct mmc_host *mmc;
    struct mmc_cmd *cmd;
    struct mmc_data *data;

    volatile uint32_t flag;

    char *thrad_irq_name;
    uint32_t thread_isr;   /* 控制器中断 thread irq处理标志 */
    spinlock_t dev_spin;
    thread_waiter_t dev_waiter;

    uint32_t pending_events;    /* 关注哪些中断事件 */
    uint32_t completed_events;
    struct mutex mutex;
    critical_thread_cond_t thread_cond;

    struct mmc_devices_config *device_config;
};

static struct jz_mmc_host jz_mmc_host[3] = {
    {
        /* 仅支持高速模式(1.8V) */
        #ifdef CONFIG_SOC_X2000_MSC0_BUS
        .index          = 0,
        .is_enable      = 1,
        .max_freq       = CONFIG_SOC_X2000_MSC0_MAX_FREQ,
        .irq            = IRQ_MSC0,
        .irq_name       = "MSC0",
        .thrad_irq_name = "msc0_thread_irq",
        #endif
    },

    {
        /* 仅支持高速模式(1.8V) */
        #ifdef CONFIG_SOC_X2000_MSC1_BUS
        .index          = 1,
        .is_enable      = 1,
        .max_freq       = CONFIG_SOC_X2000_MSC1_MAX_FREQ,
        .irq            = IRQ_MSC1,
        .irq_name       = "SDIO1",
        .thrad_irq_name = "sdio1_thread_irq",
        #endif
    },

    {
        /* 支持正常模式(3.3V) / 高速模式(1.8V)选择 */
        #ifdef CONFIG_SOC_X2000_MSC2_BUS
        .index          = 2,
        .is_enable      = 1,
        .max_freq       = CONFIG_SOC_X2000_MSC2_MAX_FREQ,
        .irq            = IRQ_MSC2,
        .irq_name       = "MMC2",
        .thrad_irq_name = "msc2_thread_irq",
        #endif
    },
};

static inline void *mmc_get_privdata(struct mmc_host *mmc)
{
    return (void *)mmc->private;
}

static inline void *mmc_set_privdata(struct mmc_host *mmc, void *data)
{
    return mmc->private = data;
}

static inline int soc_mmc_is_poll_mode(struct jz_mmc_host *host)
{
    return (host->flag & SOC_MSC_USE_POLL);
}

static inline void soc_mmc_poll_mode_enable(struct jz_mmc_host *host, int mode)
{
    if (mode)
        host->flag |= SOC_MSC_USE_POLL;
    else
        host->flag &= ~SOC_MSC_USE_POLL;
}


static inline int soc_mmc_test_pending(struct jz_mmc_host *host, uint32_t events)
{
    uint32_t ret_val = host->pending_events & (1 << events);

    return (ret_val != 0);
}

static inline void soc_mmc_clear_pending(struct jz_mmc_host *host, uint32_t events)
{
    clear_bits(host->pending_events, 1 << events);
}

static inline void soc_mmc_set_pending(struct jz_mmc_host *host, uint32_t events)
{
    set_bits(host->pending_events, 1 << events);
}

static void soc_mmc_enable_irq(int index, uint32_t bits)
{
    uint16_t value_l = (bits >> 0) & 0xFFFF;
    uint16_t value_h = (bits >> 16) & 0xFFFF;
    uint16_t tmp;
    uint16_t normal;
    uint16_t error;

    tmp = mmc_hal_get_normal_interrupt_status(index);
    normal = tmp | value_l;
    mmc_hal_enable_normal_interrupt(index, normal);

    tmp = mmc_hal_get_error_interrupt_status(index);
    error = tmp | value_h;
    mmc_hal_enable_error_interrupt(index, error);

    tmp = mmc_hal_get_normal_interrupt_signal_status(index);
    normal = tmp | value_l;
    mmc_hal_enable_normal_interrupt_signal(index, normal);

    tmp = mmc_hal_get_error_interrupt_signal_status(index);
    error = tmp | value_h;
    mmc_hal_enable_error_interrupt_signal(index, error);
}

__attribute__((__unused__)) static void soc_mmc_disable_irq(int index, uint32_t bits)
{
    uint16_t value_l = (bits >> 0) & 0xFFFF;
    uint16_t value_h = (bits >> 16) & 0xFFFF;
    uint16_t tmp;
    uint16_t normal;
    uint16_t error;

    tmp = mmc_hal_get_normal_interrupt_status(index);
    normal = tmp & ~value_l;
    mmc_hal_enable_normal_interrupt(index, normal);

    tmp = mmc_hal_get_error_interrupt_status(index);
    error = tmp & ~value_h;
    mmc_hal_enable_error_interrupt(index, error);

    tmp = mmc_hal_get_normal_interrupt_signal_status(index);
    normal = tmp & ~value_l;
    mmc_hal_enable_normal_interrupt_signal(index, normal);

    tmp = mmc_hal_get_error_interrupt_signal_status(index);
    error = tmp & ~value_h;
    mmc_hal_enable_error_interrupt_signal(index, error);
}

__attribute__((__unused__)) static void soc_mmc_clear_irq(int index, uint32_t bits)
{
    uint16_t value_l = (bits >> 0) & 0xFFFF;
    uint16_t value_h = (bits >> 16) & 0xFFFF;
    uint16_t tmp;
    uint16_t normal;
    uint16_t error;

    tmp = mmc_hal_get_normal_int_status(index);
    normal = tmp | value_l;
    mmc_hal_clear_normal_int_status(index, normal);

    tmp = mmc_hal_get_error_int_status(index);
    error = tmp | value_h;
    mmc_hal_clear_error_int_status(index, error);
}


static void soc_mmc_enable_sdio_irq_nolock(struct jz_mmc_host *host, int enable)
{
    int index = host->index;

    /* 外设已经不存在,不执行中断开关 */
    if (host->flag & SOC_MSC_DEVICE_DEAD)
        return ;

    uint16_t value_n = mmc_hal_get_normal_interrupt_status(index);
    uint16_t value_s = mmc_hal_get_normal_interrupt_signal_status(index);

    if (enable) {
        value_n |= MSC_NORMAL_INT_CARD_INT;
        value_s |= MSC_NORMAL_INT_CARD_INT;
    } else {
        value_n &= ~MSC_NORMAL_INT_CARD_INT;
        value_s &= ~MSC_NORMAL_INT_CARD_INT;
    }

    mmc_hal_enable_normal_interrupt(index, value_n);
    mmc_hal_enable_normal_interrupt_signal(index, value_s);

}

static void soc_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
    struct jz_mmc_host *host = mmc_get_privdata(mmc);

    if (enable)
        host->flag |= SOC_MSC_SDIO_IRQ_ENABLED;
    else
        host->flag &= ~SOC_MSC_SDIO_IRQ_ENABLED;

    soc_mmc_enable_sdio_irq_nolock(host, enable);
}

static void soc_mmc_controller_thread_irq(void *dev_id)
{
    struct jz_mmc_host *host = dev_id;
    unsigned long flags;
    uint32_t isr;

    while (host->flag & SOC_MSC_SDIO_THREAD_IRQ) {
        /* 等待唤醒 */
        thread_waiter_wait(&host->dev_waiter);

        spin_lock_irqsave(&host->dev_spin, flags);
        isr = host->thread_isr;
        host->thread_isr = 0;
        spin_unlock_irqrestore(&host->dev_spin, flags);

        if (isr & MSC_NORMAL_INT_CARD_INT) {
            sdio_run_irqs(host->mmc);

            spin_lock_irqsave(&host->dev_spin, flags);
            if (host->flag & SOC_MSC_SDIO_IRQ_ENABLED)
                soc_mmc_enable_sdio_irq_nolock(host, 1);
            spin_unlock_irqrestore(&host->dev_spin, flags);
        }
    } /* end of while ... */
}

static void soc_mmc_interrupt_handler(int irq, void *dev)
{
    struct jz_mmc_host *host = (struct jz_mmc_host *)dev;
    int index = host->index;

    uint16_t normal_mask = mmc_hal_get_normal_interrupt_status(index);
    uint16_t error_mask = mmc_hal_get_error_interrupt_status(index);
    uint16_t normal_status = mmc_hal_get_normal_int_status(index);
    uint16_t error_status = mmc_hal_get_error_int_status(index);
    uint16_t normal_pengding = normal_status & normal_mask;
    uint16_t error_pengding = error_status & error_mask;
    MSC_DBG("MSC%d normal status=0x%x  error status=0x%x normal_pengding=0x%x error_pengding=0x%x\n", index, normal_status, error_status, normal_pengding, error_pengding);

    if ((host->flag & SOC_MSC_SDIO_IRQ_ENABLED) && (normal_mask & MSC_NORMAL_INT_CARD_INT)) {
        soc_mmc_enable_sdio_irq_nolock(host, 0);
        host->thread_isr |= MSC_NORMAL_INT_CARD_INT;

        thread_waiter_wakeup(&host->dev_waiter);
    }

    if (error_pengding & MSC_ERROR_STATUS_ALL_ERROR) {
        /*
         * clear error flags
         */
        if (error_pengding & MSC_ERROR_STATUS_CMD_ERROR)
            host->cmd->error = -EIO;
        if (error_pengding & MSC_ERROR_STATUS_DATA_ERROR)
            host->data->error = -EIO;

        mmc_hal_clear_error_int_status(index, error_pengding);
        if (soc_mmc_test_pending(host, EVENT_CMD_RESPONSE)
            || soc_mmc_test_pending(host, EVENT_DATA_TRAN_DONE)) {
            /*
             * 出错 唤醒等待
             */
            //soc_mmc_clear_pending(host, EVENT_CMD_RESPONSE);
            //soc_mmc_clear_pending(host, EVENT_DATA_TRAN_DONE);
            //critical_thread_cond_signal(&host->thread_cond);
        }
    } else if (normal_pengding & MSC_NORMAL_INT_CMD_COMPLETE) {

        /* command / data 传输分开处理,使用if... else... 处理 */
        /* command complete */
        int value = normal_pengding & MSC_NORMAL_INT_CMD_COMPLETE;
        mmc_hal_clear_normal_int_status(index, value);

        if (soc_mmc_test_pending(host, EVENT_CMD_RESPONSE) ) {
            soc_mmc_clear_pending(host, EVENT_CMD_RESPONSE);
            critical_thread_cond_signal(&host->thread_cond);
        }
    } else if (normal_pengding & MSC_NORMAL_INT_XFER_COMPLETE) {

        /* data xfer complete */
        int value = normal_pengding & (MSC_NORMAL_INT_XFER_COMPLETE | MSC_NORMAL_INT_DMA);

        mmc_hal_clear_normal_int_status(index, value);
        if (soc_mmc_test_pending(host, EVENT_DATA_TRAN_DONE) ) {
            soc_mmc_clear_pending(host, EVENT_DATA_TRAN_DONE);
            critical_thread_cond_signal(&host->thread_cond);
        }

    } else if (normal_pengding & MSC_NORMAL_INT_DMA) {
        struct mmc_data *data = host->data;
        if (!data)
            return;

        /* 数据读写传输(SDMA) */
        /* SDMA需更新下一个DMA传输的数据 */
        unsigned int address_start = 0;
        unsigned int address_now;
        if (data->flags & MMC_DATA_WRITE) {
            address_start = (unsigned int)data->src;
        } else if (data->flags & MMC_DATA_READ) {
            address_start = (unsigned int)data->dest;
        } else {
            printf("SDMA data xfer direction invaild\n");
        }

        address_now = address_start + data->bytes_xfered;
        address_now &= ~(MSC_DEFAULT_BOUNDARY_SIZE - 1);
        address_now += (MSC_DEFAULT_BOUNDARY_SIZE);
        data->bytes_xfered = address_now - address_start;

        mmc_hal_set_sdma_address(host->index, virt_to_phys(address_now));

        int value = normal_pengding & MSC_NORMAL_INT_DMA;
        mmc_hal_clear_normal_int_status(index, value);

    } else if (normal_pengding & MSC_NORMAL_INT_READ_READY) {
        /*
         * CMD19 generates _only_ Buffer Read Ready interrupt
         */
        int value = normal_pengding & (MSC_NORMAL_INT_READ_READY);
        mmc_hal_clear_normal_int_status(index, value);
        if (soc_mmc_test_pending(host, EVENT_DATA_TRAN_DONE) ) {
            soc_mmc_clear_pending(host, EVENT_DATA_TRAN_DONE);
            critical_thread_cond_signal(&host->thread_cond);
        }
    }
}

static void soc_mmc_controller_reset_line(int index)
{
    uint32_t timeout = 100 * 1000;

    /* 复位cmd/data line */
    mmc_hal_software_reset_cmd_data(index);
    while ( mmc_hal_software_is_resetting_cmd_data(index) && --timeout)
        mdelay(1);

    if (!timeout)
        panic("MSC%d: controller reset cmd/data line timeout!\n", index);
}

/*
 * msc clock control bit[0]被置位后 cpm状态修改才有效
 */
static void soc_mmc_clk_init_stable(int index)
{
    uint32_t timeout = 0xFFFFFFFF;

    mmc_hal_clock_control_initialization_enable(index, 1);
    while (!mmc_hal_clock_control_initialization_stable(index) && --timeout);

    if (!timeout)
        panic("MSC%d: controller clk initialization stable timeout\n", index);
}

static int soc_mmc_clk_ctrl(struct jz_mmc_host *host, int on)
{
    if(on) {
        if(!clk_is_enabled(host->clk))
            clk_enable(host->clk);

        if(!clk_is_enabled(host->clk_gate))
            clk_enable(host->clk_gate);

    } else {
        if(clk_is_enabled(host->clk_gate))
            clk_disable(host->clk_gate);

        if(clk_is_enabled(host->clk))
            clk_disable(host->clk);
    }

    return 0;
}

static void soc_mmc_set_rx_phase(int index)
{
    unsigned int offset;
    unsigned int value;
    switch (index) {
    case 0:
        offset = CPM_MSC0CDR;
        break;
    case 1:
        offset = CPM_MSC1CDR;
        break;
    case 2:
        offset = CPM_MSC2CDR;
        break;
    default:
        printf("msc index(%d) is invaild\n", index);
        hang();
        break;
    }

    value = cpm_read_reg(offset);
    value &= ~(0x7 << 17);
    value |= (0x0 << 17);   /* sample clock: 0x7 is 325-degree for RX phase */
                            /* sample clock: 0x2 is  90-degree for RX phase */
                            /* sample clock: 0x0 is   0-degree for RX phase */
    cpm_write_reg(offset, value);
}

static void soc_mmc_set_tx_phase(int index)
{
    unsigned int offset;
    unsigned int value;
    switch (index) {
    case 0:
        offset = CPM_MSC0CDR;
        break;
    case 1:
        offset = CPM_MSC1CDR;
        break;
    case 2:
        offset = CPM_MSC2CDR;
        break;
    default:
        printf("msc index(%d) is invaild\n", index);
        hang();
        break;
    }

    value = cpm_read_reg(offset);
    value &= ~(0x3 << 15);
    value |= (0x3 << 15);  /* sample clock: 0x3 is 270-degree for TX phase
                            *               0x2 is 180-degree for TX phase
                            *               0x1 is 135-degree for TX phase
                            *               0x0 is 90-degree for TX phase
                            */
    cpm_write_reg(offset, value);
}

/*
 * enable: =1: msc enable tuning
 *         =0: msc disable tuning
 */
static void soc_mmc_enable_tuning(int index, int enable)
{
    unsigned int offset;
    unsigned int value;
    switch (index) {
    case 0:
        offset = CPM_MSC0CDR;
        break;
    case 1:
        offset = CPM_MSC1CDR;
        break;
    case 2:
        offset = CPM_MSC2CDR;
        break;
    default:
        printf("msc index(%d) is invaild\n", index);
        hang();
        break;
    }

    enable = !enable;
    value = cpm_read_reg(offset);
    value &= ~(0x1 << 20);     /* enable tuning */
    value |= (enable << 20);   /* bit[20] =1:disable, =0:enable  */
    cpm_write_reg(offset, value);
}

static void soc_mmc_set_power(struct jz_mmc_host *host)
{
    struct mmc_host *mmc = host->mmc;
    int index = host->index;
    int pwr = 0;
    struct mmc_devices_config *device = host->device_config;

    /* power off */
    if (mmc->power_mode == MMC_POWER_OFF) {
        if (device && device->device_power_off)
            device->device_power_off();

        pwr = MSC_POWER_CTRL_POWER_OFF;
        mmc_hal_set_power_control(index, pwr);

        return;
    }

    /* power on */
    if (device && device->device_power_on)
        device->device_power_on();

    soc_mmc_controller_reset_line(index);

    switch (1 << mmc->min_voltage) {
    case MMC_VDD_165_195:
        pwr = MSC_POWER_CTRL_POWER_180;
        break;
    case MMC_VDD_29_30:
    case MMC_VDD_30_31:
        pwr = MSC_POWER_CTRL_POWER_300;
        break;
    case MMC_VDD_32_33:
    case MMC_VDD_33_34:
        pwr = MSC_POWER_CTRL_POWER_330;
        break;
    default:
        printf("MSC%d Unsupport OCR min voltage %d\n", index, mmc->min_voltage);
        break;
    }

    pwr |= MSC_POWER_CTRL_POWER_ON;
    mmc_hal_set_power_control(index, pwr);
}

/*
 * 强制使能msc相关时钟，msc默认关闭的情况下使用该接口
 */
__attribute__((__unused__)) static void soc_mmc_force_invalid_clk_on(struct jz_mmc_host *host)
{
    int index = host->index;
    unsigned int cpm_offset;
    unsigned int value;
    unsigned int clk_gate_reg;
    unsigned int clk_gate_shift;
    switch (index) {
    case 0:
        cpm_offset = CPM_MSC0CDR;
        clk_gate_reg = CPM_CLKGR0;
        clk_gate_shift = 4;
        break;
    case 1:
        cpm_offset = CPM_MSC1CDR;
        clk_gate_reg = CPM_CLKGR0;
        clk_gate_shift = 5;
        break;
    case 2:
        cpm_offset = CPM_MSC2CDR;
        clk_gate_reg = CPM_CLKGR1;
        clk_gate_shift = 25;
        break;
    default:
        printf("msc index(%d) is invaild\n", index);
        hang();
        break;
    }

    /* 设置clk gate */
    value = cpm_read_reg(clk_gate_reg);
    value &= ~(0x1 << clk_gate_shift);     /* 关闭clk gate */
    cpm_write_reg(clk_gate_reg, value);

    /* 设置cpm clk */
    value = cpm_read_reg(cpm_offset);
    value &= ~(0x1 << 27);     /* cpm msc stop */
    value |= (1 << 29);        /* cpm msc freq change enable  */
    cpm_write_reg(cpm_offset, value);  /* 设置之后不检查busy状态 */
}

static void soc_mmc_select_exclk(struct jz_mmc_host *host, int enable)
{
    cpm_set_bit_v(CPM_MSC_EXCLK, CPM_MSC_EXCLK_ENABLE, enable);
}

static void soc_mmc_set_ios(struct mmc_host *mmc)
{
    struct jz_mmc_host *host = mmc_get_privdata(mmc);
    int index = host->index;

    soc_mmc_clk_ctrl(host, 1);

    switch(mmc->bus_width) {
    case MMC_BUS_WIDTH_8:
        mmc->flag &= ~MMC_BUS_WIDTH_MASK;
        mmc->flag |= MMC_BUS_WIDTH_8;
        mmc_hal_set_transfer_width_enable_8bit(index, 1);
        mmc_hal_set_transfer_width_enable_4bit(index, 0);
        break;

    case MMC_BUS_WIDTH_4:
        mmc->flag &= ~MMC_BUS_WIDTH_MASK;
        mmc->flag |= MMC_BUS_WIDTH_4;
        mmc_hal_set_transfer_width_enable_8bit(index, 0);
        mmc_hal_set_transfer_width_enable_4bit(index, 1);
        break;

    case MMC_BUS_WIDTH_1:
    default:
        mmc->flag &= ~MMC_BUS_WIDTH_MASK;
        mmc->flag |= MMC_BUS_WIDTH_1;
        mmc_hal_set_transfer_width_enable_8bit(index, 0);
        mmc_hal_set_transfer_width_enable_4bit(index, 0);
        break;
    }

    soc_mmc_set_power(host);

    /* set mmc controller clock */
    if (mmc->clock) {
        unsigned int clk_want = mmc->clock;

        /*
         * 根据频率大小选择时钟源
         * ext1(24M)   支持的最大频率为 24M / 4 = 6MHz
         * mpll(1500M) 支持的最小频率为 1500M / 4 / 256 = 1.46MHz
         */
        if (clk_want < 6 * 1000 * 1000) {
            clk_set_parent(host->clk, clk_get("ext1"));
            soc_mmc_select_exclk(host, 1);
        } else {
            clk_set_parent(host->clk, clk_get("mpll"));
            soc_mmc_select_exclk(host, 0);
        }

        clk_set_rate(host->clk, clk_want);

        MSC_DBG("MSC%d:%s %d clk_want = %d  real_rate=%ld\n",
                index, __FUNCTION__, __LINE__, clk_want, clk_get_rate(host->clk));

        if ((mmc->timing == MMC_TIMING_MMC_HS400) ||
            (mmc->timing == MMC_TIMING_MMC_HS200) ||
            (mmc->timing == MMC_TIMING_MMC_DDR52) ||
            (mmc->timing == MMC_TIMING_UHS_SDR50) ||
            (mmc->timing == MMC_TIMING_UHS_SDR104)||
            (mmc->timing == MMC_TIMING_UHS_DDR50) ||
            (mmc->timing == MMC_TIMING_UHS_SDR25) ||
            (mmc->timing == MMC_TIMING_MMC_HS)    ||
            (mmc->timing == MMC_TIMING_SD_HS) ) {

            soc_mmc_enable_tuning(index, 0);
            soc_mmc_set_rx_phase(index);
            soc_mmc_set_tx_phase(index);

            mmc_hal_set_high_speed_enable(index, 1);
        } else {
            soc_mmc_enable_tuning(index, 1);
            mmc_hal_set_high_speed_enable(index, 0);
        }

        /* set drviver type:预留未实现 */

        switch (mmc->timing) {
        case MMC_TIMING_MMC_HS200:
            msc_set_bits_16(index, MSC_HOST_CONTROL2, MSC_HOST_CTRL_2_tuned_clk, 1);
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_EMMC_HS200);
            break;

        case MMC_TIMING_MMC_DDR52:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_EMMC_HS_DDR);
            break;

        case MMC_TIMING_MMC_HS:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_EMMC_HS_SDR);
            break;

        case MMC_TIMING_UHS_SDR104:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_UHS_SDR104);
            break;

        case MMC_TIMING_UHS_SDR12:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_UHS_SDR12);
            break;

        case MMC_TIMING_UHS_SDR25:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_UHS_SDR25);
            break;

        case MMC_TIMING_UHS_SDR50:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_UHS_SDR50);
            break;

        case MMC_TIMING_UHS_DDR50:
            mmc_hal_set_uhs_signaling(index, MSC_CTRL_UHS_DDR50);
            break;
        }

        mmc_hal_clock_control_enable(index, 1);

        //soc_mmc_stop_clock(index);
    } else {

        soc_mmc_clk_ctrl(host, 0);
    }

    MSC_DBG("MSC%d: clk_want=%d, clk_set=%ld, bus_width=%d\n",
            index, mmc->clock, clk_get_rate(host->clk), 1 << mmc->bus_width);
}


static int soc_mmc_check_cmd_data_line(struct jz_mmc_host *host)
{
    struct mmc_cmd* cmd = host->cmd;
    struct mmc_data* data = host->data;

    int index = host->index;
    int timeout = 100 * 1000;
    int ret = 0;

    uint32_t mask = MSC_PRESENT_CMD_INHIBIT;

    if (data != NULL || (cmd->resp_type & MMC_RSP_BUSY))
        mask |= MSC_PRESENT_DATA_INHIBIT;

    /*
     * We shouldn't wait for data inihibit for stop commands, even
     * though they might use busy signaling
     */
    if (cmd->opcode == MMC_STOP_TRANSMISSION)
        mask &= ~MSC_PRESENT_DATA_INHIBIT;

    while ( (mmc_hal_get_presend_status(index) & mask) && --timeout)
        udelay(1);

    if (!timeout) {
        printf("MSC%d: Controller never released inhibit bit(s).\n", index);
        ret = -EIO;
    }

    return ret;
}

static int soc_mmc_check_error(struct jz_mmc_host *host)
{
    int index = host->index;
    uint16_t error_status = mmc_hal_get_error_int_status(index);

    if (error_status & MSC_ERROR_STATUS_ALL_ERROR) {
        printf("MSC%d:CMD%d error status:0x%08x\n", host->index, host->cmd->opcode, error_status);
        host->cmd->error = -EIO;

        if (host->data)
            host->data->error = -EIO;

        return -1;
    }

    return 0;
}

static int soc_mmc_wait_read_buffer_ready(struct jz_mmc_host *host)
{
    int index = host->index;
    uint16_t normal_status;
    int timeout = 100 *1000;

    do {
        normal_status = mmc_hal_get_normal_int_status(index);
        udelay(1);
    } while ( !(normal_status & MSC_NORMAL_INT_READ_READY) && --timeout);

    if (!timeout) {
        soc_mmc_check_error(host);
        printf("MSC%d:CMD%d wait read buffer timeout. status:0x%08x\n", host->index, host->cmd->opcode, normal_status);
        return -1;
    }

    normal_status |= MSC_NORMAL_INT_READ_READY;
    mmc_hal_clear_normal_int_status(index, normal_status);

    return 0;
}

static int soc_mmc_wait_write_buffer_ready(struct jz_mmc_host *host)
{
    int index = host->index;
    uint16_t normal_status;
    int timeout = 100 * 1000;

    do {
        normal_status = mmc_hal_get_normal_int_status(index);
        udelay(1);
    } while ( !(normal_status & MSC_NORMAL_INT_WRITE_READY) && --timeout);

    if (!timeout) {
        soc_mmc_check_error(host);
        printf("MSC%d:CMD%d wait write buffer timeout. status:0x%08x\n", host->index, host->cmd->opcode, normal_status);
        return -1;
    }

    normal_status |= MSC_NORMAL_INT_WRITE_READY;
    mmc_hal_clear_normal_int_status(index, normal_status);

    return 0;
}

static int soc_mmc_wait_xfer_complete(struct jz_mmc_host *host)
{
    int index = host->index;
    uint16_t normal_status;
    int timeout = 100 *1000;

    do {
        normal_status = mmc_hal_get_normal_int_status(index);
        udelay(1);
    } while ( !(normal_status & MSC_NORMAL_INT_XFER_COMPLETE) && --timeout);

    if (!timeout) {
        soc_mmc_check_error(host);
        printf("MSC%d:CMD%d wait xfer complete timeout. status:0x%08x\n", index, host->cmd->opcode, normal_status);
        return -1;
    }

    normal_status |= MSC_NORMAL_INT_XFER_COMPLETE;
    mmc_hal_clear_normal_int_status(index, normal_status);

    return 0;
}


static int soc_mmc_read_poll(struct jz_mmc_host *host)
{
    struct mmc_data *data = host->data;
    int index = host->index;
    int ret;
    int i;

    uint32_t data_size = data->blksz * data->blocks;
    uint32_t data_count = data_size / 4;
    uint32_t *buffer = (uint32_t *)(data->dest);
    uint32_t status = 0;

    /* 状态检查 */
    ret = soc_mmc_wait_read_buffer_ready(host);
    if (ret) {
        printf("MSC%d:CMD%d wait read buffer error\n", host->index, host->cmd->opcode);
        return ret;
    }

    unsigned int mask = MSC_PRESENT_AVAILABLE_READ;
    for (i = 0; i < data_count; i++) {
        do {
            status = mmc_hal_get_presend_status(index);
        } while (!(status & mask));

        *buffer++ = mmc_hal_read_bufferdata(index);
        data->bytes_xfered += 4;
    }

    /*
     * These codes handle the last 1, 2 or 3 bytes transfer.
     */
    if (data_size & 3) {
        do {
            status = mmc_hal_get_presend_status(index);
        } while (!(status & mask));

        uint32_t num = data_size & 3;
        uint32_t value = mmc_hal_read_bufferdata(index);
        uint8_t *p = (uint8_t *)buffer;
        while (num--) {
            *p++ = value;
            value >>= 8;
        }

        data->bytes_xfered += num;
    }

    /* Check Transfer DONE */
    ret = soc_mmc_wait_xfer_complete(host);
    if (ret) {
        data->bytes_xfered = 0;
        printf("MSC%d:CMD%d read wait xfer complete error\n", host->index, host->cmd->opcode);
        return ret;
    }

    return 0;
}

static int soc_mmc_write_poll(struct jz_mmc_host *host)
{
    struct mmc_data *data = host->data;
    int index = host->index;
    int ret;
    int i;

    uint32_t data_size = data->blksz * data->blocks;
    uint32_t data_count = data_size / 4;
    uint32_t *buffer = (uint32_t *)(data->dest);
    uint32_t status = 0;

    /* 状态检查 */
    ret = soc_mmc_wait_write_buffer_ready(host);
    if (ret) {
        printf("MSC%d:CMD%d wait write buffer error\n", host->index, host->cmd->opcode);
        return ret;
    }

    unsigned int mask = MSC_PRESENT_AVAILABLE_WRITE;
    for (i = 0; i < data_count; i++) {
        do {
            status = mmc_hal_get_presend_status(index);
        } while (!(status & mask));

        mmc_hal_write_bufferdata(index, *buffer++);
        data->bytes_xfered += 4;
    }

    /*
     * These codes handle the last 1, 2 or 3 bytes transfer.
     */
    if (data_size & 3) {
        uint32_t num = data_size & 3;
        uint32_t value = 0;
        uint8_t *p = (uint8_t *)buffer;

        for (i = 0; i < num; i++) {
            value |= *p++ << (8 * i);
        }

        do {
            status = mmc_hal_get_presend_status(index);
        } while (!(status & mask));

        mmc_hal_write_bufferdata(index, value);
        data->bytes_xfered += num;
    }

    /* Check Transfer DONE */
    ret = soc_mmc_wait_xfer_complete(host);
    if (ret) {
        data->bytes_xfered = 0;
        printf("MSC%d:CMD%d write wait xfer complete error\n", host->index, host->cmd->opcode);
        return ret;
    }

    return 0;
}

static int soc_mmc_get_response(struct jz_mmc_host *host)
{
    int index = host->index;
    uint32_t resp[4];

    resp[0] = msc_read_reg_32(index, MSC_RESPONSE_01);
    resp[1] = msc_read_reg_32(index, MSC_RESPONSE_23);
    resp[2] = msc_read_reg_32(index, MSC_RESPONSE_45);
    resp[3] = msc_read_reg_32(index, MSC_RESPONSE_67);

    if (host->cmd->resp_type & MMC_RSP_PRESENT) {
        if (host->cmd->resp_type & MMC_RSP_136) {
            /*
             * MMC_RSP_R2
             * CRC is stripped so we need to do some shifting.
             */
            host->cmd->resp[0] = resp[3] << 8 | ((resp[2] >> 24) & 0xFF);
            host->cmd->resp[1] = resp[2] << 8 | ((resp[1] >> 24) & 0xFF);
            host->cmd->resp[2] = resp[1] << 8 | ((resp[0] >> 24) & 0xFF);
            host->cmd->resp[3] = resp[0] << 8;
        } else {
            host->cmd->resp[0] = resp[0];
        }
    } else {
        /*
         * MMC_RSP_NONE : Nothing todo
         */
    }

    MSC_DBG("MSC%d Dump CMD%d response\n", host->index, host->cmd->opcode);
    MSC_DBG("resp[0] =0x%x\n", host->cmd->resp[0]);
    MSC_DBG("resp[1] =0x%x\n", host->cmd->resp[1]);
    MSC_DBG("resp[2] =0x%x\n", host->cmd->resp[2]);
    MSC_DBG("resp[3] =0x%x\n", host->cmd->resp[3]);

    MSC_DBG("Dump CMD response reg\n", host->cmd->opcode);
    MSC_DBG("resp[01] =0x%x\n", resp[0]);
    MSC_DBG("resp[23] =0x%x\n", resp[1]);
    MSC_DBG("resp[45] =0x%x\n", resp[2]);
    MSC_DBG("resp[67] =0x%x\n", resp[3]);

    return 0;
}

static int soc_mmc_wait_cmd_response_poll(struct jz_mmc_host *host)
{
    int timeout = 100 * 1000;

    while ( !(mmc_hal_is_response_complete(host->index)) && --timeout ) {
        udelay(10);
        MSC_DBG("MSC%d: CMD%d wait response[0x%x] flag = 0x%x timeout=%d\n",     \
        host->index,                            \
        host->cmd->opcode,                      \
        MSC_NORMAL_INT_STATUS,                  \
        msc_read_reg_16(host->index, MSC_NORMAL_INT_STATUS), \
        timeout);
    }

    if (!timeout) {
        printf("MSC%d: CMD%d wait response timeout\n", host->index, host->cmd->opcode);
        host->cmd->error = -ETIMEDOUT;
        return -EAGAIN;
    }

    mmc_hal_clear_int_flag_response_complete(host->index);

    soc_mmc_get_response(host);
    return 0;
}

static int soc_mmc_send_command_with_response_poll(struct jz_mmc_host *host, uint32_t command)
{
    struct mmc_cmd* cmd = host->cmd;
    int index = host->index;

    /* 开始发送命令 */
    mmc_hal_set_argument(index, cmd->arg);
    mmc_hal_set_command(index, command);

    return soc_mmc_wait_cmd_response_poll(host);
}

static int soc_mmc_send_command_with_response_sdma(struct jz_mmc_host *host, uint32_t command)
{
    struct mmc_cmd* cmd = host->cmd;
    struct mmc_data *data = host->data;
    int index = host->index;

    uint32_t int_mask = MSC_NORMAL_ERROR_CMD_MASK;

    os_enter_critical();
    soc_mmc_set_pending(host, EVENT_CMD_RESPONSE);
    if (data ||
        cmd->opcode == MMC_SEND_TUNING_BLOCK ||
        cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200) {

        /* hs200 tuning enable read_ready */
        int_mask |= MSC_NORMAL_INT_READ_READY | MSC_NORMAL_ERROR_DATA_MASK;

        soc_mmc_set_pending(host, EVENT_DATA_TRAN_DONE);
    }
    soc_mmc_enable_irq(index, int_mask);

    int timeout_ms = 500;

    /* 开始发送命令 */
    mmc_hal_set_argument(index, cmd->arg);
    mmc_hal_set_command(index, command);

    if (critical_thread_cond_wait_timeout(&host->thread_cond, timeout_ms)) {
        os_exit_critical();
        host->cmd->error = -ETIMEDOUT;
        MSC_WARN("MSC%d:CMD:%d soc mmc wait command response timeout\n", index, host->cmd->opcode);
        return -EAGAIN;
    }

    os_exit_critical();
    soc_mmc_get_response(host);

    return 0;
}

static int soc_mmc_submit_dma(struct jz_mmc_host *host, struct mmc_data *data)
{
    uint32_t dma_addr = 0;
    uint32_t addr_start;
    uint32_t buffer_size = data->blksz * data->blocks;

    if (data->flags & MMC_DATA_WRITE) {
        /* direction : DMA_TO_DEVICE */
        addr_start = (uint32_t)(data->src);
        dma_addr = (uint32_t)(data->src);
    } else if (data->flags & MMC_DATA_READ) {
        /* direction : DMA_FROM_DEVICE */
        addr_start = (uint32_t)(data->dest);
        dma_addr = (uint32_t)(data->dest);
    } else {
        printf("data transfer direction invaild\n");
        hang();
    }

    flush_dcache_force((unsigned long)addr_start, buffer_size);

    mmc_hal_set_sdma_address(host->index, virt_to_phys(dma_addr));
    return 0;
}

static int soc_mmc_send_data_prepare(struct jz_mmc_host *host, struct mmc_data *data)
{
    int index = host->index;
    unsigned long transfer_mode;

    transfer_mode = mmc_hal_get_transfer_mode(host->index);
    if (data == NULL) {
        /* clear Auto CMD settings for no data CMDs */
        set_bit_field(&transfer_mode, MSC_TRANSFER_auto_cmd_enable, 0);
        goto out;
    }

    /* 设置最大超时时间 */
    mmc_hal_set_data_timeout(index, 0xE);

    /* number of blocks */
    mmc_hal_set_block_count(index, data->blocks);

    /* number of bytes in a block */
    mmc_hal_set_block_size(index, data->blksz);

    /* 设置SDMA缓存边界 */
    mmc_hal_set_sdma_buffer_boundary(index, MSC_DEFAULT_BOUNDARY_ARG);

    /* 多次传输 */
    if ( host->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK    \
        || host->cmd->opcode == MMC_READ_MULTIPLE_BLOCK   \
        || data->blocks > 1) {
        set_bit_field(&transfer_mode, MSC_TRANSFER_multi_single_select, 1);
        set_bit_field(&transfer_mode, MSC_TRANSFER_block_count_enable, 1);
    } else {
        set_bit_field(&transfer_mode, MSC_TRANSFER_multi_single_select, 0);
        set_bit_field(&transfer_mode, MSC_TRANSFER_block_count_enable, 1);  /* ADMA, this should be 0 */
    }

    if ( host->cmd->opcode == MMC_SET_BLOCK_COUNT) {
        /* Auto CMD23 */
        set_bit_field(&transfer_mode, MSC_TRANSFER_auto_cmd_enable, 2);
    } else {
        set_bit_field(&transfer_mode, MSC_TRANSFER_auto_cmd_enable, 0);
    }


    data->bytes_xfered = 0;  /* 统计传输完成字节数据, 换算DMA地址 */

    if (data->flags & MMC_DATA_WRITE) {
        set_bit_field(&transfer_mode, MSC_TRANSFER_data_direction, 0);
    } else if (data->flags & MMC_DATA_READ) {
        set_bit_field(&transfer_mode, MSC_TRANSFER_data_direction, 1);
    } else {
        printf("MSC%d:data transfer direction invaild\n", index);
        hang();
    }


    if ( soc_mmc_is_poll_mode(host) ) {
        set_bit_field(&transfer_mode, MSC_TRANSFER_dma_enable, 0);
    } else {
        /* SDMA设置 */
        set_bit_field(&transfer_mode, MSC_TRANSFER_dma_enable, 1);
        mmc_hal_set_dma_mode(index, DMA_MODE_SDMA);
        soc_mmc_submit_dma(host, data);
    }

out:
    mmc_hal_set_transfer_mode(host->index, transfer_mode);
    return 0;
}

static int soc_mmc_data_start_sdma(struct jz_mmc_host *host)
{
    struct mmc_data *data = host->data;
    int index = host->index;
    uint32_t int_mask = 0;
    int timeout_ms = 20 * data->blocks + 500;

    int_mask = MSC_NORMAL_ERROR_DATA_MASK;

    /* 传输数据在prepare已经准备好,判断数据是否传输完成 */
    os_enter_critical();
    if (!soc_mmc_test_pending(host, EVENT_DATA_TRAN_DONE))
        goto out;

    soc_mmc_set_pending(host, EVENT_DATA_TRAN_DONE);
    soc_mmc_enable_irq(index, int_mask);

    if (critical_thread_cond_wait_timeout(&host->thread_cond, timeout_ms)) {
        host->data->error = -ETIMEDOUT;
        printf("MSC%d:CMD%d soc mmc wait data response timeout\n", index, host->cmd->opcode);
    }

out:
    os_exit_critical();

   if (host->data->error) {
        host->data->bytes_xfered = 0; /* 出错清零 */
        printf("MSC%d:CMD%d soc mmc wait date tran error:%d\n", index, host->cmd->opcode, host->data->error);
        return -1;
    }

    data->bytes_xfered = data->blksz * data->blocks;

    if (data->flags & MMC_DATA_READ) {
        invalidate_dcache_force((unsigned long)(data->dest), data->bytes_xfered);
    }

    return 0;
}

static int soc_mmc_send_data(struct jz_mmc_host *host)
{
    struct mmc_data *data = host->data;
    int ret = 0;

    if (data == NULL)
        return 0;

    if ( soc_mmc_is_poll_mode(host) ) {

        /* poll Mode */
        if (data->flags & MMC_DATA_WRITE)
            ret = soc_mmc_write_poll(host);
        else if (data->flags & MMC_DATA_READ)
            ret = soc_mmc_read_poll(host);
        else {
            printf("MSC%d:data transfer direction invaild\n", host->index);
            hang();
        }

        /* end of is_poll_mode */
    } else {
        /* DMA Mode */
        ret = soc_mmc_data_start_sdma(host);
    }

    return ret;
}

static int soc_mmc_send_command_with_response(struct jz_mmc_host *host)
{
    struct mmc_cmd* cmd = host->cmd;
    int index = host->index;
    unsigned long command = 0;

    if ( (cmd->resp_type & MMC_RSP_136) && (cmd->resp_type & MMC_RSP_BUSY)) {
        printf("MSC%d Unsupport response type(0x%x)\n", index, cmd->resp_type);
        cmd->error = -EINVAL;
        return -1;
    }

    switch (mmc_resp_type(host->cmd) ) {
    case MMC_RSP_NONE:
        set_bit_field(&command, MSC_COMMAND_cmd_crc_check, 0);
        set_bit_field(&command, MSC_COMMAND_cmd_index_crc_check, 0);
        set_bit_field(&command, MSC_COMMAND_response_type, MSC_CMD_RESP_TYPE_NONE);
        break;
    case MMC_RSP_R1:
        set_bit_field(&command, MSC_COMMAND_cmd_crc_check, 1);
        set_bit_field(&command, MSC_COMMAND_cmd_index_crc_check, 1);
        set_bit_field(&command, MSC_COMMAND_response_type, MSC_CMD_RESP_TYPE_SHORT);
        break;
    case MMC_RSP_R1B:
        set_bit_field(&command, MSC_COMMAND_cmd_crc_check, 1);
        set_bit_field(&command, MSC_COMMAND_cmd_index_crc_check, 1);
        set_bit_field(&command, MSC_COMMAND_response_type, MSC_CMD_RESP_TYPE_SHORT_BUSY);
        break;
    case MMC_RSP_R2:
        set_bit_field(&command, MSC_COMMAND_cmd_crc_check, 0);
        set_bit_field(&command, MSC_COMMAND_cmd_index_crc_check, 0);
        set_bit_field(&command, MSC_COMMAND_response_type, MSC_CMD_RESP_TYPE_LONG);
        break;
    case MMC_RSP_R3:
        set_bit_field(&command, MSC_COMMAND_cmd_crc_check, 0);
        set_bit_field(&command, MSC_COMMAND_cmd_index_crc_check, 0);
        set_bit_field(&command, MSC_COMMAND_response_type, MSC_CMD_RESP_TYPE_SHORT);
        break;
    default:
        break;
    }

    if (host->data ||
        cmd->opcode == MMC_SEND_TUNING_BLOCK ||
        cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200) {
        set_bit_field(&command, MSC_COMMAND_data_presend_sel, 1);
    }

    /* 设置命令索引号 */
    set_bit_field(&command, MSC_COMMAND_cmd_index, cmd->opcode);

    if ( soc_mmc_is_poll_mode(host) ) {
        soc_mmc_send_command_with_response_poll(host, command);
    } else {
        /* DMA模式 */
        soc_mmc_send_command_with_response_sdma(host, command) ;
    }

    if (host->cmd->error) {
        MSC_WARN("MSC%d:CMD:%d soc mmc wait command response error(%d)\n",
                index, host->cmd->opcode, host->cmd->error);
        return -1;
    }

    return 0;
}


static int soc_mmc_send_cmd_data(struct mmc_host *mmc, struct mmc_cmd* cmd, struct mmc_data* data)
{
    struct jz_mmc_host *host = mmc_get_privdata(mmc);
    int ret;

    mutex_lock(&host->mutex);
    MSC_DBG("MSC%d:CMD%d argument=0x%08x\n", host->index, cmd->opcode, cmd->arg);

    host->cmd = cmd;
    host->data = data;

    ret = soc_mmc_check_cmd_data_line(host);
    if (ret < 0) {
        printf("MSC%d Controller is busy.\n", host->index);
        goto unlock;
    }

    cmd->error = 0;

    /*
     * prepare data
     */
    soc_mmc_send_data_prepare(host, data);

    /*
     * send commmand & wait response
     */
    ret = soc_mmc_send_command_with_response(host);
    if (ret < 0) {
        MSC_WARN("MSC%d:send command failed\n", host->index);
        goto unlock;
    }

    /*
     * send data
     */
    soc_mmc_send_data(host);

unlock:
    /* 是否需要reset  cmd/data line */
    if (data && host->data->error)
        soc_mmc_controller_reset_line(host->index);

    if (host->cmd->error)
        soc_mmc_controller_reset_line(host->index);

    mmc_hal_clear_all_int_status(host->index, -1);

    mutex_unlock(&host->mutex);
    return ret;
}


static int soc_mmc_get_card_status(struct mmc_host *mmc)
{
    struct jz_mmc_host *host = mmc_get_privdata(mmc);
    struct mmc_devices_config *device = host->device_config;

    if (device && device->device_card_change)
        return device->device_card_change();

    return 0;
}

static int soc_mmc_execute_tuning(struct mmc_host *mmc, int opcode)
{
    struct jz_mmc_host *host = mmc_get_privdata(mmc);
    int index = host->index;
    int tuning_loop_counter = 32;

    if ( soc_mmc_is_poll_mode(host) ) {
        printf("Device in HS200 mode host contoller change [poll mode] to [DMA mode]\n");
        soc_mmc_poll_mode_enable(host, 0);
    }

    /*
     * As per the Host Controller spec v3.00, tuning command
     * generates Buffer Read Ready interrupt, so enable that.
     *
     * Note: The spec clearly says that when tuning sequence
     * is being performed, the controller does not generate
     * interrupts other than Buffer Read Ready interrupt. But
     * to make sure we don't hit a controller bug, we _only_
     * enable Buffer Read Ready interrupt here.
     */
    if (opcode == MMC_SEND_TUNING_BLOCK_HS200) {
        if (mmc->bus_width == MMC_BUS_WIDTH_8) {
            /* number of bytes in a block */
            mmc_hal_set_block_size(index, 128);

            /* 设置SDMA缓存边界 */
            mmc_hal_set_sdma_buffer_boundary(index, MSC_DEFAULT_BOUNDARY_ARG);
        } else {
            /* number of bytes in a block */
            mmc_hal_set_block_size(index, 64);

            /* 设置SDMA缓存边界 */
            mmc_hal_set_sdma_buffer_boundary(index, MSC_DEFAULT_BOUNDARY_ARG);
        }

    } else {

        /* number of bytes in a block */
        mmc_hal_set_block_size(index, 64);

        /* 设置SDMA缓存边界 */
        mmc_hal_set_sdma_buffer_boundary(index, MSC_DEFAULT_BOUNDARY_ARG);
    }

    mmc_hal_set_exec_tuning(index);
    mmc_hal_set_data_transfer_direction_read(index);

    struct mmc_cmd tuning_cmd = {0};
    int ret = 0;
    do {
        tuning_loop_counter--;

        tuning_cmd.opcode          = opcode;
        tuning_cmd.arg             = 0;
        tuning_cmd.resp_type       = MMC_RSP_R1;
        ret = soc_mmc_send_cmd_data(mmc, &tuning_cmd, NULL);
        if (ret < 0) {
            printf("MSC%d send execute tuning command(%d) left retry %d\n", index, tuning_cmd.opcode, tuning_loop_counter);
            continue;
        }

        os_enter_critical();
        if (soc_mmc_test_pending(host, EVENT_DATA_TRAN_DONE)) {
            if (critical_thread_cond_wait_timeout(&host->thread_cond, 1000)) {
                printf("MSC%d:CMD%d soc mmc wait data response timeout\n", index, host->cmd->opcode);
            }
        }
        os_exit_critical();

    } while(mmc_hal_get_exec_tuning(index) && tuning_loop_counter > 0);

    if (tuning_loop_counter > 0)
        ret = 0;
    else
        ret = -EAGAIN;

    return ret;
}

/*
 * MSC Control API
 */
static int soc_mmc_controller_init(int index, void *data)
{
    struct jz_mmc_host *host = &jz_mmc_host[index];
    char clk_gate_name[16];
    char clkname[16];
    int ret = 0;

    /*
     * mmc clock name
     */
    memset(clkname, 0x00, sizeof(clkname));
    memset(clk_gate_name, 0x00, sizeof(clk_gate_name));

    sprintf(clk_gate_name, "gate_msc%d", host->index);
    host->clk_gate = clk_get(clk_gate_name);
    assert(host->clk_gate != NULL);

    sprintf(clkname, "cgu_msc%d", host->index);
    host->clk = clk_get(clkname);
    assert(host->clk != NULL);

    host->mmc = malloc(sizeof(struct mmc_host));
    assert(host->mmc);

    memset(host->mmc, 0x00, sizeof(struct mmc_host));
    host->mmc->index = host->index;
    sprintf(host->mmc->name, "msc%d", host->index);
    mmc_set_privdata(host->mmc, host);

    /*
     * Initialization MSC gpio
     */
    soc_mmc_gpio_init(index);

    /*
     * Transfer Default: DMA Mode
     */
    soc_mmc_poll_mode_enable(host, 0);

    /* cpm msc clk默认使能无需设置，否则本次使能时候检查busy位，导致死循环 */
    soc_mmc_clk_ctrl(host, 1);

    soc_mmc_clk_init_stable(index);

    host->mmc->version = mmc_hal_get_host_sepc_version(index);

    /*
     * SD Controller版本大于v3.00时, 如果Host负载电流超过150mA， SD驱动应该设置XPC为1
     */
    host->mmc->max_current_330 = mmc_hal_get_max_current_330(index);
    host->mmc->max_current_300 = mmc_hal_get_max_current_300(index);
    host->mmc->max_current_180 = mmc_hal_get_max_current_180(index);


    host->flag |= SOC_MSC_SDIO_THREAD_IRQ;
    thread_waiter_init(&host->dev_waiter);
    thread_create(host->thrad_irq_name, 4096, soc_mmc_controller_thread_irq, (void *)host);

    request_irq(host->irq, 0, soc_mmc_interrupt_handler, host->irq_name, (void *)host);

    /* 停止clk,降低不必要功耗 */
    soc_mmc_clk_ctrl(host, 0);

    mmc_core_init(host->mmc);

    spin_lock_init(&host->dev_spin);
    mutex_init(&host->mutex);
    critical_thread_cond_init(&host->thread_cond);

    /* 备份SPL传递进来的参数 */
    struct card_info_params *card_params = NULL;
    struct card_info_params *_pdata = NULL;
    if ((unsigned long)data > CKSEG0 && (unsigned long)data < CKSEG2)
        _pdata = (struct card_info_params *)data;

    /* 启动控制器参数才有效 */
    if (_pdata && _pdata->host_index == index) {
        card_params = malloc(sizeof(struct card_info_params));
        if (card_params)
            memcpy(card_params, data, sizeof(struct card_info_params));
        else
            printf("card info params malloc failed\n");
    }

    /*
     * MSC OPS
     */
    host->mmc->card_params    = card_params;
    host->mmc->set_ios        = soc_mmc_set_ios;
    host->mmc->send_cmd_data  = soc_mmc_send_cmd_data;
    host->mmc->get_card_stauts= soc_mmc_get_card_status;
    host->mmc->execute_tuning = soc_mmc_execute_tuning;
    host->mmc->enable_sdio_irq= soc_mmc_enable_sdio_irq;

    return ret;
}

/*
 * MSC Public API
 */
struct mmc_card *mmc_register_device(struct mmc_devices_config *device_config)
{
    assert_range(device_config->index, 0, 2);

    struct jz_mmc_host *host = &jz_mmc_host[device_config->index];
    struct mmc_card *card;

    if (!host->is_enable) {
        panic("MSC%d is not initialization\n", device_config->index);
        return NULL;
    }

    if (host->device_config)
        panic("MSC%d is working for Device:%s\n", host->index, device_config->name);

    int min_freq = 100 * 1000;
    int max_freq = host->max_freq;

    if (max_freq > X2000_MSC_MAX_FREQ)
        max_freq = X2000_MSC_MAX_FREQ;

    if (max_freq > device_config->max_freq)
        max_freq = device_config->max_freq;

    if (min_freq > max_freq)
        max_freq = min_freq;

    host->mmc->f_min    = min_freq;  /* 100K */
    host->mmc->f_max    = max_freq;  /* 200M */
    host->mmc->voltages = device_config->ocr_avail;
    host->mmc->capacity = device_config->capacity;
    host->mmc->capacity2= device_config->capacity2;

    /* mmc-sdio-irq.c不使用thread方式 */
    host->mmc->capacity2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

    host->mmc->max_req_size  = 512 * 1024;
    host->mmc->max_blk_size  = mmc_hal_get_host_capablites_max_block_length(device_config->index);
    host->mmc->max_blk_count = 65535;
    host->device_config = device_config;

    if (device_config->gpio && device_config->gpio->removal == MMC_DEVICE_NONREMOVABLE)
        host->mmc->capacity |=  MMC_CAP_NONREMOVABLE;

    if (device_config->device_init)
        device_config->device_init();

    mmc_detect_change(host->mmc);

    card = host->mmc->card;

    /* 未检测到Card */
    if (!card)
        host->device_config = NULL;

    return card;
}

int mmc_unregister_device(struct mmc_devices_config *device_config)
{
    struct jz_mmc_host *host = &jz_mmc_host[device_config->index];

    mmc_detect_change(host->mmc);

    //soc_mmc_clk_ctrl(host, 0);
    host->device_config = NULL;

    return 0;
}

int soc_mmc_init(void* data)
{
    if (jz_mmc_host[0].is_enable)
        soc_mmc_controller_init(0, data);

    if (jz_mmc_host[1].is_enable)
        soc_mmc_controller_init(1, data);

    if (jz_mmc_host[2].is_enable)
        soc_mmc_controller_init(2, data);

    return 0;
}
