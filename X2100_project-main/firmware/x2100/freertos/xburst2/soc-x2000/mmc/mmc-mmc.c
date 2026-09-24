/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */
#include <stdio.h>
#include <common.h>
#include <assert.h>
#include <malloc.h>
#include "mmc-core.h"
#include "mmc-host.h"
#include "mmc-block.h"

//#define DEBUG_DEVICE_INFO
//#define DUPM_DEVICE_INFO
//#define MMC_DEBUG

#ifdef MMC_DEBUG
#define MMC_DBG(...)                    printf("[MMC] Debug:"), printf(__VA_ARGS__)
#define MMC_WARN(...)                   printf("[MMC] Warn:"), printf(__VA_ARGS__)
#define MMC_ERR(...)                    printf("[MMC] Err:"), printf(__VA_ARGS__)
#else
#define MMC_DBG(...)
#define MMC_WARN(...)
#define MMC_ERR(...)                    printf("[MMC] Err:"), printf(__VA_ARGS__)
#endif

/*
 * 声明
 */
static int _mmc_select_card(struct mmc_host *mmc, struct mmc_card *card);


static const unsigned int tran_exp[] = {
    10000,      100000,     1000000,    10000000,
    0,          0,          0,          0
};

static const unsigned char tran_mant[] = {
    0,  10, 12, 13, 15, 20, 25, 30,
    35, 40, 45, 50, 55, 60, 70, 80,
};

static const unsigned int tacc_exp[] = {
    1,  10, 100,    1000,   10000,  100000, 1000000, 10000000,
};

static const unsigned int tacc_mant[] = {
    0,  10, 12, 13, 15, 20, 25, 30,
    35, 40, 45, 50, 55, 60, 70, 80,
};

#define UNSTUFF_BITS(resp, start, size)                         \
    ({                                                          \
        const int __size = size;                                \
        const u32 __mask = (__size < 32 ? 1 << __size : 0) - 1; \
        const int __off = 3 - ((start) / 32);                   \
        const int __shft = (start) & 31;                        \
        u32 __res;                                              \
                                                                \
        __res = resp[__off] >> __shft;                          \
        if (__size + __shft > 32)                               \
            __res |= resp[__off-1] << ((32 - __shft) % 32);     \
        __res & __mask;                                         \
    })


/*
 * Get Card IDentification(CID) information
 */
int mmc_all_get_cid(struct mmc_host *mmc, uint32_t *cid)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;

    assert(cid);

    cmd.opcode = MMC_ALL_SEND_CID;
    cmd.arg = 0;
    cmd.resp_type =  MMC_RSP_R2 | MMC_CMD_BCR;
    cmd.retries = 3;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret)
        return ret;

    memcpy(cid, cmd.resp, sizeof(uint32_t) * 4);

    return ret;
}

int mmc_get_csd(struct mmc_card *card, uint32_t *csd)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;

    cmd.opcode      = MMC_SEND_CSD;
    cmd.arg         = card->rca << 16;
    cmd.resp_type   = MMC_RSP_R2 | MMC_CMD_AC;
    ret = mmc_send_cmd_data(card->host, &cmd, NULL);
    if (ret)
        return ret;

    memcpy(csd, cmd.resp, sizeof(uint32_t) * 4 );

    return ret;
}

int mmc_select_card(struct mmc_card *card)
{
    assert(card != NULL);

    return _mmc_select_card(card->host, card);
}

/******************************************************************************
 * Get Device OCR(Operation Conditions Register)
 * OCR Bit                   High Voltage              Dual Voltage
 *                           MutimediaCard           MutimediaCard and eMMC
 * [6:0]    Reserved           000 000b                 000 000b
 * [7]      1.70 - 1.95V       0b                       1b
 * [14:8]   2.00 - 2.60V       000 0000b                000 0000b
 * [23:15]  2.70 - 3.60V       1 1111 1111b             1 1111 1111b
 * [28:24]  Reserved           0 0000b                  0 0000b
 * [30:29]  Access Mode        00b(Byte mode)           00b(Byte mode)
 *                             10b(Sector Mode)         10b(Sector Mode)
 * [31]     Card PowerUP Status bit(busy)
 */
