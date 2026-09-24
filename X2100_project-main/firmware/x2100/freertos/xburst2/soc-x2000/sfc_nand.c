#include <errno.h>
#include <assert.h>
#include <common.h>
#include <malloc.h>
#include <driver/cache.h>
#include <driver/clk.h>
#include <io.h>
#include <os.h>
#include "sfc_nand_params.h"
#include "sfc_common.h"
#include "nand_device/bbm.h"
#include "nand_device/nand.h"
#include "nand_device/nand_common.h"
#include <driver/sfc_nand.h>
#include "sfc_nand_bbt.c"

#include <mtd_driver_nand.h>

int sfc_nand_partition_init(struct mtd_nand_device *mtd_device);
static void soc_sfc_nand_support_to_fs(void);

#define STATUS_SUSPND    (1<<0)
#define CONFIG_SFC_DEFAULT_RATE         (200 * 1000000)

/*
 * below is the informtion about nand
 * that user should modify according to nand spec
 * */

/*struct nand_param_from_burner nand_param_from_burner;*/
static LIST_HEAD(nand_list);
static struct sfc_flash *flash;
static struct storage_info nand_flash_info = {0};

void dump_flash_info(struct sfc_flash *flash)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_base_param *param = &nand_info->param;
    struct ingenic_sfcnand_partition *partition = nand_info->partition.partition;
    uint8_t num_partition = nand_info->partition.num_partition;

    printf("id_manufactory = 0x%02x\n", nand_info->id_manufactory);
    printf("id_device = 0x%02x\n", nand_info->id_device);

    printf("pagesize = %d\n", param->pagesize);
    printf("blocksize = %d\n", param->blocksize);
    printf("oobsize = %d\n", param->oobsize);
    printf("flashsize = %d\n", param->flashsize);

    printf("tHOLD = %d\n", param->tHOLD);
    printf("tSETUP = %d\n", param->tSETUP);
    printf("tSHSL_R = %d\n", param->tSHSL_R);
    printf("tSHSL_W = %d\n", param->tSHSL_W);

    printf("ecc_max = %d\n", param->ecc_max);
    printf("need_quad = %d\n", param->need_quad);

    while (num_partition--) {
        printf("partition(%d) name=%s\n", num_partition, partition[num_partition].name);
        printf("partition(%d) size = 0x%x\n", num_partition, partition[num_partition].size);
        printf("partition(%d) offset = 0x%x\n", num_partition, partition[num_partition].offset);
        printf("partition(%d) mask_flags = 0x%x\n", num_partition, partition[num_partition].mask_flags);
    }
    return;
}

static int32_t ingenic_sfc_nand_read(struct sfc_flash *flash, struct flash_address *flash_address, u_char *buffer, size_t len)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_ops *ops = nand_info->ops;
    struct sfc_cdt_xfer xfer;
    int32_t ret = 0;

    memset(&xfer, 0, sizeof(xfer));

    /* set Index */
    if (nand_info->param.need_quad) {
        xfer.cmd_index = NAND_QUAD_READ_TO_CACHE;
    } else {
        xfer.cmd_index = NAND_STANDARD_READ_TO_CACHE;
    }

    /* set addr */
    xfer.rowaddr = flash_address->pageaddr;

    if (nand_info->param.plane_select) {
        xfer.columnaddr = CONVERT_COL_ADDR(flash_address->pageaddr, flash_address->columnaddr);
    } else {
        xfer.columnaddr = flash_address->columnaddr;
    }

    xfer.staaddr0 = SPINAND_ADDR_STATUS;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = len;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = flash_address->ops_mode;
    xfer.config.buf = buffer;

retry:
    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    /* if underrun, retry read */
    if (flash->sfc->retry_count > 0) {
        printf("SFC retry transfer! %s %s %d\n",__FILE__,__func__,__LINE__);
        goto retry;
    }

    /* get status to check nand ecc status */
    ret = ops->get_feature(flash, GET_ECC_STATUS);

    return ret;
}

static int badblk_check(int len, unsigned char *buf)
{
    int  j;
    unsigned char *check_buf = buf;

    for (j = 0; j < len; j++) {
        if (check_buf[j] != 0xff) {
            return 1;
        }
    }
    return 0;
}

static int32_t ingenic_sfc_nand_write(struct sfc_flash *flash, u_char *buffer, struct flash_address *flash_address, size_t len)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_ops *ops = nand_info->ops;
    struct sfc_cdt_xfer xfer;
    int32_t ret = 0;

    memset(&xfer, 0, sizeof(xfer));

    /* set Index */
    if (nand_info->param.need_quad) {
        xfer.cmd_index = NAND_QUAD_WRITE_ENABLE;
    } else {
        xfer.cmd_index = NAND_STANDARD_WRITE_ENABLE;
    }

    /* set addr */
    xfer.rowaddr = flash_address->pageaddr;

    if (nand_info->param.plane_select) {
        xfer.columnaddr = CONVERT_COL_ADDR(flash_address->pageaddr, flash_address->columnaddr);
    } else {
        xfer.columnaddr = flash_address->columnaddr;
    }

    xfer.staaddr0 = SPINAND_ADDR_STATUS;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = len;
    xfer.config.data_dir = GLB_TRAN_DIR_WRITE;
    xfer.config.ops_mode = flash_address->ops_mode;
    xfer.config.buf = buffer;

retry:
    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    /* if overrun, retry write */
    if (flash->sfc->retry_count > 0) {
        printf("SFC retry transfer! %s %s %d\n",__FILE__,__func__,__LINE__);
        goto retry;
    }

    /* get status to be sure nand write completed */
    ret = ops->get_feature(flash, GET_WRITE_STATUS);

    return  ret;
}

static void create_sfc_desc(struct jz_sfc *sfc, unsigned char *buf, size_t len)
{
    struct sfc_desc *desc = sfc->desc;

    /* Physical Address Continuity and only need one descriptor */
    desc[0].next_des_addr = 0;
    desc[0].mem_addr = virt_to_phys((void *)buf);
    desc[0].tran_len = len;

    /* last descriptor is not link */
    desc[0].link = 0;
    // 确保写回 mem
    sfc_dma_cache_sync_to_device(desc, sizeof(struct sfc_desc) * SFC_DESC_MAX_NUM);
}

static int ingenic_sfcnand_write(struct sfc_flash *flash, unsigned long to, size_t len, size_t *retlen, const u_char *buf);

