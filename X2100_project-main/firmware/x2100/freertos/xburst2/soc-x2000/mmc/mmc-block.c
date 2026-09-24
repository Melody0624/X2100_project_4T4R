/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */
#include <stdio.h>
#include <common.h>
#include <ffs.h>
#include <assert.h>
#include <malloc.h>
#include "mmc-core.h"
#include "mmc-sd.h"

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

#define CONFIG_MMC_MAX_BLK_COUNT        (65535)
#define MMC_CORE_TIMEOUT_US             (1000 * 1000) /* 1s timeout */

static int mmc_csd_perm_w_protect(struct mmc_card *card)
{
    return card->csd.w_protect;
}

#if 0
static int mmc_set_blocklen(struct mmc_card *card, uint32_t len)
{
    struct mmc_host *mmc = card->host;
    struct mmc_cmd cmd = {0};
    int ret = 0;

    cmd.opcode      = MMC_SET_BLOCKLEN;
    cmd.arg         = len;
    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_AC;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);

    return ret;
}
#endif

/*
 * mmc read ops
 */
static int mmc_read_blocks(struct mmc_card *card, void *buffer, uint32_t start_blk, uint32_t count_blk)
{
    struct mmc_host *mmc = card->host;
    struct mmc_cmd cmd = {0};
    struct mmc_data data = {0};
    int ret = 0;

    if (count_blk > 1)
        cmd.opcode  = MMC_READ_MULTIPLE_BLOCK;
    else
        cmd.opcode  = MMC_READ_SINGLE_BLOCK;

    if (mmc_card_blockaddr(card)) {
        /* high capacity */
        cmd.arg     = start_blk;
    } else {
        /* capacity < 2GiB 操作以字节为单位 */
        cmd.arg     = start_blk << 9;   /* 默认大小: 512Byte */
    }

    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_ADTC;

    data.blksz      = card->csd.rd_blk_len;
    data.blocks     = count_blk;
    data.flags      = MMC_DATA_READ;
    data.dest       = buffer;

    ret = mmc_send_cmd_data(mmc, &cmd, &data);
    if (ret < 0) {
        printf("%s send read command CMD%d failed\n",__FUNCTION__, cmd.opcode);
        return -1;
    }

    if (count_blk > 1) {
        cmd.opcode  = MMC_STOP_TRANSMISSION;
        cmd.arg     = 0;
        cmd.resp_type = MMC_RSP_R1B;
        if (mmc_send_cmd_data(mmc, &cmd, NULL)) {
            printf("mmc failed to send stop cmd\n");
            return -1;
        }
    }

    /* Waiting for the ready status */
    uint32_t status = 0;
    uint64_t timeout_us = systick_get_time_us() + MMC_CORE_TIMEOUT_US;
    do {
        ret = mmc_send_status(card, &status);
        if (ret)
            return ret;

        if (systick_get_time_us() > timeout_us) {
            printf("mmc_read_blocks timeout\n");
            return -1;
        }
    } while( (!(status & R1_READY_FOR_DATA) ||
            (R1_CURRENT_STATE(status) == R1_STATE_PRG)) );

    return count_blk;
}

static int mmc_blk_read(struct mmc_card *card, uint32_t start, uint32_t count_blks, void *buffer)
{
    struct mmc_csd *csd = &card->csd;
    uint32_t cur = 0, blocks_todo = count_blks;
    uint32_t block_size = csd->rd_blk_len;

    if (count_blks == 0)
        return 0;

    /* 使用CSD中定义的默认大小,无需重新发送set block length的命令 */
    // if (mmc_set_blocklen(card, block_size))
    //     return 0;

    do {
        cur = (blocks_todo > CONFIG_MMC_MAX_BLK_COUNT) ?  CONFIG_MMC_MAX_BLK_COUNT : blocks_todo;
        if(mmc_read_blocks(card, buffer, start, cur) != cur)
            return (count_blks - blocks_todo);

        blocks_todo -= cur;
        start += cur;
        buffer += cur * block_size;
    } while (blocks_todo > 0);

    return count_blks;
}

/*
 * mmc write ops
 */