static int mmc_send_op_cond(struct mmc_host *mmc, uint32_t ocr, uint32_t *rocr)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;
    int i = 0;

    cmd.opcode = MMC_SEND_OP_COND;
    cmd.arg = ocr;
    cmd.resp_type = MMC_RSP_R3 | MMC_CMD_BCR;
    cmd.retries = 3;

    for (i = 0; i < 100; i++) {
        ret = mmc_send_cmd_data(mmc, &cmd, NULL);
        if (ret)
            break;

        /* if we're just probing, do a single pass */
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

static int mmc_set_card_relative_addr(struct mmc_host *mmc, uint32_t rca)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;

    cmd.opcode      = MMC_SET_RELATIVE_ADDR;
    cmd.arg         = rca << 16;
    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_AC;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);

    return ret;
}

static int __mmc_debug_dump_device_info(struct mmc_card *card)
{
#ifdef DEBUG_DEVICE_INFO
    struct mmc_csd *csd = &card->csd;
    struct mmc_cid *cid = &card->cid;

    /*
     * Dump CSD
     */
#ifdef DUPM_DEVICE_INFO
    printf("csd_resp[0]             : 0x%x\n",      card->raw_csd[0]);
    printf("csd_resp[1]             : 0x%x\n",      card->raw_csd[1]);
    printf("csd_resp[2]             : 0x%x\n",      card->raw_csd[2]);
    printf("csd_resp[3]             : 0x%x\n",      card->raw_csd[3]);

    printf("CSD structure version   : %d\n",        csd->structure);
    printf("TAAC                    : 0x%x\n",      csd->taac);
    printf("NSAC                    : 0x%x\n",      csd->nsac);
    printf("Tran Speed              : %d\n",        csd->tran_speed);
    printf("C_SIZE                  : %d\n",        csd->c_size);
    printf("C_SIZE_MULT             : %d\n",        csd->c_size_mult);
    printf("Read to Write Factor    : %d\n",        csd->r2w_factor);

    printf("Card TACC Clks          : %d\n",        card->tacc_clks);
    printf("Card TACC ns            : %d\n",        card->tacc_ns);
    printf("\n");
#endif

    printf("=================Device Information===========\n");
    /*
     * Dump CID
     */
    printf("ManufacturerID          : 0x%X\n", cid->manfid);
    printf("OEMID                   : 0x%X\n", cid->oemid);
    printf("ProductName             : %c%c%c%c%c%c\n", cid->prod_name[0],
                                                       cid->prod_name[1],
                                                       cid->prod_name[2],
                                                       cid->prod_name[3],
                                                       cid->prod_name[4],
                                                       cid->prod_name[5]);

    printf("eMMC version            : %d\n",        csd->mmca_vsn);
    printf("Device Command Class    : 0x%x\n",      csd->cmd_class);
    printf("Read Block Lenght       : %d Byte\n",   csd->rd_blk_len);
    printf("Write Block Lenght      : %d Byte\n",   csd->wr_blk_len);
    printf("Card Erase Group Size   : %d Bytes\n",  card->erase_size);
    printf("Max Freq                : %d MHz\n",    card->max_data_rate / 1000 / 1000);
    printf("Device Capacity         : %lld KBytes\n", card->card_capacity);
#endif

    return 0;
}

static int mmc_parse_cid(struct mmc_card *card)
{
    struct mmc_cid *cid = &card->cid;
    uint32_t *resp = card->raw_cid;

    cid->manfid         = UNSTUFF_BITS(resp, 120, 8);
    cid->oemid          = UNSTUFF_BITS(resp, 104, 8);
    cid->prod_name[0]   = UNSTUFF_BITS(resp, 96, 8);
    cid->prod_name[1]   = UNSTUFF_BITS(resp, 88, 8);
    cid->prod_name[2]   = UNSTUFF_BITS(resp, 80, 8);
    cid->prod_name[3]   = UNSTUFF_BITS(resp, 72, 8);
    cid->prod_name[4]   = UNSTUFF_BITS(resp, 64, 8);
    cid->prod_name[5]   = UNSTUFF_BITS(resp, 56, 8);
    cid->prod_version   = UNSTUFF_BITS(resp, 48, 8);
    cid->serial         = UNSTUFF_BITS(resp, 16, 32);
    cid->year           = UNSTUFF_BITS(resp, 8, 8);

    return 0;
}

/*
 * Given a 128-bit response, decode to our card CSD structure.
 */