static int ingenic_sfcnand_write_oob(struct sfc_flash *flash, unsigned long addr, struct mtd_oob_ops *ops)
{
    struct flash_address flash_address;
    uint32_t oob_addr = (uint32_t)addr;
    struct ingenic_sfcnand_flashinfo *flash_info = flash->flash_info;
    int32_t ret;

    mutex_lock(&flash->lock);

    if (ops->datbuf && ops->len) {
        ret = ingenic_sfcnand_write(flash, addr, ops->len, &ops->retlen, ops->datbuf);
        if (ret)
            goto write_addr_exit;
    }

    if (!(ops->oobbuf && ops->ooblen)) {
        ret = 0;
        goto write_oob_exit;
    }

    flash_address.pageaddr = oob_addr / flash_info->param.pagesize;
    flash_address.columnaddr =  flash_info->param.pagesize;
    flash_address.ops_mode = DMA_OPS;

    /* create DMA Descriptors */
    create_sfc_desc(flash->sfc, (unsigned char *)ops->oobbuf, ops->ooblen);

    if ((ret = ingenic_sfc_nand_write(flash, ops->oobbuf, &flash_address, ops->ooblen))) {
        printf("spi nand write oob error %s %s %d \n",__FILE__,__func__,__LINE__);
        goto write_oob_exit;
    }
    ops->retlen = ops->ooblen;

write_addr_exit:
write_oob_exit:
    mutex_unlock(&flash->lock);
    return ret;
}

static int ingenic_sfcnand_chip_block_markbad(struct sfc_flash *flash, unsigned long ofs)
{
    struct ingenic_sfcnand_flashinfo *flash_info = flash->flash_info;
    struct nand_chip *chip = flash_info->chip;
    uint8_t buf[2] = { 0, 0 };
    int  ret = 0, i = 0;
    int block = ofs / flash_info->param.blocksize;

    bbt_mark(block, 1);

    int write_oob = !(chip->bbt_options & NAND_BBT_NO_OOB_BBM);

    /* Write bad block marker to OOB */
    if (write_oob) {
        struct mtd_oob_ops ops;
        unsigned long wr_ofs = ofs;
        ops.datbuf = NULL;
        ops.oobbuf = buf;
        ops.ooboffs = chip->badblockpos;
        if (chip->options & NAND_BUSWIDTH_16) {
            ops.ooboffs &= ~0x01;
            ops.len = ops.ooblen = 2;
        } else {
            ops.len = ops.ooblen = 1;
        }
        ops.mode = MTD_OPS_PLACE_OOB;

        /* Write to first/last page(s) if necessary */
        if (chip->bbt_options & NAND_BBT_SCANLASTPAGE)
            wr_ofs += flash_info->param.blocksize - flash_info->param.pagesize;
        do {
            ret = ingenic_sfcnand_write_oob(flash, wr_ofs, &ops);
            if (ret)
                return ret;
            wr_ofs += flash_info->param.pagesize;
            i++;
        } while ((chip->bbt_options & NAND_BBT_SCAN2NDPAGE) && i < 2);
    }

    return ret;
}

static int32_t ingenic_sfc_nand_erase_blk(struct sfc_flash *flash, uint32_t pageaddr)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_ops *ops = nand_info->ops;
    struct sfc_cdt_xfer xfer;
    int32_t ret = 0;

    memset(&xfer, 0, sizeof(xfer));

    /* set index */
    xfer.cmd_index = NAND_ERASE_WRITE_ENABLE;

    /* set addr */
    xfer.rowaddr = pageaddr;
    xfer.staaddr0 = SPINAND_ADDR_STATUS;

    /* set transfer config */
    xfer.dataen = DISABLE;

    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    /* get status to be sure nand write completed */
    ret = ops->get_feature(flash, GET_ERASE_STATUS);
    if (ret){
        printf("Erase error, get state error ! %s %s %d \n",__FILE__,__func__,__LINE__);
    }

    return ret;
}

static int ingenic_sfcnand_erase(struct sfc_flash *flash, struct erase_info *instr)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    uint32_t addr = (uint32_t)instr->addr;
    uint32_t end;
    int32_t ret;

    if ((addr % nand_info->param.blocksize) || (instr->len % nand_info->param.blocksize)) {
        printf("ERROR:%s line %d eraseaddr no align\n", __func__,__LINE__);
        return -EINVAL;
    }
    end = addr + instr->len;
    instr->state = MTD_ERASING;
    while (addr < end) {
        if ((ret = ingenic_sfc_nand_erase_blk(flash, addr / nand_info->param.pagesize))) {
            printf("Detected bad block, spi nand erase error blk id  %d !\n",addr / nand_info->param.blocksize);
            instr->state = MTD_ERASE_FAILED;
            goto erase_exit;
        }
        addr += nand_info->param.blocksize;
    }

    instr->state = MTD_ERASE_DONE;
erase_exit:
    return ret;
}

static int ingenic_sfcnand_read(struct sfc_flash *flash, unsigned long from, size_t len, size_t *retlen, u_char *buf)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    uint32_t pagesize =  nand_info->param.pagesize;
    uint32_t pageaddr;
    uint32_t columnaddr;
    uint32_t rlen;
    size_t align_len;
    struct flash_address flash_address;
    int32_t ret = 0, reterr = 0, ret_eccvalue = 0;

    while(len) {
        pageaddr = (uint32_t)from / pagesize;
        columnaddr = (uint32_t)from % pagesize;
        rlen = min_t(uint32_t, len, pagesize - columnaddr);

        /* align length */
        align_len = ALIGN(rlen, 4);

        /* create DMA Descriptors */
        if (align_len != rlen) {
            align_len -= 4;
        }

        if (align_len > 0) {
            flash_address.pageaddr = pageaddr;
            flash_address.columnaddr = columnaddr;
            flash_address.ops_mode = DMA_OPS;

            create_sfc_desc(flash->sfc, buf, align_len);

        /* DMA Descriptors read */
            ret = ingenic_sfc_nand_read(flash, &flash_address, buf, align_len);
            if (ret < 0) {
                printf("%s %s %d: ingenic_sfc_nand_read error, ret = %d, \
                        pageaddr = %u, columnaddr = %u, rlen = %zu\n",
                        __FILE__, __func__, __LINE__,
                        ret, pageaddr, columnaddr, align_len);
                reterr = ret;
                if(ret == -EIO)
                    break;
            } else if (ret > 0) {
                printf("%s %s %d: ingenic_sfc_nand_read, ecc value = %d, \
                        pageaddr = %u, columnaddr = %u, rlen = %zu\n",
                        __FILE__, __func__, __LINE__,
                        ret, pageaddr, columnaddr, align_len);
                ret_eccvalue = ret;
            }
        }

        if (align_len != rlen) {
            flash_address.pageaddr = pageaddr;
            flash_address.columnaddr = columnaddr + align_len;
            flash_address.ops_mode = CPU_OPS;

            ret = ingenic_sfc_nand_read(flash, &flash_address, (u_char *)buf + align_len, rlen - align_len);
            if (ret < 0) {
                printf("%s %s %d: ingenic_sfc_nand_read error, ret = %d, \
                        pageaddr = %u, columnaddr = %u, rlen = %zu\n",
                        __FILE__, __func__, __LINE__,
                        ret, flash_address.pageaddr, flash_address.columnaddr, rlen - align_len);
                reterr = ret;
                if (ret == -EIO)
                    break;
            } else if (ret > 0) {
                printf("%s %s %d: ingenic_sfc_nand_read, ecc value = %d, \
                        pageaddr = %u, columnaddr = %u, rlen = %zu\n",
                        __FILE__, __func__, __LINE__,
                        ret, flash_address.pageaddr, flash_address.columnaddr, rlen - align_len);
                ret_eccvalue = ret;
            }

        }

        len -= rlen;
        from += rlen;
        buf += rlen;
        *retlen += rlen;
    }

    return reterr ? reterr : (ret_eccvalue ? ret_eccvalue : ret);
}

