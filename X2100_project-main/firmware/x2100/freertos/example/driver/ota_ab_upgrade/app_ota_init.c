#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <driver/sfc_nor.h>
#include <mtd_driver_nor.h>
#include <driver/ota_ab_upgrade.h>

// RTOS注册OTA升级对象名
#define APP_OTA_OBJ_NAME "app"

//APP段设置的AB分区名，若要修改需注意与实际分区名一起修改
#define PART_APP0_NAME   "app0"
#define PART_APP1_NAME   "app1"

//自定义app固件头部标志 "APP"
#define OTA_APP_MAGIC  ('A' | ('P' << 8) | ('P' << 16) )

//自定义app固件头部信息结构体示例，至少需要包含这三个成员
struct app_header {
    unsigned int tag; //app标志
    unsigned int version; //app当前版本号
    unsigned int image_size; //app固件大小
};

//app启动管理示例，app启动部分的具体实现有客户自行实行
int app_start_manage(const char *ab_partname[2], struct ota_ab_start_info *info)
{
    int ret;
    struct app_header hdr[2];
    const struct mtd_nor_partition *part[2];

    //获取app0分区的app固件头部
    part[0] = mtd_nor_partition_get_by_name(ab_partname[0]);
    if (part[0] == NULL) {
        printf("get app0 partition failed\n");
        return -1;
    }
    ret = sfc_nor_flash_read(part[0]->offset, sizeof(hdr[0]), (uint8_t *)&hdr[0]);
    if (ret != (int)sizeof(hdr[0])) {
        printf("read app0 header failed\n");
        return -1;
    }

    //获取app1分区的app固件头部
    part[1] = mtd_nor_partition_get_by_name(ab_partname[1]);
    if (part[1] == NULL) {
        printf("get app1 partition failed\n");
        return -1;
    }
    ret = sfc_nor_flash_read(part[1]->offset, sizeof(hdr[1]), (uint8_t *)&hdr[1]);
    if (ret != (int)sizeof(hdr[1])) {
        printf("read app1 header failed\n");
        return -1;
    }

    //获取要使用最新版本的app分区
    int i, used_part = -1;
    for(i = 0; i < 2; i++) {
        if (hdr[i].tag != OTA_APP_MAGIC)
            continue;
        if (used_part == -1) { //第一个有效分区
            used_part = i;
            continue;
        }
        //比较版本号，使用最新版本的分区
        if (hdr[i].version > hdr[used_part].version)
            used_part = i;
    }

    if (used_part == -1) {
        printf("No app partition available, APP no install\n");
        goto no_start;
    }

    //启动最新可用分区中的app固件
    int app_ret = 0;
    /* app_ret = app_start(used_part); //用户实现 */

    if(app_ret) { //若启动失败则启动另一可用分区的app固件
        printf("app start err!\n");

        //擦除启动失败分区，当前示例只要是app启动失败则擦除启动失败的分区
        ret = sfc_nor_flash_erase(part[used_part]->offset, part[used_part]->size);
        if (ret != 0)
            printf("erase app part failed: %d\n", ret);

        //切换另一个使用分区
        used_part = !used_part;
        if (hdr[used_part].tag != OTA_APP_MAGIC) { //校验另一个分区的app固件有效性
            printf("No app partition available\n");
            goto no_start;//无另一可用的分区，退出
        }

        /* app_ret = app_start(used_part);  启动另一分区 */

        if (app_ret) { //再次启动失败，擦除启动失败分区退出
            ret = sfc_nor_flash_erase(part[used_part]->offset, part[used_part]->size);
            if (ret != 0)
                printf("erase app part failed: %d\n", ret);
            goto no_start;
        }
        printf("another app start\n");
    }

    //同步ota对象信息
    info->userdata = NULL;
    info->version = hdr[used_part].version;
    strncpy(info->used_partname, part[used_part]->name, OTA_AB_NAME_LEN);
    info->used_partname[OTA_AB_NAME_LEN -1] = '\0';

    return 0;

no_start: // 没有可用的启动分区固件
    info->used_partname[0] = '\0';
    info->userdata = NULL;
    info->version = -1;

    return 0;
}

// app固件启动处理并注册ota升级对象
int app_ota_init(void)
{
    const char *ota_ab_partname[2] = {PART_APP0_NAME, PART_APP1_NAME};
    struct ota_ab_obj_ops ops = {
        .fw_start_manage = app_start_manage,
        .change_start_to_other = NULL,
    };

    // 注册app为OTA升级对象
    int ret = ota_ab_obj_register(APP_OTA_OBJ_NAME, ota_ab_partname, &ops);
    if (ret)
        return -1;

    struct ota_ab_obj_info ota_info;
    ret = ota_ab_obj_get_info(APP_OTA_OBJ_NAME, &ota_info);
    if (ret)
        return -1;

    struct ota_ab_start_info *start_info = &ota_info.start_info;
    if (start_info->used_partname[0] != '\0')
        printf("Now app Version: 0x%x, Used part: %s\n", start_info->version, start_info->used_partname);

    return 0;
}