static int mmc_parse_csd(struct mmc_card *card)
{
    int ret = 0;
    struct mmc_csd *csd = &card->csd;
    uint32_t *resp = card->raw_csd;
    uint32_t e, m;
    /*
     * We only understand CSD structure v1.1 and v1.2.
     * v1.2 has extra information in bits 15, 11 and 10.
     * We also support eMMC v4.4 & v4.41.
     */
    csd->structure = UNSTUFF_BITS(resp, 126, 2);
    if (csd->structure == 0) {
        MMC_ERR("%s: unrecognised CSD structure version %d\n",
                card->host->name, csd->structure);
        return -EINVAL;
    }
    csd->mmca_vsn       = UNSTUFF_BITS(resp, 122, 4);

    csd->taac           = UNSTUFF_BITS(resp, 112, 8);
    csd->nsac           = UNSTUFF_BITS(resp, 104, 8);

    csd->tran_speed     = UNSTUFF_BITS(resp, 96, 8);
    csd->cmd_class      = UNSTUFF_BITS(resp, 84, 12);
    csd->rd_blk_len     = UNSTUFF_BITS(resp, 80, 4);
    csd->rd_blk_part    = UNSTUFF_BITS(resp, 79, 1);
    csd->wr_blk_misalign= UNSTUFF_BITS(resp, 78, 1);
    csd->rd_blk_misalign= UNSTUFF_BITS(resp, 77, 1);
    csd->dsr_imp        = UNSTUFF_BITS(resp, 76, 1);
    csd->c_size         = UNSTUFF_BITS(resp, 62, 12);
    csd->c_size_mult    = UNSTUFF_BITS(resp, 47, 3);
    csd->r2w_factor     = UNSTUFF_BITS(resp, 26, 3);
    csd->wr_blk_len     = UNSTUFF_BITS(resp, 22, 4);
    csd->wr_blk_partial = UNSTUFF_BITS(resp, 21, 1);
    csd->w_protect      = UNSTUFF_BITS(resp, 13, 1);
    csd->csd_crc        = UNSTUFF_BITS(resp, 1, 7);

    if (csd->wr_blk_len >= 9) {
        /*
         * erase size : calculate from csd value
         */
        int a, b;
        a = UNSTUFF_BITS(resp, 42, 5);
        b = UNSTUFF_BITS(resp, 37, 5);
        card->erase_size = (a + 1) * (b + 1);
        card->erase_size <<= (csd->wr_blk_len - 9); /* Default unit: Block(512B) */
        card->erase_size *= 512; /* unit Bytes */
    }

    card->card_blk_size = 1 << csd->rd_blk_len;

    /*
     * Device Capacity(Up to 2GB)
     */
    m = csd->c_size;
    e = csd->c_size_mult;
    card->card_capacity = (m + 1) << (e + 2);
    card->card_capacity *= card->card_blk_size;
    card->card_capacity >>= 10; /* unit:KByte */

    /*
     * Device Support Max Rate
     */
    e = csd->tran_speed & 0x07;
    m = (csd->tran_speed & 0x78) >> 3;
    card->max_data_rate = tran_exp[e] * tran_mant[m];

    card->tacc_clks = csd->nsac * 100;

    e = csd->taac & 0x07;
    m = (csd->taac & 0x78) >> 3;
    card->tacc_ns   = (tacc_exp[e] * tacc_mant[m] + 9) / 10;

    /*
     * 重新调整 Read/Write Block单位(Byte)
     */
    csd->rd_blk_len = 1 << csd->rd_blk_len;
    csd->wr_blk_len = 1 << csd->wr_blk_len;

    return ret;
}

static int _mmc_select_card(struct mmc_host *mmc, struct mmc_card *card)
{
    struct mmc_cmd cmd = {0};
    int ret = 0;

    cmd.opcode = MMC_SELECT_CARD;
    if (card) {
        cmd.arg = card->rca << 16;
        cmd.resp_type = MMC_RSP_R1 | MMC_CMD_AC;
    } else {
        cmd.arg = 0;
        cmd.resp_type = MMC_RSP_NONE | MMC_CMD_AC;
    }

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret)
        return ret;

    return 0;
}

static inline int mmc_deselect_card(struct mmc_host *mmc)
{
    return _mmc_select_card(mmc, NULL);
}

static int mmc_send_ext_csd(struct mmc_card *card, uint8_t *buf)
{
    struct mmc_host *mmc = card->host;
    struct mmc_cmd cmd = {0};
    struct mmc_data data = {0};

    cmd.opcode      = MMC_SEND_EXT_CSD;
    cmd.arg         = 0;
    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_ADTC;

    data.blksz      = 512;
    data.blocks     = 1;
    data.flags      = MMC_DATA_READ;
    data.dest       = (char *)buf;

    mmc_send_cmd_data(mmc, &cmd, &data);

    if (cmd.error)
        return cmd.error;

    if (data.error)
        return data.error;

    return 0;
}

