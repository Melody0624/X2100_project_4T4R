#include "../sfc_common.h"
#include "../sfc_nand_params.h"
#include "nand_common.h"
#include <errno.h>

/*
 * Note: the deal_ecc_status() function needs to be defined in xxx_nand.c
 */
int32_t nand_common_get_feature(struct sfc_flash *flash, uint8_t flag)
{
    struct ingenic_sfcnand_flashinfo *nand_info = flash->flash_info;
    struct ingenic_sfcnand_ops *ops = nand_info->ops;
    uint16_t device_id = nand_info->id_device;
    struct sfc_cdt_xfer xfer;
    uint8_t ecc_status = 0;
    int32_t ret = 0;

retry:
    ecc_status = 0;

    memset(&xfer, 0, sizeof(xfer));

    /*set index*/
    xfer.cmd_index = NAND_GET_FEATURE;

    /* set addr */
    xfer.staaddr0 = SPINAND_ADDR_STATUS;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = 1;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf = &ecc_status;

    if(sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync_cdt error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }

    if(ecc_status & SPINAND_IS_BUSY)
        goto retry;

    switch(flag) {
        case GET_WRITE_STATUS:
            if(ecc_status & (0x1 << 3))
                ret = -EIO;
            break;

        case GET_ERASE_STATUS:
            if(ecc_status & (0x1 << 2))
                ret = -EIO;
            break;

        case GET_ECC_STATUS:
            ret = ops->deal_ecc_status(flash, device_id, ecc_status);
            break;
        default:
            printf("flag value is error ! %s %s %d\n",__FILE__,__func__,__LINE__);
    }
    return ret;
}

int32_t nand_get_ecc_conf(struct sfc_flash *flash, uint8_t addr)
{
    struct sfc_cdt_xfer xfer;
    uint32_t buf = 0;

    memset(&xfer, 0, sizeof(xfer));

    /*set index*/
    xfer.cmd_index = NAND_GET_FEATURE;

    /* set addr */
    xfer.staaddr0 = addr;

    /* set transfer config */
    xfer.dataen = ENABLE;
    xfer.config.datalen = 1;
    xfer.config.data_dir = GLB_TRAN_DIR_READ;
    xfer.config.ops_mode = CPU_OPS;
    xfer.config.buf = (uint8_t *)&buf;

    if(sfc_sync(flash->sfc, &xfer)) {
        printf("sfc_sync error ! %s %s %d\n",__FILE__,__func__,__LINE__);
        return -EIO;
    }
    return buf;
}

int nand_init(void)
{
    int ret = 0;
    ret = ato_nand_init();
    if (ret)
        return ret;

    ret = cochipgo_nand_init();
    if (ret)
        return ret;

    ret = gd_nand_init();
    if (ret)
        return ret;

    ret = esmt_nand_init();
    if (ret)
        return ret;

    ret = mxic_nand_init();
    if (ret)
        return ret;

    ret = winbond_nand_init();
    if (ret)
        return ret;

    ret = xtx_nand_init();
    if (ret)
        return ret;

    ret = fs_nand_init();
    if (ret)
        return ret;

    ret = zetta_nand_init();
    if (ret)
        return ret;

    ret = dosilicon_nand_init();
    if (ret)
        return ret;

    ret = fm_nand_init();
    if (ret)
        return ret;

    ret = issi_nand_init();
    if (ret)
        return ret;

    ret = issi_mid9d_nand_init();
    if (ret)
        return ret;

    ret = tc_nand_init();
    if (ret)
        return ret;

    ret = xcsp_nand_init();
    if (ret)
        return ret;

    ret = xtx_mid0b_nand_init();
    if (ret)
        return ret;

    ret = xtx_mid2c_nand_init();
    if (ret)
        return ret;

    ret = yhy_midc9_nand_init();
    if (ret)
        return ret;

    ret = etron_nand_init();
    if (ret)
        return ret;

    ret = gsto_nand_init();
    if (ret)
        return ret;

    ret = hik_nand_init();
    if (ret)
        return ret;

    ret = kowin_mid01_nand_init();
    if (ret)
        return ret;

    ret = kowin_midc9_nand_init();
    if (ret)
        return ret;

    ret = kowin_nand_init();
    if (ret)
        return ret;

    ret = micron_nand_init();
    if (ret)
        return ret;

    ret = unim_nand_init();
    if (ret)
        return ret;

    ret = wodposit_nand_init();
    if (ret)
        return ret;

    return 0;
}

void nand_deinit(void)
{
    ato_nand_deinit();
    cochipgo_nand_deinit();
    gd_nand_deinit();
    esmt_nand_deinit();
    mxic_nand_deinit();
    winbond_nand_deinit();
    xtx_nand_deinit();
    fs_nand_deinit();
    zetta_nand_deinit();
    dosilicon_nand_deinit();
    fm_nand_deinit();
    issi_nand_deinit();
    issi_mid9d_nand_deinit();
    tc_nand_deinit();
    xcsp_nand_deinit();
    xtx_mid0b_nand_deinit();
    xtx_mid2c_nand_deinit();
    yhy_midc9_nand_deinit();
    etron_nand_deinit();
    gsto_nand_deinit();
    hik_nand_deinit();
    kowin_mid01_nand_deinit();
    kowin_midc9_nand_deinit();
    kowin_nand_deinit();
    micron_nand_deinit();
    unim_nand_deinit();
    wodposit_nand_deinit();
}
