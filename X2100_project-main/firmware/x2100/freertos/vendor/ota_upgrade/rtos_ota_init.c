/**********************************************************************************
 * @file    rtos_ota_init.c
 * @brief   RTOS 固件启动管理 + OTA 对象注册
 *
 * 功能说明：
 * 1. 实现 ota_ab_upgrade 框架的 fw_start_manage 回调：
 *    - 直接操作 boot_flag 分区（简单字符串标志）
 *    - 使用 scratch_pad 检测 OTA 重启失败，回滚到上一分区
 *    - 读取并校验 RTOS 固件头部有效性
 * 2. 实现 change_start_to_other 回调：
 *    - 在 boot_flag 分区写入目标分区标志字符串
 * 3. 提供 rtos_ota_init() 注册 RTOS 为 OTA 升级对象
 *
 * 参考：example/driver/ota_ab_upgrade/rtos_ota_init.c
 **********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <driver/sfc_nor.h>
#include <driver/watchdog.h>
#include <driver/ota_ab_upgrade.h>

/* ======================== 宏定义 ======================== */

#define RTOS_OTA_OBJ_NAME   "rtos"

/* A/B 分区名，与烧录配置保持一致 */
#define PART_RTOS_A         "rtos_0"
#define PART_RTOS_B         "rtos_1"

/* 存放启动标志的分区名 */
#define BOOT_FLAG_PART      "boot_flag"

/* 启动标志：当 boot_flag 分区内容为此字符串时，表示启动 rtos_1 */
#define BOOT_FLAG_SLOT_B    "rtos_1"

/* 启动标志在 NOR Flash 中的偏移地址（相对于分区起始） */
#define BOOT_FLAG_OFFSET    0

/* OTA 重启失败标志（写入 scratch_pad 的值, ASCII "OTA"） */
#define OTA_REBOOT_FAIL_FLAG    0x0041544FUL

// rtos固件头部标志 "RTOS"，需与start.S保持一致
#define OTA_RTOS_MAGIC ('R' | ('T' << 8) | ('O' << 16) | ('S' << 24))

/*
 * SPL 大小：烧录文件为 rtos-with-spl.bin = SPL + zero.bin
 * rtos_header 位于 zero.bin 开头，在分区中的偏移为 SPL_SIZE
 */
// #define RTOS_HEADER_PART_OFFSET 0x6000
#define RTOS_HEADER_PART_OFFSET 0x0000 // 只使用zero.bin

/* ======================== RTOS 固件头部结构 ======================== */

struct rtos_header
{
    unsigned int code[2];
    unsigned int tag;        /* 固定为 OTA_RTOS_MAGIC */
    unsigned int version;
    unsigned long img_start;
    unsigned long img_end;
    unsigned long heap_start;
    unsigned long heap_end;
    unsigned long mapped_rtosdata_size;
};

/* ======================== 启动标志管理（直接操作 NOR Flash） ======================== */

/**
 * @brief 读取 boot_flag 分区中的启动标志
 *
 * @param[out] flag  输出缓冲区，至少 BOOT_FLAG_LEN 字节
 * @param[in]  len   缓冲区长度
 * @return 0 成功，-1 失败
 */
static int boot_flag_read(uint8_t *flag, uint32_t len)
{
    uint32_t offset = 0, size = 0;

    if (get_nor_partition_information_by_name(BOOT_FLAG_PART, &offset, &size) != 0)
    {
        printf("rtos_ota: partition '%s' not found\n", BOOT_FLAG_PART);
        return -1;
    }

    if (sfc_nor_flash_read(offset + BOOT_FLAG_OFFSET, len, flag) < 0)
    {
        printf("rtos_ota: read boot flag failed\n");
        return -1;
    }

    return 0;
}

/**
 * @brief 写入启动标志到 boot_flag 分区
 *
 * @param flag  标志字符串（如 "rtos_1"）
 * @param len   写入长度
 * @return 0 成功，-1 失败
 */
static int boot_flag_write(const uint8_t *flag, uint32_t len)
{
    uint32_t offset = 0, size = 0;

    if (get_nor_partition_information_by_name(BOOT_FLAG_PART, &offset, &size) != 0)
    {
        printf("rtos_ota: partition '%s' not found\n", BOOT_FLAG_PART);
        return -1;
    }

    /* 擦除标志区域（NOR Flash 必须先擦除再写） */
    if (sfc_nor_flash_erase(offset + BOOT_FLAG_OFFSET, len) != 0)
    {
        printf("rtos_ota: erase boot flag failed\n");
        return -1;
    }

    /* 写入标志 */
    if (sfc_nor_flash_write(offset + BOOT_FLAG_OFFSET, len, flag) < 0)
    {
        printf("rtos_ota: write boot flag failed\n");
        return -1;
    }

    return 0;
}

/* ======================== 回调实现 ======================== */

/**
 * @brief 固件启动管理回调
 *
 * 通过 boot_flag 分区中的简单字符串标志确定当前活动分区。
 * 使用 scratch_pad 检测 OTA 升级重启失败，失败时自动回滚。
 *
 * @param ab_partname[2]  A/B 分区名数组 ["rtos_0", "rtos_1"]
 * @param info            输出：启动固件信息
 * @return 0 成功，-1 失败
 */
