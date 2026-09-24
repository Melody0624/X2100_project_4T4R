#include <errno.h>
#include <assert.h>
#include <common.h>
#include <malloc.h>
#include <driver/clk.h>
#include <driver/cache.h>
#include "sfc_common.h"
#include "mtd_driver_nor.h"
#include "sfc_nor_params.h"
#include <driver/sfc_nor.h>
#include <os.h>

struct sfc_flash {
    struct jz_sfc *sfc;
    void *flash_info;
    struct mutex lock;
    struct mtd_nor_device mtd;
    struct mtd_nor_partition *nor_flash_parts;
    struct spi_nor_info *g_nor_info;
    unsigned short cur_r_cmd;
    unsigned short cur_w_cmd;
};

/*
 * standard mode max rate 50M
 * quad mode     max rate 80M  (2.7V ~ 3.0V power supply)
 * quad mode     max rate 104M (3.0V ~ 3.6V power supply)
 *
 */
#define CONFIG_SFC_DEFAULT_RATE         (200 * 1000000)

int sfc_nor_partition_init(void);

/*
 * load to cache: use cache to quickly fetch a critical address and
 * then fill the cache afterwards within a fixed length (8/16/32/64-byte) of
 * data without issuing multiple read commands
 */
#define L2CACHE_ALIGN_SIZE              128

static struct sfc_flash *flash = NULL;
static struct storage_info nor_flash_info;

static int __enter_4byte(struct sfc_flash *flash)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    /* set index */
    xfer.cmd_index = NOR_EN_4BYTE;

    /* set addr */
    xfer.columnaddr = 0;

    /* set transfer config */
    xfer.dataen = DISABLE;

    flash->sfc->xfer = &xfer;
    sfc_sync(flash->sfc, &xfer);

    return 0;
}


static void write_enable(void)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    /* set index */
    xfer.cmd_index = NOR_WRITE_ENABLE;

    /* set addr */
    xfer.columnaddr = 0;

    /* set transfer config */
    xfer.dataen = DISABLE;

    flash->sfc->xfer = &xfer;
    sfc_sync(flash->sfc, &xfer);
}


static int sfc_flash_read_chipid(void)
{
    int ret;
    unsigned char buf[3];
    uint32_t chip_id = 0;
    struct sfc_cdt_xfer xfer;

    memset(&xfer, 0, sizeof(xfer));

    /* set Index */
    xfer.cmd_index = NOR_READ_ID;

    /* set addr */
    xfer.columnaddr = 0;
    xfer.rowaddr    = 0;

    /* set transfer config */
    xfer.dataen          = ENABLE;
    xfer.config.datalen  = 3;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf      = buf;

    flash->sfc->xfer =   &xfer;

    ret = sfc_sync(flash->sfc, &xfer);
    if (ret) {
        printf("sfc_nor: read_chipid sfc_sync error !\n");
        return -EIO;
    }

    chip_id = ((buf[0] & 0xff) << 16) | ((buf[1] & 0xff) << 8) | (buf[2] & 0xff);

    //printf("sfc_nor: support flash(%s) ID = %x\n",spi_nor_info->name, chip_id);

    return chip_id;
}


static int sfc_flash_do_erase(uint32_t addr)
{
    struct sfc_cdt_xfer xfer;

    memset(&xfer, 0, sizeof(xfer));

    /* set Index */
    xfer.cmd_index = NOR_ERASE_WRITE_ENABLE;

    // /* active die */
    // if (flash->die_num > 1)
    // addr = ACTIVE_DIE(addr);

    /* set addr */
    xfer.rowaddr = addr;

    /* set transfer config */
    xfer.dataen = DISABLE;

    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    return 0;
}

/*
 * send 4byte command to enter 4byte mode
 */
static int sfc_flash_set_4byte_mode_normal(struct sfc_flash *flash)
{
    int ret = 0;

    switch (flash->g_nor_info->addr_ops_mode) {
        case 0:
            ret = __enter_4byte(flash);
            break;
        case 1:
            write_enable();
            ret = __enter_4byte(flash);
            break;
        default:
            break;
    }

    if (ret)
        printf("sfc_nor: enter 4byte mode failed\n");

    return ret;
}

static void inline set_quad_mode_cmd(void)
{
    flash->cur_r_cmd = NOR_READ_QUAD;
    flash->cur_w_cmd = NOR_WRITE_QUAD;
}