static int mmc_write_blocks(struct mmc_card *card, void *buffer, uint32_t start_blk, uint32_t count_blk)
{
    struct mmc_host *mmc = card->host;
    struct mmc_cmd cmd = {0};
    struct mmc_data data = {0};
    uint64_t capacity = card->card_capacity << 10;
    int ret = 0;

    if (start_blk + count_blk > capacity / card->csd.wr_blk_len) {
        printf("mmc write out of card capacity range.\n");
        return -1;
    }

    if (count_blk > 1)
        cmd.opcode  = MMC_WRITE_MULTIPLE_BLOCK;
    else
        cmd.opcode  = MMC_WRITE_BLOCK;

    if (mmc_card_blockaddr(card)) {
        /* high capacity */
        cmd.arg     = start_blk;
    } else {
        /* capacity < 2GiB 操作以字节为单位 */
        cmd.arg     = start_blk << 9;   /* 默认大小: 512Byte */
    }

    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_ADTC;

    data.blksz      = card->csd.wr_blk_len;
    data.blocks     = count_blk;
    data.flags      = MMC_DATA_WRITE;
    data.src        = buffer;

    ret = mmc_send_cmd_data(mmc, &cmd, &data);
    if (ret < 0) {
        printf("%s write command CMD%d failed\n",__FUNCTION__, cmd.opcode);
        return -1;
    }

    if (count_blk > 1) {
        cmd.opcode = MMC_STOP_TRANSMISSION;
        cmd.arg = 0;
        cmd.resp_type = MMC_RSP_R1B;
        if (mmc_send_cmd_data(mmc, &cmd, NULL)) {
            printf("mmc failed to send stop cmd\n");
            return -1;
        }
    }

    /* Waiting for the ready status */
    uint32_t status = 0;
    uint64_t timeout_us = systick_get_time_us() + MMC_CORE_TIMEOUT_US;
    do {
        ret = mmc_send_status(card, &status);
        if (ret)
            return ret;

        if (systick_get_time_us() > timeout_us) {
            printf("mmc_write_blocks timeout\n");
            return -1;
        }
    } while( (!(status & R1_READY_FOR_DATA) ||
            (R1_CURRENT_STATE(status) == R1_STATE_PRG)) );

    return count_blk;
}

static int mmc_blk_write(struct mmc_card *card, uint32_t start_blk, uint32_t count_blk, void *buffer)
{
    struct mmc_csd *csd = &card->csd;
    uint32_t cur = 0, blocks_todo = count_blk;

    if (count_blk == 0)
        return 0;

    if (mmc_csd_perm_w_protect(card)) {
        printf("mmc write failed(NO PERM). write is protected.\n");
        return -EPERM;
    }

    /* 使用CSD中定义的默认大小,无需重新发送set block length的命令 */
    // if (mmc_set_blocklen(card, card->csd.wr_blk_len))
    //     return 0;

    do {
        cur = (blocks_todo > CONFIG_MMC_MAX_BLK_COUNT) ?  CONFIG_MMC_MAX_BLK_COUNT : blocks_todo;
        if(mmc_write_blocks(card, buffer, start_blk, cur) != cur)
            return (count_blk - blocks_todo);

        blocks_todo -= cur;
        start_blk += cur;
        buffer += cur * csd->wr_blk_len;
    } while (blocks_todo > 0);

    return count_blk;
}

/*
 * erase 操作的单元是 erase group， 在CSD/EXT_CSD中定义
 * trim  操作的单元是 write block，即 512 Bytes(或在CSD中定义)
 * discard 和 trim类似。
 * discard 和trim区别是， device执行完之后， host再去读取被擦除的地址得到的内容不同。
 *                      trim命令之后返回全0或全1，
 *                      discard命令之后，可能返回部分或者全部的original data，由device厂家决定 JEDEC没有强制要求
 *
 * device 对 discard/erase/trim实际执行的操作都是把设定范围内的address标记起来，说明这些地址已经被擦除，而执行flash实际的擦除是要等sanitize命令
 * discard 以write block作为操作单元，可以覆盖到erase 和 trim的地址范围，而host知道已经被擦除的范围可以不关心其实际的是否执行完成擦除。
 *
 */