static int mmc_get_ext_csd(struct mmc_card *card, uint8_t **new_ext_csd)
{
    int ret = 0;
    uint8_t *ext_csd;

    *new_ext_csd = NULL;

    if (card->csd.mmca_vsn < CSD_SPEC_VER_4)
        return 0;

    /*
     * As the ext_csd is so large and mostly unused, we don't store the
     * raw block in mmc_card.
     */
    ext_csd = malloc(512);
    if (ext_csd == NULL) {
        MMC_ERR("%s could not allocate a buffer to receive the ext_csd.\n", card->host->name);
        return -ENOMEM;
    }

    ret = mmc_send_ext_csd(card, ext_csd);

#ifdef DUPM_DEVICE_INFO
    printf("================Dump EXT_CSD====================\n");
    int i = 0;
    for (i=0; i < 512; i++) {
        if ( (i != 0) && (i % 16 == 0) ) {
            printf("\n");
        }
        printf("%02x:", ext_csd[i]);
    }
    printf("\n");
#endif
    *new_ext_csd = ext_csd;

    return ret;
}

static int mmc_parse_ext_csd(struct mmc_card *card, uint8_t *ext_csd)
{
    int ext_csd_ver = 0;

    if (card == NULL || ext_csd == NULL) {
        MMC_ERR("mmc parse ext csd failed, invaild argument\n");
        return -1;
    }

    ext_csd_ver = ext_csd[EXT_CSD_REV];
    if (ext_csd_ver > 8) {
        MMC_ERR("%s: unrecognised EXT_CSD revision %d\n", card->host->name, ext_csd_ver);
        return -EINVAL;
    }

    if (ext_csd_ver >= 2) {
        /*
         * According to the JEDEC Standard, the value of
         * ext_csd's capacity is valid if the value is more
         * than 2GB
         */
        uint64_t capacity = 0;
        uint64_t sectors;
        sectors = ext_csd[EXT_CSD_SEC_CNT] << 0
                   | ext_csd[EXT_CSD_SEC_CNT + 1] << 8
                   | ext_csd[EXT_CSD_SEC_CNT + 2] << 16
                   | ext_csd[EXT_CSD_SEC_CNT + 3] << 24;
        capacity = sectors * card->card_blk_size;
        capacity >>= 10;  /* unit: KByte */

        /* Cards with density > 2GiB are sector addressed */
        if (sectors > (2u * 1024 * 1024 * 1024) / card->card_blk_size) {
            mmc_card_set_blockaddr(card);
        }
        card->card_capacity = capacity;
    }

    /*
     * Check whether GROUP_DEF is set, if yes, read out
     * group size from ext_csd directly, else calculate
     * the group size from the csd value.
     */
    if (ext_csd[EXT_CSD_ERASE_GROUP_DEF]) {
        /* High-density erase definition. Unit:512KByte */
        card->erase_size = ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE] * 512 * 1024;

    }

    /*
     * Update Device Max Freq & Support Type
     */
    int card_type = ext_csd[EXT_CSD_CARD_TYPE];
    int cap = card->host->capacity;
    int cap2 = card->host->capacity2;
    if ( (card_type & EXT_CSD_CARD_TYPE_HS200) && (cap2 & MMC_CAP2_HS200)) {
        card->max_data_rate = 200 * 1000 * 1000;
        card->mmc_avail_type = EXT_CSD_CARD_TYPE_HS200_1_8V;

    } else if ( (card_type & EXT_CSD_CARD_TYPE_HS) && (cap & MMC_CAP_MMC_HIGHSPEED) ) {
        card->max_data_rate = 52 * 1000 * 1000;
        card->mmc_avail_type = EXT_CSD_CARD_TYPE_HS_52;

    } else {
        card->max_data_rate = 26 * 1000 * 1000;
        card->mmc_avail_type = EXT_CSD_CARD_TYPE_HS_26;
    }

    /*
     * store the partition info of eMMC
     */
    if ((ext_csd[EXT_CSD_PARTITION_SUPPORT] & EXT_CSD_PART_SUPPORT_PART_EN)
            || ext_csd[EXT_CSD_BOOT_MULT]) {
        card->part_config = ext_csd[EXT_CSD_PART_CONFIG];
    }

    card->capacity_boot = ext_csd[EXT_CSD_BOOT_MULT] << 17;
    card->capacity_rpmb = ext_csd[EXT_CSD_RPMB_MULT] << 17;
    int i = 0;
    for (i = 0; i < 4; i++) {
        int index = EXT_CSD_GP_SIZE_MULT + i * 3;
        card->capacity_gp[i] = (ext_csd[index + 2] << 16)
                + (ext_csd[index + 1] << 8)
                + (ext_csd[index]);

        card->capacity_gp[i] *= ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE];
        card->capacity_gp[i] *= ext_csd[EXT_CSD_HC_WP_GRP_SIZE];
    }

    card->ext_csd = ext_csd;

    struct mmc_cid *cid = &card->cid;
    printf("eMMC Device:%c%c%c%c%c%c Capacity %lld MB\n", cid->prod_name[0],
                                                    cid->prod_name[1],
                                                    cid->prod_name[2],
                                                    cid->prod_name[3],
                                                    cid->prod_name[4],
                                                    cid->prod_name[5],
                                                    card->card_capacity / 1024);

    return 0;
}