/* write nor flash status register QE bit to set quad mode */
static void set_quad_mode_reg(void)
{
    struct spi_nor_info *spi_nor_info;
    struct spi_nor_st_info *quad_set;

    struct sfc_cdt_xfer xfer;
    unsigned int data;

    /* 记录quad模式 */
    flash->cur_r_cmd = NOR_READ_QUAD;
    flash->cur_w_cmd = NOR_WRITE_QUAD;

    /* 设置为quad模式 */
    spi_nor_info = flash->g_nor_info;
    quad_set = &spi_nor_info->quad_set;
    data = (quad_set->val & quad_set->mask) << quad_set->bit_shift;

    /* 1. set nor quad */
    memset(&xfer, 0, sizeof(xfer));
    /* set index */
    xfer.cmd_index = NOR_QUAD_SET_ENABLE;

    /* set addr */
    xfer.columnaddr = 0;
    xfer.rowaddr = 0;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = quad_set->len;
    xfer.config.data_dir = GLB_TRAN_DIR_WRITE;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf = (uint8_t *)&data;

    flash->sfc->xfer = &xfer;

    sfc_sync(flash->sfc, &xfer);
}

static int sfc_flash_set_quad_mode(struct sfc_flash *flash)
{
    switch (flash->g_nor_info->quad_ops_mode) {
        case 0:
            set_quad_mode_cmd();
            break;
        case 1:
            set_quad_mode_reg();
            break;
        default:
            break;
    }

    return 0;
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

static unsigned int sfc_do_read(struct sfc_flash *flash, unsigned int addr, unsigned char *buf, size_t len)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    /* set Index */
    xfer.cmd_index = flash->cur_r_cmd;

    /* set addr */
    xfer.columnaddr = 0;
    xfer.rowaddr = addr;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = len;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = DMA_OPS;
    xfer.config.buf = buf;

    flash->sfc->retry_count = SFC_RETRY_COUNT;
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

    return len;
}

int soc_sfc_nor_flash_read(uint32_t from, uint32_t len, uint8_t *buf)
{
    int tmp_len = 0;

    mutex_lock(&flash->lock);

    /* create DMA Descriptors */
    create_sfc_desc(flash->sfc, buf, len);

    /* DMA Descriptors read */
    tmp_len = sfc_do_read(flash, (unsigned int)from, buf, len);

    mutex_unlock(&flash->lock);

    tmp_len = tmp_len > 0 ? tmp_len : 0;
    return tmp_len;
}

static unsigned  int sfc_do_write(unsigned int addr, unsigned int len, const uint8_t *buf)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    /* create DMA Descriptors */
    create_sfc_desc(flash->sfc, (unsigned char *)buf, len);
    /* set Index */
    xfer.cmd_index = flash->cur_w_cmd;

    /* active die */
    // if (flash->die_num > 1)
    // addr = ACTIVE_DIE(addr);

    /* set addr */
    xfer.columnaddr = 0;
    xfer.rowaddr = addr;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = len;
    xfer.config.data_dir = GLB_TRAN_DIR_WRITE;
    xfer.config.ops_mode = DMA_OPS;
    xfer.config.buf = (uint8_t *)buf;

    flash->sfc->retry_count = SFC_RETRY_COUNT;
retry:
    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    if (flash->sfc->retry_count > 0) {
        printf("SFC retry transfer! %s %s %d\n",__FILE__,__func__,__LINE__);
        goto retry;
    }
    return len;
}

int soc_sfc_nor_flash_write(uint32_t to, uint32_t len, const uint8_t *buf)
{
    unsigned int page_offset, actual_len;
    int ret = 0, len_tmp = 0;
    struct spi_nor_info *spi_nor_info;
    int writesize;

    spi_nor_info = flash->g_nor_info;
    writesize = spi_nor_info->page_size;

    mutex_lock(&flash->lock);

    page_offset = to & (spi_nor_info->page_size - 1);
    /* do all the bytes fit onto one page? */
    if (page_offset + len <= spi_nor_info->page_size) {
        write_enable();
        len_tmp = sfc_do_write(to, len, buf);
        ret += (len_tmp > 0 ? len_tmp : 0);
    } else {
        u32 i;

        /* the size of data remaining on the first page */
        actual_len = spi_nor_info->page_size - page_offset;
        write_enable();
        len_tmp = sfc_do_write(to, actual_len, buf);
        ret += (len_tmp > 0 ? len_tmp : 0);

        /* write everything in flash->page_size chunks */
        for (i = actual_len; i < len; i += writesize) {
            actual_len = len - i;
            if (actual_len >= writesize)
                actual_len = writesize;
            write_enable();
            len_tmp = sfc_do_write(to + i, actual_len, buf + i);
            ret += (len_tmp > 0 ? len_tmp : 0);
        }
    }

    mutex_unlock(&flash->lock);

    return ret;
}