/*
 * function1: erase group    : MMC_ERASE_ARG
 * function2: erase trim     : MMC_SECURE_TRIM1_ARG
 * function3: erase discard  : MMC_DISCARD_ARG
 */
static int mmc_do_erase(struct mmc_card *card, int start_blk, int count_blk, uint32_t erase_arg)
{
    struct mmc_host *mmc = card->host;
    struct mmc_cmd cmd = {0};
    int ret = 0;

    if (mmc_csd_perm_w_protect(card)) {
        printf("mmc erase failed(NO PERM). write is protected.\n");
        return -EPERM;
    }

    /*
     * high_capacity 的设备的操作(参数)以block为单位，
     * 非high_capacity 设备操作(参数)以Byte为单位
     */

    /*
     * mmc erase start block
     */
    memset(&cmd, 0x00, sizeof(struct mmc_cmd));
    if (mmc_card_sd(card))
        cmd.opcode  = SD_ERASE_WR_BLK_START;
    else
        cmd.opcode  = MMC_ERASE_GROUP_START;

    if (mmc_card_blockaddr(card)) {
        /* high capacity */
        cmd.arg     = start_blk;
    } else {
        /* capacity < 2GiB 操作以字节为单位 */
        cmd.arg     = start_blk << 9;   /* 默认大小: 512Byte */
    }

    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_AC;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret < 0) {
        printf("%s send start erase command CMD%d failed\n", __FUNCTION__, cmd.opcode);
        return ret;
    }

    /*
     * mmc erase end block
     */
    memset(&cmd, 0x00, sizeof(struct mmc_cmd));
    if (mmc_card_sd(card))
        cmd.opcode  = SD_ERASE_WR_BLK_END;
    else
        cmd.opcode  = MMC_ERASE_GROUP_END;

    if (mmc_card_blockaddr(card)) {
        /* high capacity */
        int end_blk = start_blk + count_blk -1;
        cmd.arg     = end_blk;
    } else {
        /* capacity < 2GiB 操作以字节为单位 */
        int end_blk = ((start_blk + count_blk) << 9) -1; /* 默认大小: 512Byte */
        cmd.arg     = end_blk;
    }

    cmd.resp_type   = MMC_RSP_R1 | MMC_CMD_AC;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret < 0) {
        printf("%s send end erase command CMD%d failed\n", __FUNCTION__, cmd.opcode);
        return ret;
    }

    /*
     * mmc doing erase
     */
    memset(&cmd, 0x00, sizeof(struct mmc_cmd));
    cmd.opcode      = MMC_ERASE;
    cmd.arg         = erase_arg;
    cmd.resp_type   = MMC_RSP_R1B | MMC_CMD_AC;

    ret = mmc_send_cmd_data(mmc, &cmd, NULL);
    if (ret < 0) {
        printf("%s send doing erase command CMD%d failed\n", __FUNCTION__, cmd.opcode);
        return ret;
    }

    /* Check status to be sure of no errors */
    uint32_t status = 0;
    uint64_t timeout_us = systick_get_time_us() + MMC_CORE_TIMEOUT_US;
    do {
        ret = mmc_send_status(card, &status);
        if (ret)
            return ret;

        if (systick_get_time_us() > timeout_us) {
            printf("mmc_do_trim timeout\n");
            return -1;
        }
    } while( (!(status & R1_READY_FOR_DATA) ||
            (R1_CURRENT_STATE(status) == R1_STATE_PRG)) );

    return 0;
}


static int mmc_blk_erase(struct mmc_card *card, int start_blk, int count_blk)
{
    if (mmc_card_sd(card)) {
        /* SD Card Only Support MMC_ERASE_ARG */
        return mmc_do_erase(card, start_blk, count_blk, MMC_ERASE_ARG);
    } else {
        return mmc_do_erase(card, start_blk, count_blk, MMC_SECURE_TRIM1_ARG);
    }
}

int mmc_blk_set_ops(struct mmc_card_ops *card_ops)
{
    if (card_ops) {
        card_ops->read = mmc_blk_read;
        card_ops->write = mmc_blk_write;
        card_ops->erase = mmc_blk_erase;
        return 0;
    }

    return -1;
}