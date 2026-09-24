/*
 * RT-Thread Device Interface for uffs
 */
#include <mtd_driver_nand.h>
#include "dfs_uffs.h"
#include <common.h>

static int nand_init_flash(uffs_Device *dev)
{
    return UFFS_FLASH_NO_ERR;
}

static int nand_release_flash(uffs_Device *dev)
{
    return UFFS_FLASH_NO_ERR;
}
static int nand_erase_block(uffs_Device *dev, unsigned block)
{
    int res;

    res = mtd_nand_erase_block(MTD_NAND_DEVICE(dev->_private), block);

    return res == EOK ? UFFS_FLASH_NO_ERR : UFFS_FLASH_IO_ERR;
}

#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
static int nand_check_block(uffs_Device *dev, unsigned block)
{
    int res;

    res = mtd_nand_check_block(MTD_NAND_DEVICE(dev->_private), block);

    return res == EOK ? UFFS_FLASH_NO_ERR : UFFS_FLASH_BAD_BLK;
}

static int nand_mark_badblock(uffs_Device *dev, unsigned block)
{
    int res;

    res = mtd_nand_mark_badblock(MTD_NAND_DEVICE(dev->_private), block);

    return res == EOK ? UFFS_FLASH_NO_ERR : UFFS_FLASH_IO_ERR;
}
#endif


static int nand_read_page(uffs_Device *dev,
                          u32          block,
                          u32          page,
                          u8          *data,
                          int          data_len,
                          u8          *ecc,
                          uint8_t  *spare,
                          int          spare_len)
{
    int res;

    page = block * dev->attr->pages_per_block + page;
    if (data == NULL && spare == NULL)
    {
#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
        assert(0); //should not be here
#else
        /* check block status: bad or good */
        uint8_t spare[UFFS_MAX_SPARE_SIZE];

        memset(spare, 0, UFFS_MAX_SPARE_SIZE);

        mtd_nand_read_page(MTD_NAND_DEVICE(dev->_private),
                         page, NULL, 0,
                         spare, dev->attr->spare_size);//dev->mem.spare_data_size

        res = spare[dev->attr->block_status_offs] == 0xFF ?
                               UFFS_FLASH_NO_ERR : UFFS_FLASH_BAD_BLK;

        return res;
#endif
    }

    mtd_nand_read_page(MTD_NAND_DEVICE(dev->_private),
                     page, data, data_len, spare, spare_len);

    return UFFS_FLASH_NO_ERR;
}

static int nand_write_page(uffs_Device *dev,
                           u32          block,
                           u32          page,
                           const u8    *data,
                           int          data_len,
                           const u8    *spare,
                           int          spare_len)
{
    int res;

    assert(UFFS_MAX_SPARE_SIZE >= dev->attr->spare_size);

    page = block * dev->attr->pages_per_block + page;

    if (data == NULL && spare == NULL)
    {
#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
        assert(0); //should not be here
#else
        /* mark bad block  */
        uint8_t spare[UFFS_MAX_SPARE_SIZE];

        memset(spare, 0xFF, UFFS_MAX_SPARE_SIZE);
        spare[dev->attr->block_status_offs] =  0x00;

        res = mtd_nand_write_page(MTD_NAND_DEVICE(dev->_private),
                                page, NULL, 0,
                                spare, dev->attr->spare_size);//dev->mem.spare_data_size
        if (res != EOK)
            goto __error;
#endif
    }

    res = mtd_nand_write_page(MTD_NAND_DEVICE(dev->_private),
                           page,  data, data_len, spare, spare_len);
    if (res != EOK)
        goto __error;

    return UFFS_FLASH_NO_ERR;

__error:
    return UFFS_FLASH_IO_ERR;
}