int soc_sfc_nor_flash_erase(uint32_t addr, uint32_t len)
{
    int ret;
    uint32_t end;
    uint32_t erasesize = nor_flash_info.erasesize;

    mutex_lock(&flash->lock);

    if ((addr +len) % erasesize != 0)
        len = len - (len % erasesize) + erasesize;

    end = addr + len;
    while (addr < end) {
        ret = sfc_flash_do_erase(addr);
        if (ret) {
            printf("sfc_nor: erase error!\n");
            goto erase_exit;
        }
        addr += erasesize;
    }

erase_exit:
    mutex_unlock(&flash->lock);

    return ret;
}

#ifdef DEBUG_CLONER_PARAMS
static void dump_partition_params(struct mtd_nor_partition *parts, struct burner_params *params)
{
    int i = 0;
    int num_partition = 0;
    struct mtd_nor_partition *p_parts = NULL;
    struct norflash_partitions *burn_nor_parts = NULL;

    burn_nor_parts = &params->norflash_partitions;
    num_partition = burn_nor_parts->num_partition_info;

    printf("Name\t\t offset\t\t size\t\t mask\n");
    printf("Cloner Partition Param\n");
    for (i = 0; i<num_partition; i++) {
        printf("%-15s 0x%-8x\t 0x%-8x\t 0x%08x\n", burn_nor_parts->nor_partition[i].name,
                burn_nor_parts->nor_partition[i].offset,
                burn_nor_parts->nor_partition[i].size,
                burn_nor_parts->nor_partition[i].mask_flags);
    }

    printf("\n");
    printf("Mount Partition Param\n");
    for (p_parts = parts; p_parts->name != NULL; p_parts++) {
        printf("%-15s 0x%-8x\t 0x%-8x\t 0x%08x\n", p_parts->name,
                p_parts->offset,
                p_parts->size,
                p_parts->mask_flags);
    }
}

static void dump_cloner_params(struct burner_params *params)
{
    struct spi_nor_info *spi_nor_info;

    spi_nor_info = &params->spi_nor_info;
    printf("=============dump cloner params===========\n");
    printf("name                            = %s\n",   spi_nor_info->name);
    printf("id                              = 0x%x\n", spi_nor_info->id);

    printf("read_standard->cmd              = 0x%x\n", spi_nor_info->read_standard.cmd);
    printf("read_standard->dummy            = 0x%x\n", spi_nor_info->read_standard.dummy_byte);
    printf("read_standard->addr_nbyte       = 0x%x\n", spi_nor_info->read_standard.addr_nbyte);
    printf("read_standard->transfer_mode    = 0x%x\n", spi_nor_info->read_standard.transfer_mode);

    printf("read_quad->cmd                  = 0x%x\n", spi_nor_info->read_quad.cmd);
    printf("read_quad->dummy                = 0x%x\n", spi_nor_info->read_quad.dummy_byte);
    printf("read_quad->addr_nbyte           = 0x%x\n", spi_nor_info->read_quad.addr_nbyte);
    printf("read_quad->transfer_mode        = 0x%x\n", spi_nor_info->read_quad.transfer_mode);

    printf("write_standard->cmd             = 0x%x\n", spi_nor_info->write_standard.cmd);
    printf("write_standard->dummy           = 0x%x\n", spi_nor_info->write_standard.dummy_byte);
    printf("write_standard->addr_nbyte      = 0x%x\n", spi_nor_info->write_standard.addr_nbyte);
    printf("write_standard->transfer_mode   = 0x%x\n", spi_nor_info->write_standard.transfer_mode);

    printf("write_quad->cmd                 = 0x%x\n", spi_nor_info->write_quad.cmd);
    printf("write_quad->dummy               = 0x%x\n", spi_nor_info->write_quad.dummy_byte);
    printf("write_quad->addr_nbyte          = 0x%x\n", spi_nor_info->write_quad.addr_nbyte);
    printf("write_quad->transfer_mode       = 0x%x\n", spi_nor_info->write_quad.transfer_mode);

    printf("sector_erase->cmd               = 0x%x\n", spi_nor_info->sector_erase.cmd);
    printf("sector_erase->dummy             = 0x%x\n", spi_nor_info->sector_erase.dummy_byte);
    printf("sector_erase->addr_nbyte        = 0x%x\n", spi_nor_info->sector_erase.addr_nbyte);
    printf("sector_erase->transfer_mode     = 0x%x\n", spi_nor_info->sector_erase.transfer_mode);

    printf("wr_en->cmd                      = 0x%x\n", spi_nor_info->wr_en.cmd);
    printf("wr_en->dummy                    = 0x%x\n", spi_nor_info->wr_en.dummy_byte);
    printf("wr_en->addr_nbyte               = 0x%x\n", spi_nor_info->wr_en.addr_nbyte);
    printf("wr_en->transfer_mode            = 0x%x\n", spi_nor_info->wr_en.transfer_mode);

    printf("en4byte->cmd                    = 0x%x\n", spi_nor_info->en4byte.cmd);
    printf("en4byte->dummy                  = 0x%x\n", spi_nor_info->en4byte.dummy_byte);
    printf("en4byte->addr_nbyte             = 0x%x\n", spi_nor_info->en4byte.addr_nbyte);
    printf("en4byte->transfer_mode          = 0x%x\n", spi_nor_info->en4byte.transfer_mode);

    printf("quad_set->cmd                   = 0x%x\n", spi_nor_info->quad_set.cmd);
    printf("quad_set->bit_shift             = 0x%x\n", spi_nor_info->quad_set.bit_shift);
    printf("quad_set->mask                  = 0x%x\n", spi_nor_info->quad_set.mask);
    printf("quad_set->val                   = 0x%x\n", spi_nor_info->quad_set.val);
    printf("quad_set->len                   = 0x%x\n", spi_nor_info->quad_set.len);
    printf("quad_set->dummy                 = 0x%x\n", spi_nor_info->quad_set.dummy);

    printf("quad_get->cmd                   = 0x%x\n", spi_nor_info->quad_get.cmd);
    printf("quad_get->bit_shift             = 0x%x\n", spi_nor_info->quad_get.bit_shift);
    printf("quad_get->mask                  = 0x%x\n", spi_nor_info->quad_get.mask);
    printf("quad_get->val                   = 0x%x\n", spi_nor_info->quad_get.val);
    printf("quad_get->len                   = 0x%x\n", spi_nor_info->quad_get.len);
    printf("quad_get->dummy                 = 0x%x\n", spi_nor_info->quad_get.dummy);

    printf("busy->cmd                       = 0x%x\n", spi_nor_info->busy.cmd);
    printf("busy->bit_shift                 = 0x%x\n", spi_nor_info->busy.bit_shift);
    printf("busy->mask                      = 0x%x\n", spi_nor_info->busy.mask);
    printf("busy->val                       = 0x%x\n", spi_nor_info->busy.val);
    printf("busy->len                       = 0x%x\n", spi_nor_info->busy.len);
    printf("busy->dummy                     = 0x%x\n", spi_nor_info->busy.dummy);

    printf("quad_ops_mode                   = %d\n",   spi_nor_info->quad_ops_mode);
    printf("addr_ops_mode                   = %d\n",   spi_nor_info->addr_ops_mode);

    printf("tCHSH                           = %d\n",   spi_nor_info->tCHSH);
    printf("tSLCH                           = %d\n",   spi_nor_info->tSLCH);
    printf("tSHSL_RD                        = %d\n",   spi_nor_info->tSHSL_RD);
    printf("tSHSL_WR                        = %d\n",   spi_nor_info->tSHSL_WR);

    printf("chip_size                       = %d\n",   spi_nor_info->chip_size);
    printf("page_size                       = %d\n",   spi_nor_info->page_size);
    printf("erase_size                      = %d\n",   spi_nor_info->erase_size);

    printf("chip_erase_cmd                  = 0x%x\n", spi_nor_info->chip_erase_cmd);
}
#endif