static int ingenic_sfcnand_write(struct sfc_flash *flash, unsigned long to, size_t len, size_t *retlen, const u_char *buf)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    uint32_t pagesize =  nand_info->param.pagesize;
    uint32_t pageaddr;
    uint32_t columnaddr;
    uint32_t wlen;
    struct flash_address flash_address;
    int32_t ret;

    *retlen = 0;
    while (len) {
        pageaddr = (uint32_t)to / pagesize;
        columnaddr = (uint32_t)to % pagesize;
        wlen = min_t(uint32_t, pagesize - columnaddr, len);

        flash_address.pageaddr = pageaddr;
        flash_address.columnaddr = columnaddr;
        flash_address.ops_mode = DMA_OPS;

        /* create DMA Descriptors */
        create_sfc_desc(flash->sfc, (unsigned char *)buf, wlen);

        /* DMA Descriptors write */
        if ((ret = ingenic_sfc_nand_write(flash, (u_char *)buf, &flash_address, wlen))) {
            printf("%s %s %d : spi nand write fail, ret = %d, \
                pageaddr = %u, columnaddr = %u, wlen = %u\n",
                __FILE__, __func__, __LINE__, ret,
                pageaddr, columnaddr, wlen);
            break;
        }

        *retlen += wlen;
        len -= wlen;
        to += wlen;
        buf += wlen;
    }

    return ret;
}

static int32_t ingenic_sfcnand_read_oob(struct sfc_flash *flash, unsigned long from, struct mtd_oob_ops *ops)
{
    uint32_t addr = (uint32_t)from;
    struct ingenic_sfcnand_flashinfo *flash_info = flash->flash_info;
    int32_t ret = 0, ret_eccvalue = 0;
    struct flash_address flash_address;

    mutex_lock(&flash->lock);
    if (ops->datbuf) {
        flash_address.pageaddr = addr / flash_info->param.pagesize;
        flash_address.columnaddr = addr % flash_info->param.pagesize;
        flash_address.ops_mode = CPU_OPS;  /* If the length is word aligned, DMA_OPS can be used. */

        /* create DMA Descriptors */
        create_sfc_desc(flash->sfc, (unsigned char *)ops->datbuf, ops->len);

        ret = ingenic_sfc_nand_read(flash, &flash_address, ops->datbuf, ops->len);
        if (ret < 0) {
            printf("%s %s %d : spi nand read data error, ret = %d\n",__FILE__,__func__,__LINE__, ret);
            if (ret == -EIO) {
                goto read_oob_exit;
            } else {
                ret_eccvalue = ret;
            }
        }
    }

    if (ops->oobbuf) {
        flash_address.pageaddr = addr / flash_info->param.pagesize;
        flash_address.columnaddr = flash_info->param.pagesize + ops->ooboffs;
        flash_address.ops_mode = DMA_OPS;

        /* create DMA Descriptors */
        create_sfc_desc(flash->sfc, (unsigned char *)ops->oobbuf, ops->ooblen);

        ret = ingenic_sfc_nand_read(flash, &flash_address, ops->oobbuf, ops->ooblen);
        if (ret < 0)
            printf("%s %s %d : spi nand read oob error ,ret= %d\n", __FILE__, __func__, __LINE__, ret);

        if (ret != -EIO)
            ops->oobretlen = ops->ooblen;

    }

read_oob_exit:
    mutex_unlock(&flash->lock);

    return ret ? ret : ret_eccvalue;
}

static int ingenic_sfcnand_block_bad_check(struct sfc_flash *flash, unsigned long ofs)
{
    int check_len = 1;
    unsigned char check_buf[2] = {0x0};
    struct ingenic_sfcnand_flashinfo *flash_info = flash->flash_info;
    struct nand_chip *chip = flash_info->chip;
    struct mtd_oob_ops ops;

    memset(&ops, 0, sizeof(ops));
    if (chip->options & NAND_BUSWIDTH_16)
        check_len = 2;

    ops.oobbuf = check_buf;
    ops.ooblen = check_len;
    ingenic_sfcnand_read_oob(flash, ofs, &ops);

    if (badblk_check(check_len, check_buf))
        return 1;
    return 0;
}

static int nand_check_bad(struct sfc_flash *flash, unsigned long ofs)
{
    struct ingenic_sfcnand_flashinfo *flash_info = flash->flash_info;
    int block = ofs / flash_info->param.blocksize;

    if (!bbt_is_mark(block)) {
        int is_bad = ingenic_sfcnand_block_bad_check(flash, ofs);
        bbt_mark(block, is_bad);
        return is_bad;
    }

    return bbt_is_bad(block);
}

static int nand_default_bbt(struct sfc_flash *flash)
{
    int i = 0;
    struct ingenic_sfcnand_flashinfo *flash_info = flash->flash_info;
    int block = flash_info->param.flashsize / flash_info->param.blocksize;

    for (i = 0; i < block; i++)
        nand_check_bad(flash, i * flash_info->param.blocksize);

    return 0;
}

int soc_sfc_nand_flash_read(uint32_t from, uint32_t len, uint8_t *buf)
{
    size_t retlen = 0;
    int err = 0;
    assert(flash != NULL);

    mutex_lock(&flash->lock);
    err = ingenic_sfcnand_read(flash, from, len, &retlen, buf);
    mutex_unlock(&flash->lock);

    if (err < 0)
        return err;
    else
        return retlen;
}

int soc_sfc_nand_flash_write(uint32_t to, uint32_t len, uint8_t *buf)
{
    size_t retlen;
    assert(flash != NULL);

    mutex_lock(&flash->lock);
    ingenic_sfcnand_write(flash, to, len, &retlen, buf);
    mutex_unlock(&flash->lock);

    return retlen;
}

int soc_sfc_nand_flash_erase(uint32_t addr, uint32_t len)
{
    int ret;
    struct erase_info instr;
    instr.len = len;
    instr.addr = addr;
    assert(flash != NULL);

    mutex_lock(&flash->lock);
    ret = ingenic_sfcnand_erase(flash, &instr);
    mutex_unlock(&flash->lock);

    return ret;
}

static int ingenic_sfc_nand_get_feature(struct sfc_flash *flash, uint8_t addr, uint8_t *val)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    /* set index */
    xfer.cmd_index = NAND_GET_FEATURE;

    /* set addr */
    xfer.staaddr0 = addr;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = 1;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf = (uint8_t *)val;

    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error != %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    return 0;
}