static int WritePageWithLayout(uffs_Device         *dev,
                               u32                  block,
                               u32                  page,
                               const u8            *data,
                               int                  data_len,
                               const u8            *ecc,  //NULL
                               const uffs_TagStore *ts)
{
    int res;
    int spare_len;
    uint8_t spare[UFFS_MAX_SPARE_SIZE];

    assert(UFFS_MAX_SPARE_SIZE >= dev->attr->spare_size);

    page = block * dev->attr->pages_per_block + page;
    spare_len = dev->mem.spare_data_size;

    if (data == NULL && ts == NULL)
    {
#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
        assert(0); //should not be here
#else
        /* mark bad block  */
        memset(spare, 0xFF, UFFS_MAX_SPARE_SIZE);
        spare[dev->attr->block_status_offs] =  0x00;

        res = mtd_nand_write_page(MTD_NAND_DEVICE(dev->_private),
                                page, NULL, 0,
                                spare, dev->attr->spare_size);//dev->mem.spare_data_size
        if (res != EOK) {
            goto __error;
        }

        dev->st.io_write++;
        return UFFS_FLASH_NO_ERR;
#endif
    }

    if (data != NULL && data_len != 0)
    {
        assert(data_len == dev->attr->page_data_size);

        dev->st.page_write_count++;
        dev->st.io_write += data_len;
    }

    if (ts != NULL)
    {
        uffs_FlashMakeSpare(dev, ts, NULL, (u8 *)spare);
        dev->st.spare_write_count++;
        dev->st.io_write += spare_len;
    }

    res = mtd_nand_write_page(MTD_NAND_DEVICE(dev->_private),
                            page, data, data_len, spare, spare_len);
    if (res != EOK)
        goto __error;

    return UFFS_FLASH_NO_ERR;

__error:
    return UFFS_FLASH_IO_ERR;
}


static URET ReadPageWithLayout(uffs_Device   *dev,
                               u32            block,
                               u32            page,
                               u8            *data,
                               int            data_len,
                               u8            *ecc,              //NULL
                               uffs_TagStore *ts,
                               u8            *ecc_store)        //NULL
{
    int res = UFFS_FLASH_NO_ERR;
    int spare_len;
    uint8_t spare[UFFS_MAX_SPARE_SIZE];

    assert(UFFS_MAX_SPARE_SIZE >= dev->attr->spare_size);

    page = block * dev->attr->pages_per_block + page;
    spare_len = dev->mem.spare_data_size;

    if (data == NULL && ts == NULL)
    {
#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
        assert(0); //should not be here
#else
        /* check block good or bad */


        mtd_nand_read_page(MTD_NAND_DEVICE(dev->_private),
                         page, NULL, 0,
                         spare, dev->attr->spare_size);//dev->mem.spare_data_size

        dev->st.io_read++;

        res = spare[dev->attr->block_status_offs] == 0xFF ?
                               UFFS_FLASH_NO_ERR : UFFS_FLASH_BAD_BLK;


        return res;
#endif
    }

    if (data != NULL)
    {
        dev->st.io_read += data_len;
        dev->st.page_read_count++;
    }

    res = mtd_nand_read_page(MTD_NAND_DEVICE(dev->_private),
                           page, data, data_len, spare, spare_len);
    if (res == 0)
        res = UFFS_FLASH_NO_ERR;
    else if (res > 0) {
        //TODO ecc correct, add code to use hardware do ecc correct
        res = UFFS_FLASH_ECC_OK;
    } else {
        printf("test read res ===== %d, page = %d, data = %p, data_len = %d, spare = %p, spare_len = %d\n", \
             res, page, data, data_len, spare, spare_len);
        res = UFFS_FLASH_ECC_FAIL;
    }

    if (ts != NULL)
    {
        // unload ts and ecc from spare, you can modify it if you like
        uffs_FlashUnloadSpare(dev, (const u8 *)spare, ts, NULL);

        if ((spare[spare_len - 1] == 0xFF) && (res == UFFS_FLASH_NO_ERR))
            res = UFFS_FLASH_NOT_SEALED;

        dev->st.io_read += spare_len;
        dev->st.spare_read_count++;
    }

    // printf("ReadPageWithLayout res = %d\n", res);

    return res;
}