static struct mtd_nor_partition *get_burner_partition_info(struct burner_params *burner_params)
{
    int i;
    int num_partition;
    struct mtd_nor_partition *parts;
    struct norflash_partitions *burn_nor_parts = NULL;

    burn_nor_parts = &burner_params->norflash_partitions;
    num_partition = burn_nor_parts->num_partition_info + 1;

    parts = malloc(num_partition * sizeof(struct mtd_nor_partition));
    if (parts != NULL) {
        for (i = 0; i < num_partition - 1; i++) {
            parts[i].name       = burn_nor_parts->nor_partition[i].name;
            parts[i].offset     = burn_nor_parts->nor_partition[i].offset;

            parts[i].sector_size= nor_flash_info.erasesize == 0 ? 4096 : nor_flash_info.erasesize;
            parts[i].mask_flags = PART_FLAG_RDWR | PART_TYPE_BLK;

            /* partition size is : -1 */
            if (burn_nor_parts->nor_partition[i].size == (-1) )
                parts[i].size   = burner_params->spi_nor_info.chip_size - parts[i].offset;
            else
                parts[i].size   = burn_nor_parts->nor_partition[i].size;
        }

        /* end of partition */
        parts[i].name = NULL;
    }

#ifdef DEBUG_CLONER_PARAMS
    dump_cloner_params(burner_params);
    dump_partition_params(parts, burner_params);
#endif

    return parts;
}

static int32_t sfc_nor_flash_get_read_params(uint32_t address, size_t len, uint8_t *buf)
{
    struct sfc_cdt_xfer xfer;
    memset(&xfer, 0, sizeof(xfer));

    xfer.cmd_index = NOR_READ_STANDARD;

    /* set addr */
    xfer.columnaddr = 0;
    xfer.rowaddr    = address;

    /* set transfer config */
    xfer.dataen          = ENABLE;
    xfer.config.datalen  = len;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf      = buf;

    flash->sfc->xfer = &xfer;

    if (sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_nor: get_read_params sfc_sync error !\n");
        return -EIO;
    }

    return 0;
}