int ingenic_sfc_nand_set_feature(struct sfc_flash *flash, uint8_t addr, uint32_t val)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    /* set index */
    xfer.cmd_index = NAND_SET_FEATURE;

    /* set addr */
    xfer.staaddr0 = addr;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = 1;
    xfer.config.data_dir = GLB_TRAN_DIR_WRITE;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf = (uint8_t *)&val;

    if(sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    return 0;
}

static int32_t ingenic_sfc_nand_dev_init(struct sfc_flash *flash)
{
    int32_t ret;
    /*release protect*/
    uint8_t feature = 0;
    if ((ret = ingenic_sfc_nand_set_feature(flash, SPINAND_ADDR_PROTECT, feature)))
        goto exit;

    if ((ret = ingenic_sfc_nand_get_feature(flash, SPINAND_ADDR_FEATURE, &feature)))
        goto exit;

    feature |= (1 << 4) | (1 << 3) | (1 << 0);
    if ((ret = ingenic_sfc_nand_set_feature(flash, SPINAND_ADDR_FEATURE, feature)))
        goto exit;

    return 0;
exit:
    return ret;
}

static int32_t ingenic_sfc_nand_try_id(struct sfc_flash *flash)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_device *nand_device;
    struct sfc_cdt_xfer xfer;
    uint8_t id_buf[3] = {0};
    unsigned short index[2] = {NAND_TRY_ID, NAND_TRY_ID_DMY};
    uint8_t i = 0;
    struct device_id_struct *device_id = NULL;
    char *name = NULL;
    int32_t id_count = 0;

    for (i = 0; i < 2; i++) {
        memset(&xfer, 0, sizeof(xfer));

        /* set index */
        xfer.cmd_index = index[i];

        /* set addr */
        xfer.rowaddr = 0;

        /* set transfer config */
        xfer.dataen = ENABLE;
        xfer.config.datalen = sizeof(id_buf);
        xfer.config.data_dir = GLB_TRAN_DIR_READ;
        xfer.config.ops_mode = CPU_OPS;
        xfer.config.buf = id_buf;

        if (sfc_sync(flash->sfc, &xfer)) {
            printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
            return -EIO;
        }

        list_for_each_entry(nand_device, &nand_list, list) {
            if (nand_device->id_manufactory == id_buf[0]) {
                device_id = nand_device->id_device_list;
                id_count = nand_device->id_device_count;
                while (id_count--) {
                    if (device_id->id_device>0x0 && device_id->id_device <= 0xff &&
                                    (device_id->id_device == id_buf[1])) {
                        nand_info->id_manufactory = id_buf[0];
                        nand_info->id_device = id_buf[1];
                        nand_info->param = *device_id->param;
                        name = device_id->name;
                        nand_flash_info.name = device_id->name;
                        goto found_param;
                    }
                    else if (device_id->id_device >0xff && device_id->id_device <= 0xffff &&
                                    device_id->id_device == (id_buf[2] | (id_buf[1]<<8))) {
                        nand_info->id_manufactory = id_buf[0];
                        nand_info->id_device = id_buf[1]<<8;
                        nand_info->id_device  |= id_buf[2];
                        nand_info->param = *device_id->param;
                        name = device_id->name;
                        nand_flash_info.name = device_id->name;
                        goto found_param;
                    }
                    device_id++;
                }
            }
        }
    }

    if (!nand_info->id_manufactory && !nand_info->id_device) {
        printf(" ERROR!: don`t support this nand manufactory, please add nand driver.\n");
        return -ENODEV;
    }

found_param:
    printf("Supported Nand Flash, %s(%02x:%02x)\n", name, nand_info->id_manufactory, nand_info->id_device);

    /* fill manufactory special operation and cdt params */
    nand_info->ops = &nand_device->ops;
    nand_info->cdt_params = nand_info->ops->get_cdt_params(flash, nand_info->id_device);

    if (!nand_info->ops->get_feature) {
        if (!nand_info->ops->deal_ecc_status) {
            printf("ERROR:xxx_nand.c \"get_feature()\" and \"deal_ecc_status()\" not define.\n");
            return -ENODEV;
        } else {
            nand_info->ops->get_feature = nand_common_get_feature;
        }
    } else {
        printf("use nand private get feature interface!\n");
    }

    return 0;
}

static void write_cdt(struct jz_sfc *sfc, struct sfc_cdt *cdt, uint16_t start_index, uint16_t end_index)
{
    uint32_t cdt_num, cdt_size;

    cdt_num = end_index - start_index + 1;
    cdt_size = sizeof(struct sfc_cdt);

    memcpy((void *)sfc->cdt_addr + (start_index * cdt_size), (void *)cdt + (start_index * cdt_size), cdt_num * cdt_size);
}

int ingenic_sfcnand_register(struct ingenic_sfcnand_device *flash) {
    list_add_tail(&flash->list, &nand_list);
    return 0;
}

void ingenic_sfcnand_unregister(struct ingenic_sfcnand_device *flash) {
    list_del(&flash->list);
}

/*
 *MK_CMD(cdt, cmd, LINK, ADDRMODE, DATA_EN)
 *MK_ST(cdt, st, LINK, ADDRMODE, ADDR_WIDTH, POLL_EN, DATA_EN, TRAN_MODE)
 */
static void params_to_cdt(cdt_params_t *params, struct sfc_cdt *cdt)
{
    /* 6. nand standard read */
    MK_CMD(cdt[NAND_STANDARD_READ_TO_CACHE], params->r_to_cache, 1, ROW_ADDR, DISABLE);
    MK_ST(cdt[NAND_STANDARD_READ_GET_FEATURE], params->oip, 1, STA_ADDR0, 1, ENABLE, DISABLE, TM_STD_SPI);
    MK_CMD(cdt[NAND_STANDARD_READ_FROM_CACHE], params->standard_r, 0, COL_ADDR, ENABLE);

    /* 7. nand quad read */
    MK_CMD(cdt[NAND_QUAD_READ_TO_CACHE], params->r_to_cache, 1, ROW_ADDR, DISABLE);
    MK_ST(cdt[NAND_QUAD_READ_GET_FEATURE], params->oip, 1, STA_ADDR0, 1, ENABLE, DISABLE, TM_STD_SPI);
    MK_CMD(cdt[NAND_QUAD_READ_FROM_CACHE], params->quad_r, 0, COL_ADDR, ENABLE);

    /* 8. nand standard write */
    MK_CMD(cdt[NAND_STANDARD_WRITE_ENABLE], params->w_en, 1, DEFAULT_ADDRMODE, DISABLE);
    MK_CMD(cdt[NAND_STANDARD_WRITE_TO_CACHE], params->standard_w_cache, 1, COL_ADDR, ENABLE);
    MK_CMD(cdt[NAND_STANDARD_WRITE_EXEC], params->w_exec, 1, ROW_ADDR, DISABLE);
    MK_ST(cdt[NAND_STANDARD_WRITE_GET_FEATURE], params->oip, 0, STA_ADDR0, 1, ENABLE, DISABLE, TM_STD_SPI);

    /* 9. nand quad write */
    MK_CMD(cdt[NAND_QUAD_WRITE_ENABLE], params->w_en, 1, DEFAULT_ADDRMODE, DISABLE);
    MK_CMD(cdt[NAND_QUAD_WRITE_TO_CACHE], params->quad_w_cache, 1, COL_ADDR, ENABLE);
    MK_CMD(cdt[NAND_QUAD_WRITE_EXEC], params->w_exec, 1, ROW_ADDR, DISABLE);
    MK_ST(cdt[NAND_QUAD_WRITE_GET_FEATURE], params->oip, 0, STA_ADDR0, 1, ENABLE, DISABLE, TM_STD_SPI);

    /* 10. block erase */
    MK_CMD(cdt[NAND_ERASE_WRITE_ENABLE], params->w_en, 1, DEFAULT_ADDRMODE, DISABLE);
    MK_CMD(cdt[NAND_BLOCK_ERASE], params->b_erase, 1, ROW_ADDR, DISABLE);
    MK_ST(cdt[NAND_ERASE_GET_FEATURE], params->oip, 0, STA_ADDR0, 1, ENABLE, DISABLE, TM_STD_SPI);

    /* 11. ecc status read */
    MK_CMD(cdt[NAND_ECC_STATUS_READ], params->ecc_r, 0, DEFAULT_ADDRMODE, ENABLE);

}

