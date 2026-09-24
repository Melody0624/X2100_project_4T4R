/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */
#include <stdio.h>
#include <common.h>
#include <driver/cache.h>

#include "mmc-core.h"
#include "mmc-sd.h"
#include "mmc-host.h"
#include "mmc-sdio.h"
#include "mmc-sdio-cis.h"

#ifdef MMC_DEBUG
#define MMC_DBG(...)                    printf("[SDIO] Debug:"), printf(__VA_ARGS__)
#define MMC_WARN(...)                   printf("[SDIO] Warn:"), printf(__VA_ARGS__)
#define MMC_ERR(...)                    printf("[SDIO] Error:"), printf(__VA_ARGS__)
#else
#define MMC_DBG(...)
#define MMC_WARN(...)
#define MMC_ERR(...)                    printf("[SDIO] Error:"), printf(__VA_ARGS__)
#endif

int mmc_send_cmd_data(struct mmc_host *mmc, struct mmc_cmd* cmd, struct mmc_data* data);
int mmc_select_card(struct mmc_card *card);

int mmc_app_set_bus_width(struct mmc_card *card, int width);
int mmc_sd_switch_hs(struct mmc_card *card);
uint32_t mmc_sd_get_max_clock(struct mmc_card *card);

/*
 * SDIO operations
 */
static int mmc_send_io_op_cond(struct mmc_host *mmc, uint32_t ocr, uint32_t *rocr)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;
    int i = 0;

    cmd.opcode = SD_IO_SEND_OP_COND;
    cmd.arg = ocr;
    cmd.resp_type = MMC_RSP_R4 | MMC_CMD_BCR;
    cmd.retries = 3;

    for (i = 0; i < 100; i++) {
        ret = mmc_send_cmd_data(mmc, &cmd, NULL);
        if (ret)
            break;

        if (ocr == 0)
            break;

        /* wait until reset completes */
        if (cmd.resp[0] & MMC_CARD_BUSY)
            break;

        ret = -ETIMEDOUT;
        mdelay(10);
    }

    if (rocr)
        *rocr = cmd.resp[0];

    return ret;
}

static int mmc_sdio_get_card_address(struct mmc_host *mmc, uint32_t *rca)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;

    cmd.opcode = MMC_SET_RELATIVE_ADDR;
    cmd.arg = 0;
    cmd.resp_type = MMC_RSP_R6 | MMC_CMD_BCR;
    cmd.retries = 3;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret)
        return ret;

    *rca = cmd.resp[0] >> 16;

    return ret;
}

static int mmc_io_rw_direct_host(struct mmc_host* mmc, int write, unsigned int fn,
        unsigned int addr, uint8_t in, uint8_t *out)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;

    assert(mmc);
    assert(fn <= SDIO_MAX_FUNCS);

    /* sanity check */
    if (addr & ~0x1FFFF)
        return -EINVAL;

    cmd.opcode = SD_IO_RW_DIRECT;
    cmd.arg = write ? 0x80000000 : 0x00000000;
    cmd.arg |= fn << 28;
    cmd.arg |= (write && out) ? 0x08000000 : 0x00000000;
    cmd.arg |= (addr << 9);
    cmd.arg |= in;
    cmd.resp_type = MMC_RSP_R5 | MMC_CMD_AC;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret)
        return ret;

    if (cmd.resp[0] & R5_ERROR) {
        return -EIO;
    } else if (cmd.resp[0] & R5_FUNCTION_NUMBER) {
        return -EINVAL;
    } else if (cmd.resp[0] & R5_OUT_OF_RANGE) {
        return -ERANGE;
    }

    if (out)
        *out = cmd.resp[0] & 0xFF;

    return 0;
}

int sdio_io_rw_direct(struct mmc_card *card, int write, uint32_t fn, uint32_t addr, uint8_t in, uint8_t *out)
{
    assert(card);
    return  mmc_io_rw_direct_host(card->host, write, fn, addr, in, out);
}