static struct burner_params *sfc_nor_flash_get_params(struct sfc_flash *flash)
{
    int ret = 0;
    struct burner_params *burner_params = NULL;

    burner_params = malloc(sizeof(struct burner_params));
    if (!burner_params) {
        printf("sfc_nor: failed to alloc mem for params\n");
        goto err_params;
    }

    ret = sfc_nor_flash_get_read_params(SFC_FLASH_PARAMER_OFFSET, sizeof(struct burner_params), (uint8_t *)burner_params);
    if (ret < 0) {
        printf("sfc_nor: failed to read params (burned by Burner)\n");
        goto err_read_params;
    }

    if (burner_params->magic == NOR_MAGIC) {
        if (burner_params->version == NOR_VERSION) {
#ifdef CONFIG_DFS_ELMFAT
            /* 目前文件系统的块擦除大小 不支持 大于等于 32k大小的*/
            burner_params->spi_nor_info.erase_size = 0x1000;
            burner_params->spi_nor_info.sector_erase.cmd = CMD_ERASE_4K;
#endif
            nor_flash_info.id           = burner_params->spi_nor_info.id;
            nor_flash_info.name         = burner_params->spi_nor_info.name;
            nor_flash_info.pagesize     = burner_params->spi_nor_info.page_size;
            nor_flash_info.erasesize    = burner_params->spi_nor_info.erase_size;
            nor_flash_info.addrsize     = burner_params->spi_nor_info.read_standard.addr_nbyte;
            nor_flash_info.chipsize     = burner_params->spi_nor_info.chip_size;

            flash->nor_flash_parts = get_burner_partition_info(burner_params);
            assert(flash->nor_flash_parts != NULL);
            flash->g_nor_info = &burner_params->spi_nor_info;
        } /* end of NOR_VERSION */
    } /* end of NOR_MAGIC */

    if ( !(nor_flash_info.id) ) {
        printf("sfc_nor: WARNING ! cannot get flash information !!!\n");
        printf("sfc_nor: Magic is 0x%x  Version is 0x%x\n", burner_params->magic, burner_params->version);
        printf("sfc_nor: WARNING ! NOR Flash is nor support !!!\n");
        goto err_id_not_match;
    }

    return burner_params;

err_id_not_match:
    if (flash->nor_flash_parts)
        free(flash->nor_flash_parts);
err_read_params:
    free(burner_params);
    burner_params = NULL;

err_params:
    return NULL;
}

extern void sfc_nor_flash_test(void);

struct mtd_nor_partition *sfc_nor_flash_partition_information(void)
{
    return flash->nor_flash_parts;
}


static void write_cdt(struct jz_sfc *jz_sfc, struct sfc_cdt *cdt, uint16_t start_index, uint16_t end_index)
{
    uint32_t cdt_num, cdt_size;

    cdt_num = end_index - start_index + 1;
    cdt_size = sizeof(struct sfc_cdt);

    memcpy((void *)jz_sfc->cdt_addr + (start_index * cdt_size), (void *)cdt + (start_index * cdt_size), cdt_num * cdt_size);
}