static void nand_create_cdt_table(struct jz_sfc *sfc, void *flash_info, uint32_t flag)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash_info;
    cdt_params_t *cdt_params;
    struct sfc_cdt sfc_cdt[NAND_INDEX_MAX_NUM];

    memset(sfc_cdt, 0, sizeof(sfc_cdt));
    if (flag & DEFAULT_CDT) {

        /* 1. reset */
        sfc_cdt[NAND_RESET].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
        sfc_cdt[NAND_RESET].xfer = CMD_XFER(0, DISABLE, 0, DISABLE, SPINAND_CMD_RESET);
        sfc_cdt[NAND_RESET].staExp = 0;
        sfc_cdt[NAND_RESET].staMsk = 0;

        /* 2. try id */
        sfc_cdt[NAND_TRY_ID].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
        sfc_cdt[NAND_TRY_ID].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, SPINAND_CMD_RDID);
        sfc_cdt[NAND_TRY_ID].staExp = 0;
        sfc_cdt[NAND_TRY_ID].staMsk = 0;

        /* 3. try id with dummy */
        /*
         * There are some NAND flash, try ID operation requires 8-bit dummy value to be all 0,
         * so use 1 byte address instead of dummy here.
         */
        sfc_cdt[NAND_TRY_ID_DMY].link = CMD_LINK(0, ROW_ADDR, TM_STD_SPI);
        sfc_cdt[NAND_TRY_ID_DMY].xfer = CMD_XFER(1, DISABLE, 0, ENABLE, SPINAND_CMD_RDID);
        sfc_cdt[NAND_TRY_ID_DMY].staExp = 0;
        sfc_cdt[NAND_TRY_ID_DMY].staMsk = 0;

        /* 4. set feature */
        sfc_cdt[NAND_SET_FEATURE].link = CMD_LINK(0, STA_ADDR0, TM_STD_SPI);
        sfc_cdt[NAND_SET_FEATURE].xfer = CMD_XFER(1, DISABLE, 0, ENABLE, SPINAND_CMD_SET_FEATURE);
        sfc_cdt[NAND_SET_FEATURE].staExp = 0;
        sfc_cdt[NAND_SET_FEATURE].staMsk = 0;

        /* 5. get feature */
        sfc_cdt[NAND_GET_FEATURE].link = CMD_LINK(0, STA_ADDR0, TM_STD_SPI);
        sfc_cdt[NAND_GET_FEATURE].xfer = CMD_XFER(1, DISABLE, 0, ENABLE, SPINAND_CMD_GET_FEATURE);
        sfc_cdt[NAND_GET_FEATURE].staExp = 0;
        sfc_cdt[NAND_GET_FEATURE].staMsk = 0;

        if (!(flag & UPDATE_CDT)) {
            /* first create cdt table (default)*/
            write_cdt(sfc, sfc_cdt, NAND_RESET, NAND_GET_FEATURE);
            return;
        }
    }

    if (flag & UPDATE_CDT) {
        cdt_params = nand_info->cdt_params;
        params_to_cdt(cdt_params, sfc_cdt);

        /* second create cdt table */
        if (!(flag & DEFAULT_CDT)) {
            /* second create cdt table (update)*/
            write_cdt(sfc, sfc_cdt, NAND_STANDARD_READ_TO_CACHE, NAND_ECC_STATUS_READ);
        } else {
            /* create full cdt table (default && update)*/
            write_cdt(sfc, sfc_cdt, NAND_RESET, NAND_ECC_STATUS_READ);
        }
    }
    //dump_cdt(sfc);
}

static struct ingenic_sfcnand_burner_param *burn_param;

static int32_t nand_partition_param_copy(struct sfc_flash *flash, struct ingenic_sfcnand_burner_param *burn_param) {
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    int ret;
    size_t retlen = 0;

    /* partition param copy */
    nand_info->partition.num_partition = burn_param->partition_num;

    burn_param->partition = calloc(sizeof(uint32_t), nand_info->partition.num_partition * sizeof(struct ingenic_sfcnand_partition));
    assert(burn_param->partition != NULL);
    memset(burn_param->partition, 0, nand_info->partition.num_partition * sizeof(struct ingenic_sfcnand_partition));

    nand_info->partition.partition = calloc(sizeof(uint32_t), nand_info->partition.num_partition * sizeof(struct mtd_partition));
    assert(nand_info->partition.partition != NULL);
    memset(nand_info->partition.partition, 0, nand_info->partition.num_partition * sizeof(struct mtd_partition));

    ret = ingenic_sfcnand_read(flash, flash->param_offset + sizeof(*burn_param) - sizeof(burn_param->partition), \
        nand_info->partition.num_partition * sizeof(struct ingenic_sfcnand_partition), \
        &retlen, (u_char *)burn_param->partition);
    if (ret < 0) {
        printf("read nand partition failed!\n");
        free(burn_param->partition);
        free(nand_info->partition.partition);
        return -EIO;
    }

    nand_info->partition.num_partition = burn_param->partition_num;
    memcpy(nand_info->partition.partition, burn_param->partition, nand_info->partition.num_partition * sizeof(struct ingenic_sfcnand_partition));

#ifdef DEBUG
    int i;
    for (i = 0; i < nand_info->partition.num_partition; i++) {
        printf("sfc_nand: name = %s\t size = 0x%x\t offset = 0x%x\t mask_flags = 0x%x\n", \
                nand_info->partition.partition[i].name, \
                nand_info->partition.partition[i].size, \
                nand_info->partition.partition[i].offset, \
                nand_info->partition.partition[i].mask_flags);
    }
#endif

    free(burn_param->partition);
    return 0;
}

