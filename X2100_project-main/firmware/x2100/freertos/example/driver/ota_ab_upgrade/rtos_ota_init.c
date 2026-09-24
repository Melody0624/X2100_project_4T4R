#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <driver/sfc_nor.h>
#include <driver/watchdog.h>
#include <mtd_driver_nor.h>
#include <driver/ota_ab_upgrade.h>

// RTOS注册OTA升级对象名
#define RTOS_OTA_OBJ_NAME "rtos"

// RTOS设置的AB分区名，需与uboot设置的一致
#define PART_RTOS_NAME      "rtos"
#define PART_RTOS_OTA_NAME  "rtos_ota"

// 存放RTOS启动标志的分区，需与uboot设置的一致
// 标志若为ota:rtos_ota启动rtos_ota分区的固件否则启动rtos分区固件
#define PART_OTA_NAME       "ota"
#define PART_OTA_INFO       "ota:rtos_ota"

// rtos固件头部标志 "RTOS"，需与start.S保持一致
#define OTA_RTOS_MAGIC  ('R' | ('T' << 8) | ('O' << 16) | ('S' << 24))

// rtos固件头部信息结构体，需与start.S保持一致
struct rtos_header {
    unsigned int code[2];
    unsigned int tag;
    unsigned int version;
    unsigned long img_start;
    unsigned long img_end;
    unsigned long heap_start;
    unsigned long heap_end;
    unsigned long mapped_rtosdata_size;
};

//rtos启动管理
static int rtos_boot_start_manage(const char *ab_partname[2], struct ota_ab_start_info *info)
{
    //停止看门狗，擦除ota重启标志
    wdt_start(1000); //使能看门狗
    wdt_stop(); //停止看门狗

    unsigned int reboot_flag = scratch_pad_read(); //记录本次ota重启标志
    scratch_pad_wirte(0); //擦除ota重启标志

    //获取ota分区偏移及大小
    const struct mtd_nor_partition *part;
    part = mtd_nor_partition_get_by_name(PART_OTA_NAME);
    if (part == NULL) {
        printf("get ota partition info failed\n");
        return -1;
    }

    //读取ota标志
    uint8_t rtos_flag[strlen(PART_OTA_INFO)];
    int ret = sfc_nor_flash_read(part->offset, strlen(PART_OTA_INFO), rtos_flag);
    if (ret != (int)strlen(PART_OTA_INFO)) {
        printf("read ota flag failed\n");
        return -1;
    }
    int ota_flag = (memcmp(rtos_flag, PART_OTA_INFO, strlen(PART_OTA_INFO)) == 0);

    //若ota重启标志为失败则切换ota标志
    if (reboot_flag == 0x0041544F) {//ota重启失败标志
        printf("OTA upgrade failed and reverted to the previous partition\n");

        //切换ota标志
        ota_flag = !ota_flag;

        //擦除ota标志
        ret = sfc_nor_flash_erase(part->offset, strlen(PART_OTA_INFO));
        if (ret != 0) {
            printf("erase ota flag failed: %d\n", ret);
            return -1;
        }

        //若切换后ota标志是rtos_ota分区，则更新ota标志为 ota:rtos_ota
        if (ota_flag) {
            ret = sfc_nor_flash_write(part->offset, strlen(PART_OTA_INFO), (const uint8_t *)PART_OTA_INFO);
            if (ret != (int)strlen(PART_OTA_INFO)){
                printf("write ota flag failed: %d\n", ret);
                return -1;
            }
        }
    }

    // 获取使用分区信息
    const char *used_partname = ota_flag ? ab_partname[1] : ab_partname[0];
    part = mtd_nor_partition_get_by_name(used_partname);
    if (part == NULL) {
        printf("get rtos used partition info failed\n");
        return -1;
    }

    struct rtos_header used_hdr;
    ret = sfc_nor_flash_read(part->offset, sizeof(used_hdr), (uint8_t *)&used_hdr);
    if (ret != (int)sizeof(used_hdr)) {
        printf("read %s rtos header failed\n", part->name);
        return -1;
    }

    if (used_hdr.tag != OTA_RTOS_MAGIC) {
        printf("rtos used firmware header tag err\n");
        return -1;
    }

    //同步ota对象信息
    info->userdata = NULL;
    info->version = used_hdr.version;
    strncpy(info->used_partname, used_partname, OTA_AB_NAME_LEN);
    info->used_partname[OTA_AB_NAME_LEN - 1] = '\0';

    return 0;
}

static int ota_change_start_to_other(const struct ota_ab_obj_info *info)
{
    //获取ota分区偏移及大小
    const struct mtd_nor_partition *part;
    part = mtd_nor_partition_get_by_name(PART_OTA_NAME);
    if (part == NULL) {
        printf("get ota partition info failed\n");
        return -1;
    }

    //擦除ota标志
    int ret = sfc_nor_flash_erase(part->offset, strlen(PART_OTA_INFO));
    if (ret != 0) {
        printf("erase ota flag failed: %d\n", ret);
        return -1;
    }

    //若目标分区为rtos_ota,则更新ota标志为"ota:rtos_ota"
    if (!strcmp(info->target_partname, PART_RTOS_OTA_NAME)) {
        ret = sfc_nor_flash_write(part->offset, strlen(PART_OTA_INFO), (const uint8_t *)PART_OTA_INFO);
        if (ret != (int)strlen(PART_OTA_INFO)) {
            printf("write ota flag failed: %d\n", ret);
            return -1;
        }
    }

    return 0;
}

// rtos固件启动处理并注册ota升级对象
int rtos_ota_init(void)
{
    const char *ota_ab_partname[2] = {PART_RTOS_NAME, PART_RTOS_OTA_NAME};
    struct ota_ab_obj_ops ops = {
        .fw_start_manage = rtos_boot_start_manage,
        .change_start_to_other = ota_change_start_to_other,
    };

    // 注册rtos为OTA升级对象
    int ret = ota_ab_obj_register(RTOS_OTA_OBJ_NAME, ota_ab_partname, &ops);
    if (ret)
        return -1;

    struct ota_ab_obj_info info;
    ret = ota_ab_obj_get_info(RTOS_OTA_OBJ_NAME, &info);
    if (ret)
        return -1;

    struct ota_ab_start_info *start_info = &info.start_info;
    printf("Now RTOS Version: 0x%x, Used part: %s\n", start_info->version, start_info->used_partname);

    return 0;
}