static void params_to_cdt(struct spi_nor_info *params, struct sfc_cdt *cdt)
{
    /* 4.nor singleRead */
    MK_CMD(cdt[NOR_READ_STANDARD], params->read_standard, 0, ROW_ADDR, ENABLE);

    /* 5.nor quadRead */
    MK_CMD(cdt[NOR_READ_QUAD], params->read_quad, 0, ROW_ADDR, ENABLE);

#if 1
    /* 6. nor writeStandard */
    MK_CMD(cdt[NOR_WRITE_STANDARD_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
    MK_CMD(cdt[NOR_WRITE_STANDARD], params->write_standard, 1, ROW_ADDR, ENABLE);
    MK_ST(cdt[NOR_WRITE_STANDARD_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

    /* 7. nor writeQuad */
    MK_CMD(cdt[NOR_WRITE_QUAD_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
    MK_CMD(cdt[NOR_WRITE_QUAD], params->write_quad, 1, ROW_ADDR, ENABLE);
    MK_ST(cdt[NOR_WRITE_QUAD_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

    /* 8. nor erase */
    MK_CMD(cdt[NOR_ERASE_WRITE_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
    MK_CMD(cdt[NOR_ERASE], params->sector_erase, 1, ROW_ADDR, DISABLE);
    MK_ST(cdt[NOR_ERASE_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);
#endif

    /* 9. quad mode */
    if (params->quad_ops_mode) {
        MK_CMD(cdt[NOR_QUAD_SET_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
        MK_ST(cdt[NOR_QUAD_SET], params->quad_set, 1, DEFAULT_ADDRMODE, 0, DISABLE, ENABLE, TM_STD_SPI);  //disable poll, enable data
        MK_ST(cdt[NOR_QUAD_FINISH], params->busy, 1, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);
        MK_ST(cdt[NOR_QUAD_GET], params->quad_get, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);
    }

    /* 10. nor write ENABLE */
    MK_CMD(cdt[NOR_WRITE_ENABLE], params->wr_en, 0, DEFAULT_ADDRMODE, DISABLE);

    /* 11. entry 4byte mode */
    MK_CMD(cdt[NOR_EN_4BYTE], params->en4byte, 0, DEFAULT_ADDRMODE, DISABLE);

    /* 12. chip erase */
    MK_CMD(cdt[NOR_CHIP_ERASE_WRITE_ENABLE], params->wr_en, 1, DEFAULT_ADDRMODE, DISABLE);
    cdt[NOR_CHIP_ERASE].link = CMD_LINK(1, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_CHIP_ERASE].xfer = CMD_XFER(0, DISABLE, 0, DISABLE, params->chip_erase_cmd);
    cdt[NOR_CHIP_ERASE].staExp = 0;
    cdt[NOR_CHIP_ERASE].staMsk = 0;
    MK_ST(cdt[NOR_CHIP_ERASE_FINISH], params->busy, 0, DEFAULT_ADDRMODE, 0, ENABLE, DISABLE, TM_STD_SPI);

    /* 13. die select */
    cdt[NOR_DIE_SELECT].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_DIE_SELECT].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, CMD_DIE_SEL);
    cdt[NOR_DIE_SELECT].staExp = 0;
    cdt[NOR_DIE_SELECT].staMsk = 0;

    /* 14. read active die ID */
    cdt[NOR_READ_ACTIVE_DIE_ID].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_READ_ACTIVE_DIE_ID].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, CMD_READ_DIE_ID);
    cdt[NOR_READ_ACTIVE_DIE_ID].staExp = 0;
    cdt[NOR_READ_ACTIVE_DIE_ID].staMsk = 0;
}

static void create_cdt_table(struct sfc_flash *flash, uint32_t flag)
{
    struct spi_nor_info *flash_info;
    struct sfc_cdt cdt[NOR_INDEX_MAX_NUM];

    memset(cdt, 0, sizeof(cdt));

    /* 1.nor reset */
    cdt[NOR_RESET_ENABLE].link = CMD_LINK(1, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_RESET_ENABLE].xfer = CMD_XFER(0, DISABLE, 0, DISABLE, CMD_RSTEN);
    cdt[NOR_RESET_ENABLE].staExp = 0;
    cdt[NOR_RESET_ENABLE].staMsk = 0;

    cdt[NOR_RESET].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_RESET].xfer = CMD_XFER(0, DISABLE, 0, DISABLE, CMD_RST);
    cdt[NOR_RESET].staExp = 0;
    cdt[NOR_RESET].staMsk = 0;

    /* 2.nor read id */
    cdt[NOR_READ_ID].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_READ_ID].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, CMD_RDID);
    cdt[NOR_READ_ID].staExp = 0;
    cdt[NOR_READ_ID].staMsk = 0;

    /* 3. nor get status */
    cdt[NOR_GET_STATUS].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_GET_STATUS].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, CMD_RDSR);
    cdt[NOR_GET_STATUS].staExp = 0;
    cdt[NOR_GET_STATUS].staMsk = 0;

    cdt[NOR_GET_STATUS_1].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_GET_STATUS_1].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, CMD_RDSR_1);
    cdt[NOR_GET_STATUS_1].staExp = 0;
    cdt[NOR_GET_STATUS_1].staMsk = 0;

    cdt[NOR_GET_STATUS_2].link = CMD_LINK(0, DEFAULT_ADDRMODE, TM_STD_SPI);
    cdt[NOR_GET_STATUS_2].xfer = CMD_XFER(0, DISABLE, 0, ENABLE, CMD_RDSR_2);
    cdt[NOR_GET_STATUS_2].staExp = 0;
    cdt[NOR_GET_STATUS_2].staMsk = 0;

    if (flag == DEFAULT_CDT) {
        /* 4.nor singleRead */
        cdt[NOR_READ_STANDARD].link = CMD_LINK(0, ROW_ADDR, TM_STD_SPI);
        cdt[NOR_READ_STANDARD].xfer = CMD_XFER(DEFAULT_ADDRSIZE, DISABLE, 0, ENABLE, CMD_READ);
        cdt[NOR_READ_STANDARD].staExp = 0;
        cdt[NOR_READ_STANDARD].staMsk = 0;
        /* first create cdt table */
        write_cdt(flash->sfc, cdt, NOR_RESET_ENABLE, NOR_READ_STANDARD);
    }

    if (flag == UPDATE_CDT) {
        flash_info = flash->g_nor_info;
        params_to_cdt(flash_info, cdt);
        write_cdt(flash->sfc, cdt, NOR_READ_STANDARD, NOR_READ_ACTIVE_DIE_ID);
    }
#ifdef SFC_NOR_DEBUG
    dump_cdt(flash->sfc);
#endif
}

static int sfc_flash_reset(struct jz_sfc *sfc)
{
    int ret = 0;
    struct sfc_cdt_xfer xfer;

    memset(&xfer, 0, sizeof(xfer));
    // sfc_list_init(&transfer);

    /* set Index */
    xfer.cmd_index = NOR_RESET_ENABLE;

    /* set addr */
    xfer.rowaddr = 0;
    xfer.columnaddr = 0;

    /* set transfer config */
    xfer.dataen = DISABLE;

    // flash->sfc->xfer = &xfer;
    ret = sfc_sync(sfc, &xfer);
    if (ret) {
        printf("sfc_nor: sfc_sync error !\n");
        ret = -EIO;
    }

    udelay(100);

    return ret;
}

static void sfc_flash_default_standard_mode(struct sfc_flash *flash)
{
    flash->cur_r_cmd = NOR_READ_STANDARD;
    flash->cur_w_cmd = NOR_WRITE_STANDARD;
}

int sfc_clk_set_highspeed(struct sfc_flash *flash, unsigned long rate_clk)
{
    assert(flash != NULL);

    struct burner_params *burner_params = (struct burner_params *)flash->flash_info;
    struct jz_sfc *jz_sfc = flash->sfc;
    unsigned long max_rate;
    int tchsh;
    int tslch;
    int tshsl_rd;
    int tshsl_wr;
    int quad_mode;

    if (!burner_params)
        return 0;

    if (!jz_sfc)
        return 0;

    if (!jz_sfc->clk)
        return 0;

    quad_mode = burner_params->nor_pri_data.uk_quad;
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

    /* re-setting flash control timing */
    tchsh     = burner_params->spi_nor_info.tCHSH;
    tslch     = burner_params->spi_nor_info.tSLCH;
    tshsl_rd  = burner_params->spi_nor_info.tSHSL_RD;
    tshsl_wr  = burner_params->spi_nor_info.tSHSL_WR;

    soc_set_flash_timing(flash->sfc, tchsh, tslch, tshsl_rd, tshsl_wr);

    if (jz_sfc->clk_rate >= 200 * 1000000)
        soc_sfc_smp_delay(jz_sfc, DEV_CONF_SMP_DELAY_180);

    return 0;
}

struct spi_nor_info_tag {
    char tag[8];
    int array_size;
};

int soc_sfc_nor_flash_init(void)
{
    int ret = 0;

    flash = malloc(sizeof(struct sfc_flash));
    assert(flash != NULL);
    memset(flash, 0x00, sizeof(struct sfc_flash));

    flash->sfc = soc_sfc_init(CONFIG_SFC_DEFAULT_RATE);

    flash->sfc->desc = malloc(sizeof(struct sfc_desc) * SFC_DESC_MAX_NUM);
    assert(flash->sfc->desc);
    memset(flash->sfc->desc, 0, sizeof(struct sfc_desc) * SFC_DESC_MAX_NUM);

    flash->sfc->desc_max_num = SFC_DESC_MAX_NUM;

    create_cdt_table(flash, DEFAULT_CDT);

    sfc_flash_reset(flash->sfc);

    sfc_flash_default_standard_mode(flash);

    flash->flash_info = sfc_nor_flash_get_params(flash);
    if (flash->flash_info == NULL) {
        ret = -EACCES;
        goto error_get_params;
    }

    unsigned int nor_id = sfc_flash_read_chipid();
    if (nor_id == -EIO)
        goto error_chip_unsupport;

    struct burner_params *burner_params = flash->flash_info;
    if (nor_id != burner_params->spi_nor_info.id) {
        struct spi_nor_info_tag tag;
        sfc_nor_flash_get_read_params(CONFIG_EXTRA_NOR_INFO_OFF, sizeof(tag), (void *)&tag);
        if (!strncmp(tag.tag, "nor_tag", sizeof(tag.tag))) {
            struct spi_nor_info info;
            int i;
            for (i = 0; i < tag.array_size; i++) {
                int off = CONFIG_EXTRA_NOR_INFO_OFF + sizeof(tag) + i*sizeof(info);
                sfc_nor_flash_get_read_params(off, sizeof(info), (void *)&info);
                if (nor_id == info.id) {
#ifdef CONFIG_DFS_ELMFAT
                    /* 目前文件系统的块擦除大小 不支持 大于等于 32k大小的*/
                    info.erase_size = 0x1000;
                    info.sector_erase.cmd = CMD_ERASE_4K;
#endif
                    burner_params->spi_nor_info = info;
                    nor_flash_info.id           = info.id;
                    nor_flash_info.name         = info.name;
                    nor_flash_info.pagesize     = info.page_size;
                    nor_flash_info.erasesize    = info.erase_size;
                    nor_flash_info.addrsize     = info.read_standard.addr_nbyte;
                    nor_flash_info.chipsize     = info.chip_size;
                    break;
                }
            }
            if (i == tag.array_size)
                printf("not match extra nor info: %x\n", nor_id);
        } else {
            printf("not found extra nor info array\n");
        }
    }

    if (nor_id != burner_params->spi_nor_info.id)
        goto error_chip_unsupport;
    
    printf("sfc_nor: find chip:%s id:0x%x\n", burner_params->spi_nor_info.name, burner_params->spi_nor_info.id);

    sfc_clk_set_highspeed(flash, CONFIG_SFC_RATE);

    create_cdt_table(flash, UPDATE_CDT);

    sfc_flash_set_quad_mode(flash);

    /* if nor flash size is greater than 16M, use 4byte mode */
    if (nor_flash_info.chipsize  > NOR_SIZE_16M)
        sfc_flash_set_4byte_mode_normal(flash);

    mutex_init(&flash->lock);

    sfc_nor_partition_init();

    return 0;

error_chip_unsupport:
    if (flash->nor_flash_parts)
        free(flash->nor_flash_parts);

error_get_params:
    if (flash->flash_info)
        free(flash->flash_info);

    if (flash->sfc) {
        if (flash->sfc->desc)
            free(flash->sfc->desc);
        free(flash->sfc);
    }

    free(flash);
    return ret;
}

const struct storage_info *soc_sfc_nor_flash_info(void)
{
    return &nor_flash_info;
}

int soc_get_nor_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size)
{
    int i = 0;
    struct burner_params *nor_info = flash->flash_info;
    struct norflash_partitions *partitions;

    partitions = &nor_info->norflash_partitions;
    for (i = 0; i < partitions->num_partition_info; i++) {
        if (!strcmp(partitions->nor_partition[i].name, name)) {
                memcpy(offset, &partitions->nor_partition[i].offset, sizeof(unsigned int));
                memcpy(size, &partitions->nor_partition[i].size, sizeof(unsigned int));
                return 0;
        }
    }

    return -1;
}

void sfc_nor_flash_test(void)
{
    unsigned char *read_buffer;
    unsigned char *write_buffer;
    int buffer_test_len = 0x1000;       /* length: 4096 */
    int flash_addr_start = 0x400000;    /* offset: 4MByte */
    int ret = 0;
    int i = 0;

    read_buffer = malloc(buffer_test_len);
    write_buffer = malloc(buffer_test_len);

    assert(read_buffer != NULL);
    memset(read_buffer, 0, buffer_test_len);

    assert(write_buffer != NULL);
    memset(write_buffer, 0, buffer_test_len);

    /* erase */
    printf("NorFlash Erase Test:\n");
    ret = soc_sfc_nor_flash_erase(flash_addr_start, buffer_test_len);
    if (ret < 0) {
        printf("sfc nor flash test: erase failed. ret=%d\n", ret);
        return;
    }

    /* write */
    printf("NorFlash Write Test:\n");
    for (i = 0; i < buffer_test_len; i++)
        write_buffer[i] = i % 256;

    ret = soc_sfc_nor_flash_write(flash_addr_start, buffer_test_len, (uint8_t *)write_buffer);
    if (ret != buffer_test_len)
        printf("sfc nor flash test: write lenght(%d) not equal actual(%d).\n", buffer_test_len, ret);

    /* read */
    printf("NorFlash Read Test:\n");
    ret = soc_sfc_nor_flash_read(flash_addr_start, buffer_test_len, (uint8_t *)read_buffer);
    if (ret != buffer_test_len)
        printf("sfc nor flash test: read lenght(%d) not equal actual(%d).\n", buffer_test_len, ret);

    /* comparison */
    if ((ret = memcmp(write_buffer, read_buffer, buffer_test_len)) != 0) {
        printf("sfc nor flash test: comparison write/read buffer failed. ret= %d.\n", ret);

        printf("==========Dump Buffer===============\n");
        for (i=0; i<0x100; i++) {
            if ( (i !=0) && (i%8 == 0))
                printf("\n");
            printf("%04x:", read_buffer[i]);
        }
        printf("\n");

        return;
    }

    printf("NorFlash Test Successfully.\n");

    free(read_buffer);
    free(write_buffer);
}