static int mmc_switch(struct mmc_card *card, uint8_t cmd_sets, uint8_t index, uint8_t value)
{
    struct mmc_cmd cmd = {0};
    struct mmc_host *mmc = card->host;
    int ret = 0;
    uint32_t status;

    cmd.opcode          = MMC_SWITCH;
    cmd.arg             = (MMC_SWITCH_MODE_WRITE_BYTE << 24) | (index << 16) | (value << 8) | cmd_sets;
    cmd.resp_type       = MMC_RSP_R1B;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret)
        return ret;

    /* Must check status to be sure of no errors */
    do {
        ret = mmc_send_status(card, &status);
        if (ret)
            return ret;

    } while(R1_CURRENT_STATE(status) == R1_STATE_PRG);

    return ret;
}

static int mmc_compare_ext_csds(struct mmc_card *card, uint8_t *ext_csd, int bus_width)
{
    int ret = 0;
    uint8_t *bw_ext_csd = NULL;

    if (bus_width == MMC_BUS_WIDTH_1)
        return 0;

    ret = mmc_get_ext_csd(card, &bw_ext_csd);
    if (ret < 0)
        goto out;

    /* only compare read only fields */
    ret = !( (ext_csd[EXT_CSD_PARTITION_SUPPORT] == bw_ext_csd[EXT_CSD_PARTITION_SUPPORT]) &&
        (ext_csd[EXT_CSD_ERASED_MEM_CONT] == bw_ext_csd[EXT_CSD_ERASED_MEM_CONT]) &&
        (ext_csd[EXT_CSD_REV] == bw_ext_csd[EXT_CSD_REV]) &&
        (ext_csd[EXT_CSD_STRUCTURE] == bw_ext_csd[EXT_CSD_STRUCTURE]) &&
        (ext_csd[EXT_CSD_CARD_TYPE] == bw_ext_csd[EXT_CSD_CARD_TYPE]) &&
        (ext_csd[EXT_CSD_S_A_TIMEOUT] == bw_ext_csd[EXT_CSD_S_A_TIMEOUT]) &&
        (ext_csd[EXT_CSD_HC_WP_GRP_SIZE] == bw_ext_csd[EXT_CSD_HC_WP_GRP_SIZE]) &&
        (ext_csd[EXT_CSD_ERASE_TIMEOUT_MULT] == bw_ext_csd[EXT_CSD_ERASE_TIMEOUT_MULT]) &&
        (ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE] == bw_ext_csd[EXT_CSD_HC_ERASE_GRP_SIZE]) &&
        (ext_csd[EXT_CSD_SEC_TRIM_MULT] == bw_ext_csd[EXT_CSD_SEC_TRIM_MULT]) &&
        (ext_csd[EXT_CSD_SEC_ERASE_MULT] == bw_ext_csd[EXT_CSD_SEC_ERASE_MULT]) &&
        (ext_csd[EXT_CSD_SEC_FEATURE_SUPPORT] == bw_ext_csd[EXT_CSD_SEC_FEATURE_SUPPORT]) &&
        (ext_csd[EXT_CSD_TRIM_MULT] == bw_ext_csd[EXT_CSD_TRIM_MULT]) &&
        (ext_csd[EXT_CSD_SEC_CNT + 0] == bw_ext_csd[EXT_CSD_SEC_CNT + 0]) &&
        (ext_csd[EXT_CSD_SEC_CNT + 1] == bw_ext_csd[EXT_CSD_SEC_CNT + 1]) &&
        (ext_csd[EXT_CSD_SEC_CNT + 2] == bw_ext_csd[EXT_CSD_SEC_CNT + 2]) &&
        (ext_csd[EXT_CSD_SEC_CNT + 3] == bw_ext_csd[EXT_CSD_SEC_CNT + 3]));

    if (ret)
        ret = -EINVAL;

