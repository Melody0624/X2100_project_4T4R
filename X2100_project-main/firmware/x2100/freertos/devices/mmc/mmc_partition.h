/*
 * Copyright (C) 2019, Ingenic Semiconductor
 *
 * Author : YangHuanHuan huanhuan.yang@ingenic.com
 *
 */


#ifndef _MMC_PARTITION_H_
#define _MMC_PARTITION_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DFS_ELMFAT
#include <block_device_driver.h>

/*
 * functions
 */

int emmc_device_partition_init(struct mmc_card *card);
int emmc_device_partition_deinit(struct mmc_card *card);
#endif


#ifdef __cplusplus
}
#endif

#endif /* _MMC_PARTITION_H_ */