static uint32_t get_partition_from_spinand(struct sfc_flash *flash)
{
    uint32_t ret = 0;
    size_t retlen = 0;

    flash->param_offset = SFC_FLASH_PARAMER_OFFSET;

    burn_param = calloc(sizeof(uint32_t), sizeof(struct ingenic_sfcnand_burner_param));
    assert(burn_param != NULL);
    memset(burn_param, 0, sizeof(struct ingenic_sfcnand_burner_param));

    ret = ingenic_sfcnand_read(flash, flash->param_offset, sizeof(struct ingenic_sfcnand_burner_param), &retlen, (u_char *)burn_param);
    if (ret < 0) {
        printf("read nand base param failed!\n");
        ret = -EIO;
        goto failed;
    }

    if (burn_param->magic_num != SPINAND_MAGIC_NUM) {
        printf("NOTICE: this flash haven`t param, magic_num:%x\n", burn_param->magic_num);
        ret = -EINVAL;
        goto failed;
    }

    if  (nand_partition_param_copy(flash, burn_param)) {
        ret = -ENOMEM;
        goto failed;
    }

    return 0;
failed:
    free(burn_param);
    return ret;
}

const struct storage_info *soc_sfc_nand_flash_info(void) {

    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;

    nand_flash_info.id = nand_info->id_manufactory;
    nand_flash_info.id = nand_flash_info.id << 8 | nand_info->id_device;
    nand_flash_info.chipsize = nand_info->param.flashsize;
    nand_flash_info.pagesize = nand_info->param.pagesize;
    nand_flash_info.erasesize = nand_info->param.blocksize;

    return &nand_flash_info;
}

int soc_get_nand_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size)
{
    int i = 0;
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_partition_param *partitions;

    partitions = &nand_info->partition;
    for (i = 0; i < partitions->num_partition; i++) {
        if (!strcmp(partitions->partition[i].name, name)) {
                memcpy(offset, &partitions->partition[i].offset, sizeof(unsigned int));
                memcpy(size, &partitions->partition[i].size, sizeof(unsigned int));
                return 0;
        }
    }

    return -1;
}

int sfc_clk_set_highspeed(struct sfc_flash *flash, unsigned long rate_clk)
{
    assert(flash != NULL);
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct jz_sfc *jz_sfc = flash->sfc;
    unsigned long max_rate;
    int quad_mode;

    if (!nand_info)
        return 0;

    if (!jz_sfc)
        return 0;

    if (!jz_sfc->clk)
        return 0;

    quad_mode = nand_info->param.need_quad;

    if (quad_mode) {
        /* quad mode max rate:104M*4 */
        max_rate = 416 * 1000000;
    } else {
        /* quad mode max rate:50M*4 */
        max_rate = 200 * 1000000;
    }

    if (rate_clk > max_rate)
        rate_clk = max_rate;

    jz_sfc->clk_rate = rate_clk;
    clk_set_rate(jz_sfc->clk, jz_sfc->clk_rate);

    if (jz_sfc->clk_rate >= 200 * 1000000)
        soc_sfc_smp_delay(jz_sfc, DEV_CONF_SMP_DELAY_180);

    return 0;
}

int soc_sfc_nand_flash_init(void)
{
    struct nand_chip *chip;
    uint32_t block;
    struct ingenic_sfcnand_flashinfo *nand_info;
    int32_t ret;

    flash = calloc(sizeof(uint8_t), sizeof(struct sfc_flash));
    assert(flash != NULL);
    memset(flash, 0x00, sizeof(struct sfc_flash));

    nand_info = calloc(sizeof(uint8_t), sizeof(struct ingenic_sfcnand_flashinfo));
    assert(nand_info != NULL);
    memset(nand_info, 0, sizeof(struct ingenic_sfcnand_flashinfo));

    flash->sfc = soc_sfc_init(CONFIG_SFC_DEFAULT_RATE);
    flash->flash_info = nand_info;

    flash->sfc->desc = cache_align_malloc(sizeof(struct sfc_desc) * SFC_DESC_MAX_NUM);
    assert(flash->sfc->desc);
    memset(flash->sfc->desc, 0, sizeof(struct sfc_desc) * SFC_DESC_MAX_NUM);

    flash->sfc->desc_max_num = SFC_DESC_MAX_NUM;

    if ((ret = nand_init()))
        goto err_nand_init;


#define THOLD    5
#define TSETUP    5
#define TSHSL_R        100
#define TSHSL_W        100

    soc_set_flash_timing(flash->sfc, THOLD, TSETUP, TSHSL_R, TSHSL_W);

    /* Try creating default CDT table */
    flash->create_cdt_table = nand_create_cdt_table;
    flash->create_cdt_table(flash->sfc, flash->flash_info, DEFAULT_CDT);

    if ((ret = ingenic_sfc_nand_dev_init(flash))) {
        printf("nand device init failed!\n");
        goto err_dev_init;
    }

    if ((ret = ingenic_sfc_nand_try_id(flash))) {
        printf("try device id failed!\n");
        goto err_chip_id;
    }

    /* Update to private CDT table */
    flash->create_cdt_table(flash->sfc, flash->flash_info, UPDATE_CDT);

    sfc_clk_set_highspeed(flash, CONFIG_SFC_RATE);

    soc_set_flash_timing(flash->sfc, nand_info->param.tHOLD,
            nand_info->param.tSETUP, nand_info->param.tSHSL_R, nand_info->param.tSHSL_W);

    block = nand_info->param.flashsize / nand_info->param.blocksize;

    uint32_t *bbt_bad = calloc(sizeof(uint32_t), (block + 31) / 32);
    assert(bbt_bad != NULL);
    memset(bbt_bad, 0, (block + 31) / 32);

    uint32_t *bbt_mark = calloc(sizeof(uint32_t), (block + 31) / 32);
    assert(bbt_mark != NULL);
    memset(bbt_mark, 0, (block + 31) / 32);

    flash_bbt.is_bad = bbt_bad;
    flash_bbt.is_mark = bbt_mark;

    chip = calloc(sizeof(uint8_t), sizeof(struct nand_chip));
    assert(chip != NULL);
    memset(chip, 0, sizeof(struct nand_chip));

    chip->select_chip = NULL;
    chip->badblockbits = 8;
    chip->scan_bbt = nand_default_bbt;
    chip->block_bad = ingenic_sfcnand_block_bad_check;
    chip->block_markbad = ingenic_sfcnand_chip_block_markbad;
    chip->bbt_erase_shift = chip->phys_erase_shift = ffs(nand_info->param.blocksize) - 1;
    if (!(chip->options & NAND_OWN_BUFFERS))
        chip->buffers = cache_align_malloc(sizeof(*chip->buffers));

    nand_info->chip = chip;

#ifndef CONFIG_SFC_NAND_JUMP_CHECK_BADBLOCK
    if ((ret = chip->scan_bbt(flash))) {
        printf("scan bbt failed\n");
        goto err_scan_bbt;
    }
#endif

    /* Set the bad block position */
    if (nand_info->param.pagesize > 512 || (chip->options & NAND_BUSWIDTH_16))
        chip->badblockpos = NAND_LARGE_BADBLOCK_POS;
    else
        chip->badblockpos = NAND_SMALL_BADBLOCK_POS;

    nand_info->param.pagesize = nand_info->param.pagesize;

    ret = get_partition_from_spinand(flash);
    if (ret < 0) {
        printf("get partition failed!\n");
        goto err_get_partition;
    }

    soc_sfc_nand_flash_info();

    soc_sfc_nand_support_to_fs();

    mutex_init(&flash->lock);
    return 0;

err_get_partition:
#ifndef CONFIG_SFC_NAND_JUMP_CHECK_BADBLOCK
err_scan_bbt:
#endif
    if (chip->buffers)
        free(chip->buffers);
    if (chip)
        free(chip);
    if (bbt_mark)
        free(bbt_mark);
    if (bbt_bad)
        free(bbt_bad);
err_chip_id:
err_dev_init:
    nand_deinit();
err_nand_init:
    if (flash->sfc->desc)
        free(flash->sfc->desc);
    if (flash->sfc)
        soc_sfc_deinit(flash->sfc);
    if (nand_info)
        free(nand_info);
    if (flash) {
        free(flash);
        flash = NULL;
    }

    return ret;
}