out:
    if (bw_ext_csd)
        free(bw_ext_csd);

    return ret;
}

__attribute__((__unused__)) static int mmc_ext_csd_power_on(struct mmc_card *card)
{
    int ret;

    /* power off notification */
    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_POWER_OFF_NOTIFICATION , EXT_CSD_POWER_ON);
    if (ret)
        MMC_ERR("MMC EXT_CSD_HS_TIMING Power OFF Notification error\n");

    return ret;
}

__attribute__((__unused__)) static int mmc_enable_hpi_mgmt(struct mmc_card *card)
{
    int ret;

    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_HPI_MGMT , 1);
    if (ret)
        MMC_ERR("MMC EXT_CSD_HS_TIMING Enable HPI error\n");

    return ret;
}

__attribute__((__unused__)) static int mmc_cache_ctrl(struct mmc_card *card)
{
    int ret;

    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_CACHE_CTRL , 1);
    if (ret)
        MMC_ERR("MMC EXT_CSD_HS_TIMING Cache ctrl error\n");

    return ret;
}

static int mmc_select_ddr_bus_width(struct mmc_card *card)
{
    int ret;

    /* Change to High Speed Mode */
    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_BUS_WIDTH , EXT_CSD_DDR_BUS_WIDTH_8);
    if (ret)
        MMC_ERR("MMC EXT_CSD_HS_TIMING Change mode error\n");

    return ret;
}

/*
 * 返回值: < 0 : 切换宽度失败
 *        >= 0: 切换宽度成功
 *              =0 未执行切换
 *              =1,4,8 ：data buswidth
 */
static int mmc_select_bus_width(struct mmc_card *card)
{
    uint32_t ext_csd_bits[] = {
            EXT_CSD_BUS_WIDTH_8,
            EXT_CSD_BUS_WIDTH_4,
            EXT_CSD_BUS_WIDTH_1
    };

    uint32_t bus_widths[] = {
            MMC_BUS_WIDTH_8,
            MMC_BUS_WIDTH_4,
            MMC_BUS_WIDTH_1
    };

    int ret = 0;
    int index = 0;

    uint8_t *ext_csd = card->ext_csd;
    if (ext_csd == NULL)
        return -EINVAL;

    /*
     * Version 4 supports high-speed
     */
    if (card->csd.mmca_vsn < CSD_SPEC_VER_4)
        return 0;


    /*
    * Unlike SD, MMC cards dont have a configuration register to notify
    * supported bus width. So bus test command should be run to identify
    * the supported bus width or compare the ext csd values of current
    * bus width and ext csd values of 1 bit mode read earlier.
    */
    if (card->host->capacity & MMC_CAP_8_BIT_DATA) {
        index = 0;
    } else if (card->host->capacity & MMC_CAP_4_BIT_DATA) {
        index = 1;
    } else {
        index = 2;
    }

    for (; index < sizeof(ext_csd_bits) / sizeof(uint32_t); index++) {
        ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_BUS_WIDTH , ext_csd_bits[index]);
        if (ret)
            continue;

        uint8_t bus_width = bus_widths[index];
        mmc_set_bus_width(card->host, bus_width);

        /*
         * 检查一下切换到对应的bus宽度是否有效
         */
        ret = mmc_compare_ext_csds(card, ext_csd, bus_width);
        if (!ret)
            break;
    }

    ret = bus_widths[index] == 0 ? 1 : 2 << (bus_widths[index]-1);
    MMC_DBG("eMMC support bus widths[%d] = %d\n", index, ret);
    return ret;
}

__attribute__((__unused__)) static int mmc_change_freq(struct mmc_card *card, uint32_t freq)
{
    struct mmc_host *mmc = card->host;
    int value = 0;
    int ret;

    mmc_set_clock(mmc, freq);
    freq = mmc->clock;

    if (freq > 52 *1000 * 1000) {
        value = EXT_CSD_TIMING_HS200;  /* HS200 */
    } else if (freq > 26 *1000 * 1000) {
        value = EXT_CSD_TIMING_HS;  /* High Speed */
    } else {
        value = EXT_CSD_TIMING_BC;  /* Default */
    }

    /* Change to High Speed Mode */
    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_HS_TIMING , value);
    if (ret)
        MMC_ERR("MMC EXT_CSD_HS_TIMING Change mode error\n");

    return ret;
}

/*
 * Activate wide bus and DDR if supported.
 */