int sdio_io_rw_extended(struct mmc_card *card, int write, uint32_t fn,
    uint32_t addr, int incr_addr, uint8_t *buf, uint32_t blocks, uint32_t blksz)
{
    assert(card);

    struct mmc_host *mmc = card->host;
    struct mmc_cmd cmd = {0};
    struct mmc_data data = {0};

    assert(mmc);
    assert(fn <= SDIO_MAX_FUNCS);
    assert(blksz != 0);

    /* sanity check */
    if (addr & ~0x1FFFF)
        return -EINVAL;

    /* cmd */
    cmd.opcode      = SD_IO_RW_EXTENDED;
    cmd.arg         = write ? 0x80000000 : 0x00000000;
    cmd.arg         |= fn << 28;
    cmd.arg         |= incr_addr ? 0x04000000 : 0x00000000;
    cmd.arg         |= addr << 9;
    if (blocks == 0)
        cmd.arg     |= (blksz == 512) ? 0 : blksz;  /* byte mode */
    else
        cmd.arg     |= 0x08000000 | blocks;         /* block mode */

    cmd.resp_type   = MMC_RSP_R5 | MMC_CMD_ADTC;

    /* data */
    /* Code in host drivers/fwk assumes that "blocks" always is >=1 */
    data.blksz      = blksz;
    data.blocks     = blocks ? blocks : 1;

    if (write) {
        data.flags  = MMC_DATA_WRITE;
        data.src    = (char *)buf;
    } else {
        data.flags  = MMC_DATA_READ;
        data.dest   = (char *)buf;
    }

    mmc_send_cmd_data(mmc, &cmd, &data);

    if (cmd.error)
        return cmd.error;

    if (data.error)
        return data.error;

    return 0;
}


static int sdio_read_cccr(struct mmc_card *card)
{
    int ret;
    uint32_t cccr_version;
    uint8_t data;
    uint8_t speed;

    memset(&card->cccr, 0x00, sizeof(struct sdio_cccr));

    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_CCCR, 0, &data);
    if (ret)
        goto out;

    cccr_version = data & 0x0F;

    if (cccr_version > SDIO_CCCR_REV_3_00) {
        MMC_ERR("%s: unrecignised CCCR structure version:%d\n", card->host->name, cccr_version);
        return -EINVAL;
    }

    card->cccr.sd_version = (data & 0xF0) >> 4;

    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_CAPS, 0, &data);
    if (ret)
        goto out;

    if (data & SDIO_CCCR_CAP_SMB)
        card->cccr.multi_block = 1;
    if (data & SDIO_CCCR_CAP_LSC)
        card->cccr.low_speed = 1;
    if (data & SDIO_CCCR_CAP_4BLS)
        card->cccr.wide_bus = 1;

    if (cccr_version >= SDIO_CCCR_REV_1_10) {
        ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_POWER, 0, &data);
        if (ret)
            goto out;

        if (data & SDIO_POWER_SMPC)
            card->cccr.high_power = 1;
    }

    if (cccr_version >= SDIO_CCCR_REV_1_20) {
        ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_SPEED, 0, &speed);
        if (ret)
            goto out;

        card->scr.sda_spec3 = 0;
        card->sw_caps.sd3_bus_mode = 0;
        card->sw_caps.sd3_drv_type = 0;

        if (cccr_version > SDIO_CCCR_REV_3_00 && 0) {
            /* 暂不处理 UHS模式 */
            //card->scr.sda_spec3 = 1;
            //ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_UHS, 0, &data);
            //card->sw_caps.sd3_bus_mode |= SD_MODE_UHS_DDR50;
            //card->sw_caps.sd3_bus_mode |= SD_MODE_UHS_SDR50;
            //card->sw_caps.sd3_bus_mode |= SD_MODE_UHS_SDR104;
            //ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_DRIVE_STRENGTH, 0, &data);
            //card->sw_caps.sd3_drv_type |= SD_DRIVER_TYPE_A;
            //card->sw_caps.sd3_drv_type |= SD_DRIVER_TYPE_C;
            //card->sw_caps.sd3_drv_type |= SD_DRIVER_TYPE_D;
        }

        /* if no uhs mode ensure we check for high speed */
        if (!card->sw_caps.sd3_bus_mode) {
            if (speed & SDIO_SPEED_SHS) {
                card->cccr.high_speed = 1;
                card->sw_caps.hs_max_dtr = 50 * 1000000;
            } else {
                card->cccr.high_speed = 0;
                card->sw_caps.hs_max_dtr = 25 * 1000000;
            }
        }
    }

out:
    return ret;
}

