/*
 * Copyright (C) 2019, Ingenic Semiconductor
 *
 * Author : YangHuanHuan huanhuan.yang@ingenic.com
 *
 */


#ifndef _RAMDISK_PARTITION_H_
#define _RAMDISK_PARTITION_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_DFS_ELMFAT
#include <ramdisk_device_driver.h>
#include "ramdisk_device.h"
/*
 * functions
 */

int ramdisk_device_partitions_init(struct ramdisk_block *rdev);
int ramdisk_device_partitions_deinit(struct ramdisk_block *rdev);

#endif


#ifdef __cplusplus
}
#endif

#endif /* _RAMDISK_PARTITION_H_ */
