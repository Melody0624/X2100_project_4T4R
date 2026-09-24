/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __RAMDISK_DEVICES_H__
#define __RAMDISK_DEVICES_H__

#include <stdint.h>
#include <driver/ramdisk.h>

#ifndef CONFIG_RAMDISK_DEVICE_SIZE
#define RAMDISK_DEVICE_SIZE             (1*1024*1024)
#else
#define RAMDISK_DEVICE_SIZE             CONFIG_RAMDISK_DEVICE_SIZE
#endif


struct ramdisk_block *ramdisk_devices_info(void);

#endif /* __RAMDISK_DEVICES_H__ */
