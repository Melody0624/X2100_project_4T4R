/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_BLOCK_H__
#define __MMC_BLOCK_H__


#include "mmc-mmc.h"

int mmc_blk_set_ops(struct mmc_card_ops *card_ops);

#endif /* __MMC_BLOCK_H__ */