int soc_sfc_nand_is_badblock(uint32_t addr)
{
    assert(flash != NULL);

    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    assert(nand_info != NULL);

    struct nand_chip *chip = nand_info->chip;
    assert(chip != NULL);

    return chip->block_bad(flash, addr);
}

int soc_sfc_nand_mark_badblock(uint32_t addr)
{
    assert(flash != NULL);

    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    assert(nand_info != NULL);

    struct nand_chip *chip = nand_info->chip;
    assert(chip != NULL);

    return chip->block_markbad(flash, addr);
}

/**
*  @brief check block is bad or not before sfc nand read
*  @offset: read offset pointer
*  @size: how many data bytes to read
*  @buf: data buf to read

*  This function reads @size bytes of data to @offset of the FLASH device.
*  Returns size if read success.
*  Returns -1 if bad offset or length.
**/
int soc_sfc_nand_flash_read_check_badblock(uint32_t *offset, uint32_t size, uint8_t *buf)
{
    int ret = 0;
    unsigned int blockstart = 0;
    unsigned int blocksize = nand_flash_info.erasesize;
    unsigned int chipsize = nand_flash_info.chipsize;
    unsigned int tmp = *offset;
    unsigned int rlen, rsize = size;

    assert(blocksize);

    if (tmp < 0) {
        printf("Bad offset 0x%x \n", tmp);
        return -1;
    }

    while (rsize) {
        while (blockstart != (tmp & ~(blocksize - 1))) {
            blockstart = (tmp & ~(blocksize - 1));
            ret = sfc_nand_is_badblock(blockstart);
            if (ret == 1) {
                printf("Bad block at %x, from %x to %x will be skipped\n", blockstart, blockstart, blockstart + blocksize - 1);
                tmp = blockstart + blocksize;
                if (tmp > chipsize) {
                    printf("Current offset %x exceed chipsize: %x \n", tmp, chipsize);
                    return -1;
                }
            }
        }

        rlen = min_t(uint32_t, rsize, blocksize - tmp % blocksize);
        ret = soc_sfc_nand_flash_read(tmp, rlen, buf);

        if (ret != rlen) {
            printf("sfc read nand flash error, rlen(%d) != ret(%d)\n", rlen, ret);
            return -1;
        }
        tmp += rlen;
        rsize -= rlen;
        buf += rlen;
    }

    *offset += (tmp - *offset - size);
    return size - rsize;
}


/**
*  @brief check block is bad or not before sfc nand write
*
*  @offset: offset to write to
*  @size: how many data bytes to write
*  @buf: data buf to write
*  @current_block: Store the current block address

*  This function writes @size bytes of data to @offset of the FLASH device.
*  Returns 0  write success.
*  Returns -1 if bad offset or length or write failure.
**/

int soc_sfc_nand_flash_write_check_badblock(uint32_t offset, uint32_t size, uint8_t *buf)
{
    int retlen = 0;
    unsigned int blockstart = 0;
    unsigned int blocksize = nand_flash_info.erasesize;
    unsigned int pagesize = nand_flash_info.pagesize;

    if (offset < 0 || offset % blocksize + size > blocksize) {
        printf("Bad offset %d or length %d, block%d eraseblock size is %d \n", offset, size, offset / blocksize, blocksize);
        return -1;
    }

    if (offset % pagesize) {
        printf("write offset %d is not aligned to min. I/O size %d \n", offset, pagesize);
        return -1;
    }

    if (size % pagesize) {
        printf("write length %d is not aligned to min. I/O size %d \n", size, pagesize);
        return -1;
    }


    while (blockstart != (offset & ~(blocksize - 1))) {
        blockstart = (offset & ~(blocksize - 1));
        retlen = soc_sfc_nand_is_badblock(blockstart);
        if (retlen == 1) {
            printf("Bad block at %x, from %x to %x will be skipped\n", blockstart, blockstart, blockstart + blocksize - 1);
            return -1;
        }
    }

    retlen = soc_sfc_nand_flash_write(offset, size, buf);

    if (retlen != size) {
        retlen =  soc_sfc_nand_flash_erase(blockstart, blocksize);
        if (retlen < 0) {
            printf("erase block %d failed \n", blockstart);
            return -EIO;
        }

        soc_sfc_nand_mark_badblock(blockstart);
        printf("write failed,Bad block at %x\n", blockstart);
        return -1;
    }

    return 0;
}

void sfc_nand_flash_test(void)
{
    unsigned char *read_buffer;
    unsigned char *write_buffer;
    int buffer_len = 0x1000;         /* length: 4096 */
    int block_len = 0x40000;         /* block : 4 * 1024 * 64 */
    int flash_addr_start = 0x400000; /* offset: 4MByte */
    int ret = 0;
    int i = 0;

    read_buffer = malloc(buffer_len * sizeof(unsigned char));
    assert(read_buffer != NULL);

    write_buffer = malloc(buffer_len * sizeof(unsigned char));
    assert(write_buffer != NULL);

    /* erase */
    printf("Nandflash earse test:\n");
    ret = soc_sfc_nand_flash_erase(flash_addr_start, block_len);
    if (ret < 0) {
        printf("nand flash erase test failed!!!\n");
        return;
    }

    /* write */
    printf("Nandflash write test:\n");
    for (i = 0; i < buffer_len; i++)
        write_buffer[i] = i % 256;

    ret = soc_sfc_nand_flash_write(flash_addr_start, buffer_len, (uint8_t *)write_buffer);
    if (ret != buffer_len) {
        printf("ret = %d\n buffer_len = %d\n", ret, buffer_len);
        printf("nand flash write test failed!!!\n");
    }

    /* read */
    printf("Nandflash read test:\n");
    ret = soc_sfc_nand_flash_read(flash_addr_start, buffer_len, (uint8_t *)read_buffer);
    if (ret != buffer_len) {
        printf("ret = %d\n buffer_len = %d\n", ret, buffer_len);
        printf("nand flash write test failed!!!\n");
    }

    /* comparison */
    if ((ret = memcmp(write_buffer, read_buffer, buffer_len)) != 0) {
        printf("sfc nor flash test: comparison write/read buffer failed. ret= %d.\n", ret);

        printf("==========Dump Buffer===============\n");
        for (i=0; i<0x100; i++) {
            if ((i != 0) && (i%8 == 0)) {
                printf("\n");
            }
            printf("%02x:", read_buffer[i]);
        }
        printf("\n");
        return;
    }

    free(read_buffer);
    free(write_buffer);
}