static int rtos_boot_start_manage(const char *ab_partname[2],
                                  struct ota_ab_start_info *info)
{
    int ret;
    uint32_t offset = 0, size = 0;

    /*------------------------------------------------------*
     * 1. 检测 OTA 重启失败
     *    如果 scratch_pad 值为 "OTA"，说明上次 OTA 升级后
     *    新固件未能正常启动，需要回滚到上一分区。
     *------------------------------------------------------*/
    wdt_start(1000);
    wdt_stop();

    unsigned int reboot_flag = scratch_pad_read();
    scratch_pad_wirte(0);

    // printf("rtos_ota: reboot_flag: 0x%x\n", reboot_flag);

    /*------------------------------------------------------*
     * 2. 读取 boot_flag 分区中的启动标志
     *------------------------------------------------------*/
    uint8_t flag[8] = {0};
    ret = boot_flag_read(flag, sizeof(flag));
    if (ret != 0)
    {
        printf("rtos_ota: cannot read boot flag, default to %s\n",
               ab_partname[0]);
        flag[0] = '\0';
    }

    // printf("rtos_ota: boot flag: %s, 0x%02x %02x %02x %02x %02x %02x %02x %02x\n", flag, flag[0], flag[1], flag[2], flag[3], flag[4], flag[5], flag[6], flag[7]);

    /* 判断当前启动标志是否指向 rtos_1 */
    bool boot_from_b = (strcmp((const char *)flag, BOOT_FLAG_SLOT_B) == 0);

    /*------------------------------------------------------*
     * 2.1 首次启动初始化：如果 boot_flag 内容无效（全 0xFF 或
     *     未知内容），显式初始化为默认状态（擦除 = 使用 rtos_0）
     *------------------------------------------------------*/
    if (ret != 0 || (flag[0] != '\0' && !boot_from_b))
    {
        printf("rtos_ota: initializing boot flag (default to %s)\n",
               ab_partname[0]);

        uint32_t flag_offset = 0, flag_size = 0;
        if (get_nor_partition_information_by_name(BOOT_FLAG_PART,
                                                  &flag_offset, &flag_size) == 0)
        {
            /* 擦除 boot_flag 前 8 字节，显式设置为 rtos_0 状态 */
            sfc_nor_flash_erase(flag_offset + BOOT_FLAG_OFFSET, 8);
        }

        boot_from_b = false;
        flag[0] = '\0';
    }

    /*------------------------------------------------------*
     * 3. 如果检测到 OTA 重启失败，切换启动标志
     *------------------------------------------------------*/
    if (reboot_flag == OTA_REBOOT_FAIL_FLAG)
    {
        printf("rtos_ota: OTA upgrade failed, reverting to previous partition\n");

        /* 翻转启动标志 */
        boot_from_b = !boot_from_b;

        /* 擦除并重写启动标志（必须包含 \0 终止符） */
        const char *new_flag = boot_from_b ? BOOT_FLAG_SLOT_B : "";
        if (boot_flag_write((const uint8_t *)new_flag,
                            strlen(new_flag) + 1) != 0)
        {
            printf("rtos_ota: failed to write boot flag for rollback\n");
            return -1;
        }
        printf("rtos_ota: rollback to %s\n",
               boot_from_b ? ab_partname[1] : ab_partname[0]);
    }

    /*------------------------------------------------------*
     * 4. 确定当前活动分区
     *------------------------------------------------------*/
    const char *active_name = boot_from_b ? ab_partname[1] : ab_partname[0];

    /*------------------------------------------------------*
     * 5. 获取分区信息
     *------------------------------------------------------*/
    ret = get_nor_partition_information_by_name((char *)active_name,
                                                &offset, &size);
    if (ret != 0)
    {
        printf("rtos_ota: partition '%s' not found\n", active_name);
        return -1;
    }

    /*------------------------------------------------------*
     * 6. 读取并校验固件头部
     *    注意：烧录文件为 rtos-with-spl.bin = SPL(0x6000) + zero.bin，
     *    rtos_header 在分区中的实际偏移为 offset + RTOS_HEADER_PART_OFFSET
     *------------------------------------------------------*/
    struct rtos_header hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (sfc_nor_flash_read(offset + RTOS_HEADER_PART_OFFSET, sizeof(hdr), (uint8_t *)&hdr) < 0)
    {
        printf("rtos_ota: read header from %s failed\n", active_name);
        return -1;
    }

