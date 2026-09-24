#include <errno.h>
#include <mtd_driver_nand.h>
#include "filesystem/yaffs/yaffs/yaffs_guts.h"
#include "filesystem/yaffs/yaffs/yaffs_trace.h"

static int write_chunk(struct yaffs_dev *dev, int nand_chunk,
                       const u8 *data, int data_len,
                       const u8 *oob, int oob_len)
{
    int ret;

    if (!data || !data_len) {
        data = NULL;
        data_len = 0;
    }

    if (!oob || !oob_len) {
        oob = NULL;
        oob_len = 0;
    }

    ret = mtd_nand_write_page(MTD_NAND_DEVICE(dev->driver_context),
                           nand_chunk, data, data_len, oob, oob_len);

    return ret == EOK ? YAFFS_OK : YAFFS_FAIL;

}

static int read_chunk(struct yaffs_dev *dev, int nand_chunk,
                      u8 *data, int data_len,
                      u8 *oob, int oob_len,
                      enum yaffs_ecc_result *ecc_result)
{
    int ret;

    ret = mtd_nand_read_page(MTD_NAND_DEVICE(dev->driver_context), nand_chunk, data, data_len, oob, oob_len);

    if (ret == 0)
        *ecc_result = YAFFS_ECC_RESULT_NO_ERROR;
    else if (ret > 0)
        *ecc_result = YAFFS_ECC_RESULT_FIXED;
    else
        *ecc_result = YAFFS_ECC_RESULT_UNFIXED;

    return ret >= EOK ? YAFFS_OK : YAFFS_FAIL;
}

static int erase(struct yaffs_dev *dev, int block_no)
{
    int ret;
    ret = mtd_nand_erase_block(MTD_NAND_DEVICE(dev->driver_context), block_no);
    return ret >= EOK ? YAFFS_OK : YAFFS_FAIL;
}

static int mark_bad(struct yaffs_dev *dev, int block_no)
{
    int ret;
    ret = mtd_nand_mark_badblock(MTD_NAND_DEVICE(dev->driver_context), block_no);
    return ret >= EOK ? YAFFS_OK : YAFFS_FAIL;
}

static int check_bad(struct yaffs_dev *dev, int block_no)
{
    int ret;
    ret = mtd_nand_check_block(MTD_NAND_DEVICE(dev->driver_context), block_no);
    return ret ? YAFFS_FAIL : YAFFS_OK;
}

static int initialise(struct yaffs_dev *dev)
{
    return YAFFS_OK;
}

static int deinitialise(struct yaffs_dev *dev)
{
    return YAFFS_OK;
}

void yaffs_mtd_drv_install(struct yaffs_dev *dev)
{
    struct yaffs_driver *drv = &dev->drv;

    drv->drv_write_chunk_fn   = write_chunk;
    drv->drv_read_chunk_fn   = read_chunk;
    drv->drv_erase_fn        = erase;
    drv->drv_mark_bad_fn     = mark_bad;
    drv->drv_check_bad_fn    = check_bad;
    drv->drv_initialise_fn   = initialise;
    drv->drv_deinitialise_fn = deinitialise;
}