static int sdio_enable_wide(struct mmc_card *card)
{
    int ret;
    uint8_t ctrl;

    if (!(card->host->capacity & MMC_CAP_4_BIT_DATA))
        return 0;

    if (card->cccr.low_speed && !card->cccr.wide_bus)
        return 0;

    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_IF, 0, &ctrl);
    if (ret)
        return ret;

    if ( (ctrl & SDIO_BUS_WIDTH_MASK) == SDIO_BUS_WIDTH_RESERVED)
        printf("%s: SDIO_CCCR_IF is invalid: 0x%02x\n", card->host->name, ctrl);

    /* set as 4-bit bus width */
    ctrl &= ~SDIO_BUS_WIDTH_MASK;
    ctrl |= SDIO_BUS_WIDTH_4BIT;

    ret = sdio_io_rw_direct(card, 1, 0, SDIO_CCCR_IF, ctrl, NULL);
    if (ret)
        return ret;

    return 1;
}

/*
 * Devices that remain active during a system suspend are
 * put back into 1-bit mode.
 */
__attribute__((__unused__)) static int sdio_disable_wide(struct mmc_card *card)
{
    int ret;
    uint8_t ctrl;

    if (!(card->host->capacity & MMC_CAP_4_BIT_DATA))
        return 0;

    if (card->cccr.low_speed && !card->cccr.wide_bus)
        return 0;

    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_IF, 0, &ctrl);
    if (ret)
        return ret;

    if (!(ctrl & SDIO_BUS_WIDTH_4BIT))
        return 0;

    ctrl &= ~SDIO_BUS_WIDTH_4BIT;
    ctrl |= SDIO_BUS_ASYNC_INT;

    ret = sdio_io_rw_direct(card, 1, 0, SDIO_CCCR_IF, ctrl, NULL);
    if (ret)
        return ret;

    mmc_set_bus_width(card->host, MMC_BUS_WIDTH_1);

    return 0;
}

/*
 * If desired, disconnect the pull-up resistor on CD/DAT[3] (pin 1)
 * of the card. This may be required on certain setups of boards,
 * controllers and embedded sdio device which do not need the card's
 * pull-up. As a result, card detection is disabled and power is saved.
 */
static int sdio_disable_cd(struct mmc_card *card)
{
    int ret;
    uint8_t ctrl;

    if (!mmc_card_disable_cd(card))
        return 0;

    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_IF, 0, &ctrl);
    if (ret)
        return ret;

    ctrl |= SDIO_BUS_CD_DISABLE;

    ret = sdio_io_rw_direct(card, 1, 0, SDIO_CCCR_IF, ctrl, NULL);

    return ret;
}

int sdio_reset(struct mmc_host *mmc)
{
    int ret;
    uint8_t abort;

    /*
     * SDIO Simplified Specification V2.0, 4.4 Reset for SDIO
     */
    ret = mmc_io_rw_direct_host(mmc, 0, 0, SDIO_CCCR_ABORT, 0, &abort);
    if (ret)
        abort = 0x08;
    else
        abort |= 0x08;

    ret = mmc_io_rw_direct_host(mmc, 1, 0, SDIO_CCCR_ABORT, abort, NULL);

    return ret;
}

int mmc_send_if_cond(struct mmc_host *mmc, uint32_t ocr)
{
    static const uint8_t test_pattern = 0xAA;
    struct mmc_cmd cmd = {0};
    uint8_t result_pattern;
    int ret;

    /*
     * To support SD 2.0 cards, we must always invoke SD_SEND_IF_COND
     * before SD_APP_OP_COND. This command will harmlessly fail for
     * SD 1.0 cards.
     */
    cmd.opcode = SD_SEND_IF_COND;
    cmd.arg = ((ocr & 0xFF8000) != 0) << 8 | test_pattern;
    cmd.resp_type = MMC_RSP_R7 | MMC_CMD_BCR;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret)
        return ret;

    result_pattern = cmd.resp[0] & 0xFF;

    if (result_pattern != test_pattern)
        return -EIO;

    return 0;
}

static int sdio_enable_4bit_bus(struct mmc_card *card)
{
    int ret;

    if (card->type == MMC_TYPE_SDIO)
        ret = sdio_enable_wide(card);
    else if ( (card->host->capacity & MMC_CAP_4_BIT_DATA) &&
            (card->scr.bus_widths & SD_SCR_BUS_WIDTH_4)) {

            ret =  mmc_app_set_bus_width(card, MMC_BUS_WIDTH_4);
            if (ret)
                return ret;

            ret = sdio_enable_wide(card);
            if (ret <= 0)
                mmc_app_set_bus_width(card, MMC_BUS_WIDTH_1);
    } else
        return 0;

    if (ret > 0) {
        mmc_set_bus_width(card->host, MMC_BUS_WIDTH_4);
        ret = 0;
    }

    return ret;
}