    if (hdr.tag != OTA_RTOS_MAGIC)
    {
        printf("rtos_ota: bad magic 0x%08x in %s (expected 0x%08lx)\n",
               (unsigned int)hdr.tag, active_name, (unsigned long)OTA_RTOS_MAGIC);
        
        /*
         * 如果当前活动分区魔数校验失败，说明这是全新烧录的空分区。
         * 强制使用默认第一个分区 rtos_0，不返回错误。
         * 系统当前已经在运行了，uboot 会正确跳转到入口地址，
         * OTA 框架只需要知道下次该升级哪个分区。
         */
        if (strcmp(active_name, PART_RTOS_A) != 0)
        {
            printf("rtos_ota: fallback to default partition '%s'\n", PART_RTOS_A);
            active_name = PART_RTOS_A;
            
            /* 重新读取 rtos_0 的头部（注意偏移） */
            ret = get_nor_partition_information_by_name((char *)active_name,
                                                        &offset, &size);
            if (ret != 0)
            {
                printf("rtos_ota: default partition '%s' not found\n", active_name);
                return -1;
            }
            
            memset(&hdr, 0, sizeof(hdr));
            if (sfc_nor_flash_read(offset + RTOS_HEADER_PART_OFFSET, sizeof(hdr), (uint8_t *)&hdr) < 0)
            {
                printf("rtos_ota: read header from %s failed\n", active_name);
                return -1;
            }
            
            if (hdr.tag != OTA_RTOS_MAGIC)
            {
                printf("rtos_ota: default partition also bad magic, using default version 0\n");
                hdr.version = 0;
            }
        }
        else
        {
            printf("rtos_ota: default partition bad magic, using default version 0\n");
            hdr.version = 0;
        }
    }

    /*------------------------------------------------------*
     * 7. 填充启动信息
     *------------------------------------------------------*/
    info->version = hdr.version;
    strncpy(info->used_partname, active_name, OTA_AB_NAME_LEN - 1);
    info->used_partname[OTA_AB_NAME_LEN - 1] = '\0';
    info->userdata = NULL;

    // printf("rtos_ota: boot from %s, version 0x%x\n", active_name, hdr.version);
    return 0;
}

/**
 * @brief 切换启动分区回调（OTA 升级完成后调用）
 *
 * 在 boot_flag 分区写入目标分区标志，下次启动时
 * fw_start_manage 回调将根据此标志选择启动分区。
 *
 * @param info  OTA 对象信息（包含目标分区名）
 * @return 0 成功，-1 失败
 */
static int ota_change_start_to_other(const struct ota_ab_obj_info *info)
{
    printf("rtos_ota: switching boot slot to %s...\n", info->target_partname);

    /* 如果目标分区是 rtos_1，写入 "rtos_1" 标志 */
    if (strcmp(info->target_partname, PART_RTOS_B) == 0)
    {
        /* 注意：必须包含 \0 终止符，否则 strcmp 比较会失败 */
        if (boot_flag_write((const uint8_t *)BOOT_FLAG_SLOT_B,
                            strlen(BOOT_FLAG_SLOT_B) + 1) != 0)
        {
            printf("rtos_ota: failed to write boot flag for %s\n",
                   PART_RTOS_B);
            return -1;
        }
        printf("rtos_ota: will boot from %s on next restart\n", PART_RTOS_B);
    }
    else
    {
        /* 目标分区是 rtos_0，擦除标志（空标志表示 rtos_0） */
        uint32_t part_offset = 0, part_size = 0;
        if (get_nor_partition_information_by_name(BOOT_FLAG_PART,
                                                  &part_offset, &part_size) != 0)
        {
            printf("rtos_ota: partition '%s' not found\n", BOOT_FLAG_PART);
            return -1;
        }

        if (sfc_nor_flash_erase(part_offset + BOOT_FLAG_OFFSET,
                                strlen(BOOT_FLAG_SLOT_B) + 1) != 0)
        {
            printf("rtos_ota: erase boot flag failed\n");
            return -1;
        }
        printf("rtos_ota: will boot from %s on next restart (flag cleared)\n",
               PART_RTOS_A);
    }

    return 0;
}

/* ======================== 注册接口 ======================== */

/**
 * @brief 注册 RTOS 为 OTA 升级对象
 *
 * 在系统启动时调用，注册后 ota_ab_upgrade 框架会自动管理 RTOS 的
 * A/B 分区启动和升级目标确定。
 *
 * @return 0 成功，-1 失败
 */
int rtos_ota_init(void)
{
    const char *ab_partname[2] = {PART_RTOS_A, PART_RTOS_B};

    struct ota_ab_obj_ops ops = {
        .fw_start_manage = rtos_boot_start_manage,
        .change_start_to_other = ota_change_start_to_other,
    };

    /* 注册 RTOS 为 OTA 升级对象 */
    int ret = ota_ab_obj_register(RTOS_OTA_OBJ_NAME, ab_partname, &ops);
    if (ret)
    {
        printf("rtos_ota: ota_ab_obj_register failed\n");
        return -1;
    }

    /* 获取注册后的信息并打印 */
    struct ota_ab_obj_info ota_info;
    ret = ota_ab_obj_get_info(RTOS_OTA_OBJ_NAME, &ota_info);
    if (ret)
    {
        printf("rtos_ota: ota_ab_obj_get_info failed\n");
        return -1;
    }

    struct ota_ab_start_info *start_info = &ota_info.start_info;
    if (start_info->used_partname[0] != '\0')
    {
        printf("rtos_ota: RTOS registered, version 0x%x, partition %s\n",
               start_info->version, start_info->used_partname);
        // printf("rtos_ota: target for upgrade: %s\n", ota_info.target_partname);
    }

    return 0;
}