__attribute__((__unused__)) static int mmc_select_hs_ddr(struct mmc_card *card)
{
    mmc_select_ddr_bus_width(card);

    mmc_set_timing(card->host, MMC_TIMING_MMC_DDR52);

    return 0;
}

/*
 * Switch to the high-speed mode
 */
static int mmc_select_hs(struct mmc_card *card)
{
    int ret;
    int value;

    value = EXT_CSD_TIMING_HS ;
    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_HS_TIMING , value);
    if (ret) {
        MMC_ERR("MMC EXT_CSD_HS_TIMING Change mode(to %s) error\n", "high-speed");
        return ret;
    }

    mmc_set_timing(card->host, MMC_TIMING_MMC_HS);
    return 0;
}

/*
 * Execute tuning sequence to seek the proper bus operating
 * conditions for HS200 and HS400, which sends CMD21 to the device.
 */
static int mmc_hs200_tuning(struct mmc_card *card)
{
    return mmc_execute_tuning(card);
}

/*
 * For device supporting HS200 mode, the following sequence
 * should be done before executing the tuning process.
 * 1. set the desired bus width(4-bit or 8-bit, 1-bit is not supported)
 * 2. switch to HS200 mode
 * 3. set the clock to > 52Mhz and <=200MHz
 */
static int mmc_select_hs200(struct mmc_card *card)
{
    int ret;
    int value;
    /* 设置电压 1.2V or 1.8V */

    // mmc_select_driver_type(card);
    int driver_strength = 1;

	/*
	 * Set the bus width(4 or 8) with host's support and
	 * switch to HS200 mode if bus width is set successfully.
	 */
    ret = mmc_select_bus_width(card);
    if (ret < 4) {
        /* HS200模式只支持数据宽度 4 or 8 */
        printf("mmc select bus width failed\n");
        return ret;
    }

    value = EXT_CSD_TIMING_HS200 | driver_strength << EXT_CSD_DRV_STR_SHIFT;
    ret = mmc_switch(card, EXT_CSD_CMD_SET_NORMAL, EXT_CSD_HS_TIMING , value);
    if (ret) {
        MMC_ERR("MMC EXT_CSD_HS_TIMING Change mode(to %s) error\n", "hs200");
        return ret;
    }

    mmc_set_timing(card->host, MMC_TIMING_MMC_HS200);
    return 0;
}


/*
 * Activate High Speed or HS200 mode if supported.
 */
static int mmc_select_timing(struct mmc_card *card)
{
    int ret = 0;
    char *timing_str = "default";
    if (card->mmc_avail_type & EXT_CSD_CARD_TYPE_HS200) {
        timing_str = "hs200";
        ret = mmc_select_hs200(card);

    } else if (card->mmc_avail_type & EXT_CSD_CARD_TYPE_HS) {
        timing_str = "high-speed";
        ret = mmc_select_hs(card);
    }

    if (ret) {
        printf("%s switch to %s failed\n", card->host->name, timing_str);
        return ret;
    }

    mmc_set_clock(card->host, card->max_data_rate);

    MMC_DBG("%s work mode is %s\n", card->host->name, timing_str);
    return ret;
}