static uint8_t *w_pagebuf;
static uint8_t *r_pagebuf;
static struct mtd_nand_partition *g_nand_flash_parts;

static void soc_sfc_nand_support_to_fs(void)
{
    int i;
    int num_partition;
    struct mtd_nand_partition *parts;

    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_base_param *param = &nand_info->param;

    num_partition = nand_info->partition.num_partition + 1;

    struct ingenic_sfcnand_partition *partition = nand_info->partition.partition;

    parts = calloc(num_partition, sizeof(struct mtd_nand_partition));
    assert(parts);
    if (parts != NULL) {
        for (i = 0; i < num_partition - 1; i++) {
            parts[i].name       = partition[i].name;
            parts[i].offset     = partition[i].offset;
            parts[i].sector_size = param->blocksize;
            parts[i].mask_flags = PART_FLAG_RDWR | PART_TYPE_MTD;
            if (partition[i].size == (uint32_t)(-1) )
                parts[i].size   = nand_flash_info.chipsize - parts[i].offset;
            else
                parts[i].size   = partition[i].size;
        }
        parts[i].name = NULL;
    }

    g_nand_flash_parts = parts;

    w_pagebuf = cache_align_malloc(param->pagesize + param->oobsize);
    memset(w_pagebuf, 0xff, param->pagesize + param->oobsize);

    r_pagebuf = cache_align_malloc(param->pagesize + param->oobsize);
    memset(r_pagebuf, 0xff, param->pagesize + param->oobsize);

    struct mtd_nand_device *mtd_info = calloc(1, sizeof(*mtd_info));
    assert(mtd_info != NULL);

    mtd_info->page_size = param->pagesize;
    mtd_info->ecc_max = param->ecc_max;
    mtd_info->oob_size = param->oobsize;
    mtd_info->oob_free = param->oobsize;
    mtd_info->pages_per_block = param->blocksize / param->pagesize;
    mtd_info->plane_num = 1;

    sfc_nand_partition_init(mtd_info);
}

struct mtd_nand_partition *soc_sfc_nand_flash_partition_information(void)
{
    return g_nand_flash_parts;
}

static int ingenic_sfcnand_write_oob2(struct sfc_flash *f, unsigned long addr, struct mtd_oob_ops *ops)
{
    struct ingenic_sfcnand_flashinfo *flash_info = f->flash_info;
    int32_t ret = 0, ret_eccvalue = 0;
    struct flash_address flash_address;
    if (ops->datbuf) {
        flash_address.pageaddr = addr / flash_info->param.pagesize;
        flash_address.columnaddr = addr % flash_info->param.pagesize;
        flash_address.ops_mode = DMA_OPS;
        create_sfc_desc(f->sfc, (unsigned char *)ops->datbuf, ops->len);
        ret = ingenic_sfc_nand_write(f, ops->datbuf, &flash_address, ops->len);
        if (ret < 0) {
            if (ret == -EIO) goto write_oob_exit;
            else ret_eccvalue = ret;
        }
    }
    if (ops->oobbuf) {
        flash_address.pageaddr = addr / flash_info->param.pagesize;
        flash_address.columnaddr = flash_info->param.pagesize + ops->ooboffs;
        flash_address.ops_mode = DMA_OPS;
        create_sfc_desc(f->sfc, (unsigned char *)ops->oobbuf, ops->ooblen);
        ret = ingenic_sfc_nand_write(f, ops->oobbuf, &flash_address, ops->ooblen);
        if (ret != -EIO) ops->oobretlen = ops->ooblen;
    }
write_oob_exit:
    return ret ? ret : ret_eccvalue;
}

static int32_t ingenic_sfcnand_read_oob2(struct sfc_flash *f, unsigned long from, struct mtd_oob_ops *ops)
{
    uint32_t addr = (uint32_t)from;
    struct ingenic_sfcnand_flashinfo *flash_info = f->flash_info;
    int32_t ret = 0, ret_eccvalue = 0;
    struct flash_address flash_address;
    if (ops->datbuf) {
        flash_address.pageaddr = addr / flash_info->param.pagesize;
        flash_address.columnaddr = addr % flash_info->param.pagesize;
        flash_address.ops_mode = DMA_OPS;
        create_sfc_desc(f->sfc, (unsigned char *)ops->datbuf, ops->len);
        ret = ingenic_sfc_nand_read(f, &flash_address, ops->datbuf, ops->len);
        if (ret < 0) {
            if (ret == -EIO) goto read_oob_exit;
            else ret_eccvalue = ret;
        }
    }
    if (ops->oobbuf) {
        flash_address.pageaddr = addr / flash_info->param.pagesize;
        flash_address.columnaddr = flash_info->param.pagesize + ops->ooboffs;
        flash_address.ops_mode = CPU_OPS;
        create_sfc_desc(f->sfc, (unsigned char *)ops->oobbuf, ops->ooblen);
        ret = ingenic_sfc_nand_read(f, &flash_address, ops->oobbuf, ops->ooblen);
        if (ret != -EIO) ops->oobretlen = ops->ooblen;
    }
read_oob_exit:
    return ret ? ret : ret_eccvalue;
}

int soc_sfc_nand_flash_write_page(uint32_t page, const uint8_t *data, uint32_t data_len, const uint8_t *spare, uint32_t spare_len)
{
    struct mtd_oob_ops ops = {0};
    uint32_t addr = page * nand_flash_info.pagesize;
    ops.datbuf = (uint8_t *)data; ops.len = data_len;
    ops.oobbuf = (uint8_t *)spare; ops.ooblen = spare_len;
    return ingenic_sfcnand_write_oob2(flash, addr, &ops);
}

int soc_sfc_nand_flash_read_page(uint32_t page, uint8_t *data, uint32_t data_len, uint8_t *spare, uint32_t spare_len)
{
    struct mtd_oob_ops ops = {0};
    uint32_t addr = page * nand_flash_info.pagesize;
    ops.datbuf = data; ops.len = data_len;
    ops.oobbuf = spare; ops.ooblen = spare_len;
    return ingenic_sfcnand_read_oob2(flash, addr, &ops);
}

int soc_sfc_nand_flash_erase_block(uint32_t block)
{
    uint32_t pages_per_block = (nand_flash_info.erasesize / nand_flash_info.pagesize);
    return ingenic_sfc_nand_erase_blk(flash, block * pages_per_block);
}
