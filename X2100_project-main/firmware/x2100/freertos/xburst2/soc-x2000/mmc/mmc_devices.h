/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#ifndef __MMC_DEVICES_H__
#define __MMC_DEVICES_H__

#include <stdint.h>
#include <mmc-mmc.h>
#include <driver/hrtimer.h>

enum {
    MMC_DEVICE_NONREMOVABLE = 0,
    MMC_DEVICE_REMOVABLE = 1,
};

struct mmc_device_gpio {
    int reset;
    int reset_level;

    int power;
    int power_level;

    int detect;
    int detect_level;
    int detect_change;

    int removal;  /* =1: REMOVABLE 可移除,动态拔插,  =0: NONREMOVABLE 固定build-in,不可动态拔插 */

    int is_inited;
    struct mutex mlock;
    thread_cond_t cond;
    struct hrtimer cd_timer; /* 消抖 */
};


struct mmc_devices_config {
    char *name;
    int index;
    uint32_t ocr_avail;
    uint32_t capacity;
    uint32_t capacity2;
    uint32_t max_freq;
    struct mmc_device_gpio *gpio;

    int (*device_init)(void);
    void (*device_power_on)(void);
    void (*device_power_off)(void);
    int (*device_card_change)(void);
};

int mmc_devices_init(void);
int mmc_devices_deinit(void);
struct mmc_card *mmc_devices_info(void);
struct mmc_card *mmc_register_device(struct mmc_devices_config *device_config);
int mmc_unregister_device(struct mmc_devices_config *device_config);

#endif /* __MMC_DEVICES_H__ */
