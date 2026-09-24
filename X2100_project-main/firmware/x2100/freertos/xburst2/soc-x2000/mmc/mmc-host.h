/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_HOST_H__
#define __MMC_HOST_H__

#include <stdint.h>
#include <mmc-core.h>

static inline int mmc_card_hs(struct mmc_host *mmc)
{
    return (mmc->timing == MMC_TIMING_SD_HS ||
            mmc->timing == MMC_TIMING_MMC_HS);
}

static inline int mmc_card_uhs(struct mmc_host *mmc)
{
    return (mmc->timing >= MMC_TIMING_UHS_SDR12 &&
            mmc->timing <= MMC_TIMING_UHS_DDR50);
}

static inline int mmc_card_hs200(struct mmc_host *mmc)
{
    return (mmc->timing == MMC_TIMING_MMC_HS200);
}

static inline int mmc_card_ddr52(struct mmc_host *mmc)
{
    return (mmc->timing == MMC_TIMING_MMC_DDR52);
}

static inline int mmc_card_hs400(struct mmc_host *mmc)
{
    return (mmc->timing == MMC_TIMING_MMC_HS400);
}

static inline int mmc_host_uhs(struct mmc_host *mmc)
{
    return mmc->capacity &
        (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
        MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 |
        MMC_CAP_UHS_DDR50) &&
        mmc->capacity & MMC_CAP_4_BIT_DATA;
}


#endif /* __MMC_HOST_H__ */