const uffs_FlashOps ecc_hw_nand_ops =
{
    nand_init_flash,    /* InitFlash() */
    nand_release_flash, /* ReleaseFlash() */
    NULL,               /* ReadPage() */
    ReadPageWithLayout, /* ReadPageWithLayout */
    NULL,               /* WritePage() */
    WritePageWithLayout,/* WirtePageWithLayout */

#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
    nand_check_block,
    nand_mark_badblock,
#else
    NULL,               /* IsBadBlock(), let UFFS take care of it. */
    NULL,               /* MarkBadBlock(), let UFFS take care of it. */
#endif
    nand_erase_block,   /* EraseBlock() */
};

const uffs_FlashOps ecc_soft_nand_ops =
{
    nand_init_flash,    /* InitFlash() */
    nand_release_flash, /* ReleaseFlash() */
    nand_read_page,     /* ReadPage() */
    NULL,               /* ReadPageWithLayout */
    nand_write_page,    /* WritePage() */
    NULL,               /* WirtePageWithLayout */
#if defined(UFFS_USE_CHECK_MARK_FUNCITON)
    nand_check_block,
    nand_mark_badblock,
#else
    NULL,               /* IsBadBlock(), let UFFS take care of it. */
    NULL,               /* MarkBadBlock(), let UFFS take care of it. */
#endif
    nand_erase_block,   /* EraseBlock() */
};


uffs_FlashOps nand_ops;

static uint8_t hw_flash_data_layout[UFFS_SPARE_LAYOUT_SIZE] =
{
    0x05, 0x08, 0xFF, 0x00
};

static uint8_t hw_flash_ecc_layout[UFFS_SPARE_LAYOUT_SIZE] =
{
    0x00, 0x04, 0xFF, 0x00
};


void uffs_setup_storage(struct uffs_StorageAttrSt *attr,
                        struct mtd_nand_device *nand)
{
    memset(attr, 0, sizeof(struct uffs_StorageAttrSt));

//  attr->total_blocks = nand->end_block - nand->stablock + 1;/* no use */
    attr->page_data_size = nand->page_size;                /* page data size */
    attr->pages_per_block = nand->pages_per_block;         /* pages per block */
    attr->spare_size = nand->oob_size;                     /* page spare size */
    attr->ecc_opt = nand->ecc_max ? UFFS_ECC_HW_AUTO : UFFS_ECC_SOFT;               /* ecc option */
    attr->ecc_size = nand->oob_size - nand->oob_free;        /* ecc size */
    attr->block_status_offs = attr->ecc_size;              /* indicate block bad or good, offset in spare */
    attr->layout_opt = nand->ecc_max ? UFFS_LAYOUT_FLASH : UFFS_LAYOUT_UFFS;              /* let UFFS do the spare layout */

    if (!nand->ecc_max) {
        printf("nand ecc by soft\n");
        nand_ops = ecc_soft_nand_ops;
        return;
    }

    printf("nand ecc by uffs hw auto\n");

    nand_ops = ecc_hw_nand_ops;

    /* calculate the ecc layout array */
    hw_flash_data_layout[0] = attr->ecc_size + 1; /* ecc size + 1byte block status */
    hw_flash_data_layout[1] = 0x08;
    hw_flash_data_layout[2] = 0xFF;
    hw_flash_data_layout[3] = 0x00;

    hw_flash_ecc_layout[0] = 0;
    hw_flash_ecc_layout[1] = attr->ecc_size;
    hw_flash_ecc_layout[2] = 0xFF;
    hw_flash_ecc_layout[3] = 0x00;

    /* initialize  _uffs_data_layout and _uffs_ecc_layout */
    memcpy(attr->_uffs_data_layout, hw_flash_data_layout, UFFS_SPARE_LAYOUT_SIZE);
    memcpy(attr->_uffs_ecc_layout, hw_flash_ecc_layout, UFFS_SPARE_LAYOUT_SIZE);

    attr->data_layout = attr->_uffs_data_layout;
    attr->ecc_layout = attr->_uffs_ecc_layout;

}