/*
 * Test if the card supports high-speed mode and, if so, switch to it.
 */
static int mmc_sdio_switch_hs(struct mmc_card *card, int enable)
{
    int ret;
    uint8_t speed;

    if (!(card->host->capacity & MMC_CAP_SD_HIGHSPEED))
        return 0;

    if (!card->cccr.high_speed)
        return 0;

    ret = sdio_io_rw_direct(card, 0, 0, SDIO_CCCR_SPEED, 0, &speed);
    if (ret)
        return ret;

    if (enable)
        speed |= SDIO_SPEED_EHS;
    else
        speed &= ~SDIO_SPEED_EHS;

    ret = sdio_io_rw_direct(card, 1, 0, SDIO_CCCR_SPEED, speed, NULL);
    if (ret)
        return ret;

    return 1;
}


/*
 * Enable SDIO/combo card's high-speed mode. Return 0/1 if [not]supported.
 */
static int sdio_enable_hs(struct mmc_card *card)
{
    int ret;

    ret = mmc_sdio_switch_hs(card, 1);
    if (ret <= 0 || card->type == MMC_TYPE_SDIO)
        return ret;

    ret = mmc_sd_switch_hs(card);
    if (ret <= 0)
        mmc_sdio_switch_hs(card, 0);

    return ret;
}

static unsigned mmc_sdio_get_max_clock(struct mmc_card *card)
{
    unsigned int max_dtr;

    if (mmc_card_hs(card->host)) {
        /*
         * The SDIO specification doesn't mention how
         * the CIS transfer speed register relates to
         * high-speed, but it seems that 50 MHz is
         * mandatory.
         */
        max_dtr = 50 *1000000;
    } else {
        max_dtr = card->cis.max_dtr;
    }

    if (card->type == MMC_TYPE_SD_COMBO) {
        unsigned int sd_max_dtr = mmc_sd_get_max_clock(card);
        max_dtr = min(max_dtr, sd_max_dtr);
    }

    return max_dtr;
}


static int sdio_read_fbr(struct sdio_func *func)
{
    int ret;
    unsigned char data;

    /*
     * 强制 必须支持standard SDIO card attached
     * 对于non-standard SDIO card attached不做判断，不做处理
     */
    ret = sdio_io_rw_direct(func->card, 0, 0, SDIO_FBR_BASE(func->num) + SDIO_FBR_STD_IF, 0, &data);
    if (ret)
        goto out;

    data &= 0x0F;

    if (data == 0x0F) {
        ret = sdio_io_rw_direct(func->card, 0, 0, SDIO_FBR_BASE(func->num) + SDIO_FBR_STD_IF_EXT, 0, &data);
        if (ret)
            goto out;
    }

    func->class = data;

out:
    return ret;
}

static int sdio_remove_func(struct sdio_func *func)
{
    sdio_free_func_cis(func);

    func->card = NULL;

    if (func->tmpbuf)
        free(func->tmpbuf);

    free(func);

    return 0;
}

static int sdio_init_func(struct mmc_card *card, unsigned int fn)
{
    int ret;
    struct sdio_func *func;

    assert(fn <= SDIO_MAX_FUNCS);

    func = malloc(sizeof(struct sdio_func));
    if (!func) {
        printf("sdio alloc function failed\n");
        return -ENOMEM;
    }

    memset(func, 0x00, sizeof(struct sdio_func));

    /*
     * allocate buffer separately to make sure it's properly aligned for
     * DMA usage (incl. 64 bit DMA)
     */
    func->tmpbuf = cache_align_malloc(4);
    if (!func->tmpbuf)
        goto fail;

    func->card = card;
    func->num = fn;

    /*
     * 必须支持 standard SDIO card attached
     * non-standard SDIO card 不做判断处理
     */
    ret = sdio_read_fbr(func);
    if (ret)
        goto fail;

    ret = sdio_read_func_cis(func);
    if (ret)
        goto fail;

    card->sdio_func[fn - 1] = func;

    return 0;

fail:
    sdio_remove_func(func);
    card->sdio_func[fn - 1] = NULL;
    return ret;
}

/*
 * Handle the detection and initialisation of a card.
 */