static int mmc_card_init(struct mmc_host *mmc, uint32_t ocr)
{
    int ret;
    uint32_t rocr;
    uint32_t cid[4];
    uint8_t *ext_csd = NULL;
    struct mmc_card *card = NULL;

    /*
     * Since we're changing the OCR value, we seem to
     * need to tell some cards to go back to the idle
     * state.  We wait 1ms to give cards time to
     * respond.
     * mmc_go_idle is needed for eMMC that are asleep
     */
    mmc_go_idle(mmc);

    /*
     * bit:30=1b indicates that we support high capacity
     * Access Mode is Sector Mode
     */
    ret = mmc_send_op_cond(mmc, ocr | (1 << 30), &rocr);
    if (ret)
        goto err;

    /*
     * Get Card CID information
     */
    ret = mmc_all_get_cid(mmc, cid);
    if (ret)
        goto err;

    card = malloc(sizeof(struct mmc_card));
    if (card == NULL) {
        printf("malloc mmc card failed.\n");
        ret = -ENOMEM;
        goto err;
    }

    memset(card, 0x00, sizeof(struct mmc_card));
    card->type = MMC_TYPE_MMC;
    card->rca = 0x1; /* Host 分配设备地址， 从1开始 */
    card->host = mmc;
    memcpy(card->raw_cid, cid, sizeof(card->raw_cid));

    mmc_parse_cid(card);

    /*
     * Set Card RCA address
     */
    ret = mmc_set_card_relative_addr(mmc, card->rca);
    if (ret)
        goto err1;

    /*
     * Get Card CSD information (Card-Specific Data)
     */
    ret = mmc_get_csd(card, card->raw_csd);
    if (ret)
        goto err1;

    ret = mmc_parse_csd(card);
    if (ret)
        goto err1;

    ret = mmc_select_card(card);
    if (ret)
        goto err1;

    /*
     * Get and process extended CSD
     */
    ret = mmc_get_ext_csd(card, &ext_csd);
    if (ret)
        goto err1;

    ret = mmc_parse_ext_csd(card, ext_csd);
    if (ret) {
        goto err2;
    }

    if (!(mmc_card_blockaddr(card)) && (rocr & (1 << 30)))
        mmc_card_set_blockaddr(card);

    __mmc_debug_dump_device_info(card);

    /*
     * set bus speed
     */
    ret = mmc_select_timing(card);
    if (ret)
        goto err2;

    /*
     * set bus width
     */
    if (mmc_card_hs200(card->host) ) {
        ret = mmc_hs200_tuning(card);
        if (ret)
            goto err2;

    } else {
        ret = mmc_select_bus_width(card);
        if (ret < 0)
            goto err2;
    }

    /* card ops */
    card->ops = malloc(sizeof(struct mmc_card_ops));
    if (card->ops == NULL) {
        printf("malloc card ops failed.\n");
        ret = -ENOMEM;
        goto err3;
    }

    mmc_blk_set_ops(card->ops);

    mmc->card = card;

    free(ext_csd);
    card->ext_csd = NULL;

    return 0;

err3:
err2:
    free(ext_csd);
err1:
    free(card);
err:
    return ret;
}

int mmc_attach_mmc(struct mmc_host *mmc)
{
    int ret = 0;
    uint32_t ocr= 0;

    ret = mmc_send_op_cond(mmc, 0, &ocr);
    if (ret)
        return ret;

    ocr = mmc_select_voltage(mmc, ocr);

    /*
     * Can we support the voltage(s) of the card(s)?
     */
    if (!ocr) {
        ret = -EINVAL;
        return ret;
    }

    ret = mmc_card_init(mmc, ocr);
    if (ret) {
        printf("MMC card init failed\n");
        return -ENODEV;
    }

    printf("detect MMC Successfully ....\n");

    return ret;
}

int mmc_attach_mmc_params(struct mmc_host *mmc, struct card_info_params *params)
{
    int ret;
    struct mmc_card *card = NULL;

    assert(params);

    card = malloc(sizeof(struct mmc_card));
    if (card == NULL) {
        printf("malloc mmc card failed.\n");
        ret = -ENOMEM;
        goto err;
    }

    memset(card, 0x00, sizeof(struct mmc_card));
    card->type = MMC_TYPE_MMC;
    card->rca = params->rca;
    card->host = mmc;

    memcpy(card->raw_cid, params->raw_cid, sizeof(card->raw_cid));
    mmc_parse_cid(card);

    memcpy(card->raw_csd, params->raw_csd, sizeof(card->raw_cid));
    ret = mmc_parse_csd(card);
    if (ret)
        goto err1;

    ret = mmc_parse_ext_csd(card, params->ext_csd);
    if (ret)
        goto err1;

    if (params->highcap)
        mmc_card_set_blockaddr(card);

    mmc->bus_width = params->bus_width;
    mmc->clock = params->max_speed;

    if (mmc->clock > 52 * 1000000)
        mmc->timing = MMC_TIMING_MMC_HS200;     /* 52M ~ 200M */
    else if (mmc->clock > 26 * 1000000)
        mmc->timing = MMC_TIMING_MMC_HS;        /* 26M ~ 52M */
    else
        mmc->timing = MMC_TIMING_LEGACY;        /* 0M ~ 26M */

    mmc->power_mode = MMC_POWER_ON;
    mmc->min_voltage = fls(mmc->voltages) - 1;

    /* 更新变量信息到控制器(mmc- soc-type.c) */
    mmc_set_timing(mmc, mmc->timing);

    /* card ops */
    card->ops = malloc(sizeof(struct mmc_card_ops));
    if (card->ops == NULL) {
        printf("malloc card ops failed.\n");
        ret = -ENOMEM;
        goto err1;
    }

    mmc_blk_set_ops(card->ops);

    mmc->card = card;

    return 0;

err1:
    free(card);
err:
    return ret;
}
