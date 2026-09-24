#include "rtos_ota_init.c"
#include "app_ota_init.c"

#include <driver/watchdog.h>
#include <driver/ota_ab_upgrade.h>
#include <include_bin.h>

INCBIN(rtos_ota_pkt, "example/driver/ota_ab_upgrade/ota.bin"); //rtos要用于升级的ota升级包
INCBIN(app_ota_pkt, "example/driver/ota_ab_upgrade/ota.bin"); //app要用于升级的ota升级包

int ota_ab_update_demo(void)
{
    /*--------------- 注册ota升级对象进行固件启动管理 ----------------*/
    int ret = rtos_ota_init();
    if (ret)
        return -1;

    ret = app_ota_init();
    if (ret)
        return -1;

    /*--------------- 注册ota升级对象启动固件结束 -----------------*/


    /* --------------- RTOS固件 OTA AB分区升级 --------------- */
    const char *ota_obj_name = NULL;
    if (rtos_ota_pktSize) {
        // 1.调用开始接口，开始rtos的ota固件升级
        ota_obj_name = RTOS_OTA_OBJ_NAME;
        ret = ota_ab_obj_upgrade_start(ota_obj_name);
        if (ret) {
            printf("RTOS OTA start failed\n");
            goto upgrade_err;
        }

        printf("RTOS OTA Upgrade Start!\n");

        // 2.调用写接口，持续写入新的固件数据到目标分区中
        ret = ota_ab_obj_upgrade_write(ota_obj_name, rtos_ota_pktData, rtos_ota_pktSize);
        if (ret) {
            printf("RTOS OTA Upgrade Write Target Partiton falied\n");
            goto upgrade_err;
        }

        // 3.调用停止接口，结束rtos的ota固件升级
        int written = ota_ab_obj_upgrade_stop(ota_obj_name);
        if (written != rtos_ota_pktSize) {
            printf("RTOS OTA upgrade written size < OTA Firmware size\n");
            goto done;
        }

        printf("RTOS OTA Upgrade End!\n");
    }
    /* ---------------------- OTA升级结束 --------------------- */


    /* --------------- APP固件 OTA AB分区升级 --------------- */
    if (app_ota_pktSize) {
        // 1.调用开始接口，开始app的ota固件升级
        ota_obj_name = APP_OTA_OBJ_NAME;
        ret = ota_ab_obj_upgrade_start(ota_obj_name);
        if (ret) {
            printf("APP OTA start failed\n");
            goto upgrade_err;
        }

        printf("APP OTA Upgrade Start!\n");

        // 2.调用写接口，持续写入新的固件数据到目标分区中
        ret = ota_ab_obj_upgrade_write(ota_obj_name, app_ota_pktData, app_ota_pktSize);
        if (ret) {
            printf("APP OTA Upgrade Write Target Partiton falied\n");
            goto upgrade_err;
        }

        // 3.调用停止接口，结束app的ota固件升级
        int written = ota_ab_obj_upgrade_stop(ota_obj_name);
        if (written != app_ota_pktSize) {
            printf("APP OTA upgrade written size < OTA Firmware size\n");
            goto done;
        }

        printf("APP OTA Upgrade End!\n");
    }
    /* ---------------------- OTA升级结束 --------------------- */


    // 重启前切换ota升级对象下次的固件启动分区
    if (app_ota_pktSize) {
        ret = ota_ab_obj_change_start_to_other(APP_OTA_OBJ_NAME);
        if (ret) {
            printf("change APP Firmware start to other err\n");
            goto done;
        }
    }

    // 最后切换rtos下次的固件启动分区
    if (rtos_ota_pktSize) {
        ret = ota_ab_obj_change_start_to_other(RTOS_OTA_OBJ_NAME);
        if (ret) {
            printf("change RTOS Firmware start to other err\n");
            goto done;
        }
    }

    //有进行OTA固件升级就重启，启动新固件
    if (app_ota_pktSize || rtos_ota_pktSize)
        reset();

    return 0;

upgrade_err:
    ota_ab_obj_upgrade_stop(ota_obj_name);
done:
    return -1;
}