static int sdio_card_init(struct mmc_host *mmc, uint32_t ocr)
{
    int ret;
    uint32_t rocr;
    struct mmc_card *card = NULL;

    /*
     * Inform the card of the voltage
     */
    ret = mmc_send_io_op_cond(mmc, ocr, &rocr);
    if (ret)
        return ret;

    /*
     * Allocate card structure.
     */
    card = malloc(sizeof(struct mmc_card));
    if (card == NULL) {
        MMC_ERR("malloc sdio card failed.\n");
        ret = -ENOMEM;
        goto err;
    }
    memset(card, 0x00, sizeof(struct mmc_card));

    if (rocr & R4_MEMORY_PRESENT) {
        MMC_ERR("sdio driver now not support Type:MMC_TYPE_SD_COMBO\n");
        ret = -ENOENT;
        goto err1;
    }

    /* Now Drvier Only Support SDIO Card,
     * not support type: MMC_TYPE_SD_COMBO
     */
    card->type = MMC_TYPE_SDIO;
    card->host = mmc;

    ret = mmc_sdio_get_card_address(mmc, &card->rca);
    if (ret)
        goto err1;

    /*
     * Select card, as all following commands rely on that.
     */
    ret = mmc_select_card(card);
    if (ret)
       goto err1;

    /*
     * Read the common registers.
     */
    ret = sdio_read_cccr(card);
    if (ret)
        goto err1;

    /*
     * Read the common CIS tuples.
     */
    ret = sdio_read_common_cis(card);
    if (ret)
        goto err1;

    /*
     * If needed, disconnect card detection pull-up resistor.
     */
    ret = sdio_disable_cd(card);
    if (ret)
        goto err1;


    if ( 0 ) {
        /* 当前不支持 UHS-I cards
         * Initialization sequence for UHS-I cards
         * Only if card supports 1.8v and UHS signaling
         */
        //ret = mmc_sdio_init_uhs_card(card);
    } else {
        /*
         * Switch to high-speed (if supported).
         */
        ret = sdio_enable_hs(card);
        if (ret > 0)
            mmc_set_timing(card->host, MMC_TIMING_SD_HS);
        else if (ret)
            goto err1;

        /*
         * Change to the card's maximum speed.
         */
        mmc_set_clock(card->host, mmc_sdio_get_max_clock(card));

        /*
         * Switch to wider bus (if supported).
         */
        if (card->host->capacity & MMC_CAP_4_BIT_DATA) {
            ret = sdio_enable_4bit_bus(card);
            if (ret)
                goto err1;
        }
    }

    mmc->card = card;

    return 0;

err1:
    free(card);
err:
    return ret;
}

/*
 * Starting point for SDIO card init.
 */
int mmc_attach_sdio(struct mmc_host *mmc)
{
    int ret;
    uint32_t ocr, rocr;
    struct mmc_card *card;

    ret = mmc_send_io_op_cond(mmc, 0, &ocr);
    if (ret)
        return ret;

    /* 简化SDIO Driver 不支持电压范围:VDD_165_195 */
    if (ocr & MMC_VDD_165_195) {
        ocr &= ~MMC_VDD_165_195;
        printf("Now Can't support the low voltage SDIO card\n");
    }

    rocr = mmc_select_voltage(mmc, ocr);

    /*
     * Can we support the voltage(s) of the card(s)?
     */
    if (!rocr) {
        printf("%s not support the voltage(s) of the card(s)\n", mmc->name);
        return -EINVAL;
    }

    /*
     * Detect and init the card
     */
    ret = sdio_card_init(mmc, rocr);
    if (ret) {
        printf("SDIO card init failed\n");
        return -ENODEV;
    }

    /*
     * The number of functions on the card is encoded inside
     * the ocr.
     *
     * funcs_num 范围是0~7, 该计数不包括I/O Card function0上的公共区域
     * function 初始化计数从1开始
     */
    int i;
    int funcs_num = (ocr & 0x70000000) >> 28;
    card = mmc->card;

    /*
     * Initialize all present functions.
     */
    card->sdio_funcs = 0;
    for (i = 0; i < funcs_num; i++, card->sdio_funcs++) {
        ret = sdio_init_func(card, i + 1);
        if (ret)
            goto remove;
    }

    printf("detect SDIO Successfully.... \n");

    return 0;

remove:
    for (i = 0; i < mmc->card->sdio_funcs; i++) {
        if (mmc->card->sdio_func[i]) {
            sdio_remove_func(mmc->card->sdio_func[i]);
            mmc->card->sdio_func[i] = NULL;
        }
    }

    free(mmc->card);
    mmc->card = NULL;
    return ret;
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(sdio_io_rw_direct);
EXPORT_SYMBOL(sdio_io_rw_extended);
