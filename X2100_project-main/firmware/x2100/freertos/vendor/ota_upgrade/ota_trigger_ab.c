/**********************************************************************************
 * @file    ota_trigger_ab.c
 * @brief   A/B 双区 OTA 升级触发模块（基于 ota_ab_upgrade 框架）
 *
 * 功能说明：
 * 1. 通过 USB CDC 虚拟串口或 UART3 物理串口接收上位机指令，由 CLI 命令分发
 * 2. 收到 "otaUpgrade" 命令后，在独立线程中启动 OTA 升级
 * 3. 完整适配原有 pkt 协议，包括数据包校验、ACK 握手、双线程收发
 * 4. 升级流程使用 ota_ab_upgrade 框架（start → write → stop）
 * 5. 升级目标为 A/B 双区中当前非活动（inactive）分区
 * 6. OTA 成功后切换 BootFlag 槽位，重启进入新固件
 *
 * 传输通道选择：
 *   - 调用方（USB CDC 接收任务 / UART3 接收任务）通过 transport 参数传入
 *   - ota_cmd_handler 根据传入的传输通道类型选择对应的接口实例
 *   - 无需解析命令参数字符串，由调用方直接指定
 *
 * 协议适配：
 *   - 完全遵循原 OTA 框架的 pkt 协议格式（包头 + 数据 + CRC 校验）
 *   - 支持所有包类型：pkt_start, pkt_ota_upgrade_partiton, pkt_ota_update_file_len,
 *                    pkt_ota_update_file, pkt_ota_upgrade_end, pkt_get_*, 等
 *   - 保持 ACK 应答机制，与上位机工具完全兼容
 *
 * 分区布局（由烧录工具的分区表定义）：
 *   - uboot           : 0x000000, 大小 0x6000  (~24  KB，SPL分区)
 *   - rtos_0 (Slot A) : 0x006000, 大小 0xEA000 (~936 KB)
 *   - rtos_1 (Slot B) : 0x0F0000, 大小 0xEA000 (~936 KB)
 *   - boot_flag       : 0x1DA000, 大小 0x1000 (4 KB, 存储 BootFlag)
 *   - config          : 0x1DB000, 大小 0x25000 (~148 KB)
 *
 * 升级触发方式：上位机通过 USB CDC 或 UART3 发送 "otaUpgrade" 命令
 **********************************************************************************/

#include <stdio.h>
#include <string.h>
#include <os.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/semphr.h>
#include <os/freertos/include/task.h>
#include <assert.h>
#include <errno.h>
#include <crc32.h>
#include <driver/ota.h>
#include <driver/ota_ab_upgrade.h>
#include <driver/watchdog.h>
#include <usb/gadget_serial.h>

#include "pkt.h"
#include "rtos_ota_init.h"
#include "ota_trigger_ab.h"
#include "gadget_serial.h"
#include "uart_trans.h"
#include "heap_malloc.h"

/* ======================== 宏定义 ======================== */

#define RTOS_OTA_OBJ_NAME     "rtos"
#define OTA_DEFAULT            0
#define OTA_WRITE              1
#define OTA_READ               2

/* 连续超时最大次数，超过则复位系统（防止无限挂起） */
#define OTA_AB_MAX_RETRY       20
#define OTA_AB_RETRY_MAX       3 // 数据包超时重试次数

/* ======================== 私有数据结构 ======================== */

typedef struct device_info {
    enum pkt_type pkt_type;
    struct ota_transfer *transfer;
} DeviceInfo;

typedef struct ota_ab_private_data {
    uint64_t img_len;                // 固件总长度
    char partition_name[128];        // 分区名称（来自上位机）

    struct transfer_cfg cfg;
    uint32_t file_verify;            // 整个文件的校验值（32位）
    DeviceInfo dev_info;

    int restart;                     // 收到 pkt_start 后置1
    int ota_cmd;                     // OTA_WRITE / OTA_READ
    int devstatus;                   // STATUS_OK / ERROR / TIMEOUT
    int ota_upgrade_end;             // 收到结束命令置1

    volatile int erase_done;         // 擦除完成标志：0=进行中，1=完成
    volatile int erase_error;        // 擦除错误标志：0=正常，1=错误

    uint8_t num_processed[16];       // 去重：记录已处理的包编号（0~15）
    int last_num;                    // 上一次包编号，用于检测循环

    thread_waiter_t wait;
    thread_waiter_t ota_cmd_wait;
    thread_waiter_t file_len_wait;
    thread_waiter_t DeviceInfo_wait;

    thread_ptr_t work_thread;
} ota_ab_private_data_t;

/* ======================== 传输层回调 (USB CDC) ======================== */

static int ota_usb_transfer_init(void)
{
    return 0;
}

static void ota_usb_transfer_exit(void)
{
    // USB already initialized by gadget_serial
}

static int ota_usb_transfer_read(uint8_t *buf, ssize_t size, uint32_t timeout_ms)
{
    int ret, left = size;
    uint8_t *ptr = buf;
    while (left > 0) {
        ret = gadget_serial_read(ptr, left, 1, timeout_ms);
        if (ret <= 0) return ret;
        ptr += ret;
        left -= ret;
    }
    return (size - left);
}

static int ota_usb_transfer_write(uint8_t *buf, ssize_t size)
{
    int ret, left = size;
    uint8_t *ptr = buf;
    while (left > 0) {
        ret = gadget_serial_write(ptr, left, 1, PKT_TIMEOUT_MS);
        if (ret <= 0) break;
        ptr += ret;
        left -= ret;
    }
    return (size - left);
}

/* ======================== UART3 传输层回调 ======================== */

static int ota_uart_transfer_init(void)
{
    /* UART3 已在 uart_open() 中初始化，无需额外操作 */
    return 0;
}

static void ota_uart_transfer_exit(void)
{
    /* UART3 由系统管理，无需额外清理 */
}

static int ota_uart_transfer_read(uint8_t *buf, ssize_t size, uint32_t timeout_ms)
{
    int ret, left = size;
    uint8_t *ptr = buf;
    while (left > 0) {
        ret = uart_raw_receive(ptr, left, timeout_ms);
        if (ret <= 0) return ret;
        ptr += ret;
        left -= ret;
    }
    return (size - left);
}

static int ota_uart_transfer_write(uint8_t *buf, ssize_t size)
{
    int ret, left = size;
    uint8_t *ptr = buf;
    while (left > 0) {
        ret = uart_raw_send((const char *)ptr, left, PKT_TIMEOUT_MS);
        if (ret <= 0) break;
        ptr += ret;
        left -= ret;
    }
    return (size - left);
}

/* ======================== 统一的传输接口 ======================== */

/**
 * @brief 传输通道接口
 *
 * 将传输层回调、名称、接收任务控制函数封装为统一接口，
 * ota_cmd_handler 根据参数选择对应的实例传入升级线程。
 */
typedef struct {
    const char *name;                  /**< 传输通道名称（用于日志打印） */
    struct transfer_cb *cb;            /**< OTA pkt 协议传输回调 */
    void (*suspend_recv)(void);        /**< 挂起接收任务 */
    void (*resume_recv)(void);         /**< 恢复接收任务 */
    void (*reset_read_wait)(void);     /**< 重置 read_wait 状态 */
    void (*reset_write_wait)(void);    /**< 重置 write_wait 状态 */
} ota_transport_iface_t;

/* USB CDC 传输回调 */
static struct transfer_cb ota_usb_transfer_cb = {
    .transfer_init = ota_usb_transfer_init,
    .transfer_exit = ota_usb_transfer_exit,
    .transfer_read = ota_usb_transfer_read,
    .transfer_write = ota_usb_transfer_write,
};

/* UART3 传输回调 */
static struct transfer_cb ota_uart_transfer_cb = {
    .transfer_init = ota_uart_transfer_init,
    .transfer_exit = ota_uart_transfer_exit,
    .transfer_read = ota_uart_transfer_read,
    .transfer_write = ota_uart_transfer_write,
};

/* USB CDC 传输接口实例 */
static const ota_transport_iface_t g_ota_transport_usb = {
    .name = "USB CDC",
    .cb = &ota_usb_transfer_cb,
    .suspend_recv = gadget_serial_suspend_recv_task,
    .resume_recv = gadget_serial_resume_recv_task,
    .reset_read_wait = gadget_serial_reset_read_wait_ab,
    .reset_write_wait = gadget_serial_reset_write_wait_ab,
};

/* UART3 传输接口实例 */
static const ota_transport_iface_t g_ota_transport_uart = {
    .name = "UART3",
    .cb = &ota_uart_transfer_cb,
    .suspend_recv = uart_suspend_recv_task,
    .resume_recv = uart_resume_recv_task,
    .reset_read_wait = uart_reset_read_wait_ab,
    .reset_write_wait = uart_reset_write_wait_ab,
};

/* ======================== 协议处理函数 ======================== */

static void cope_with_data_timeout(void)
{
    printf("ota: data timeout, resetting system...\n");
    sleep(2);
    reset();   // 系统复位
}

static void DeviceInfoRetriever(void *data)
{
    DeviceInfo *info = (DeviceInfo *)data;
    struct ota_transfer *transfer = info->transfer;
    struct ota_ab_private_data *pri_data = (struct ota_ab_private_data *)transfer->data;
    struct transfer_cfg cfg;

    /* 静态存储信息，首次获取后缓存 */
    static struct {
        uint32_t pagesize;
        uint64_t partsize;
        uint32_t blocks;
    } s_storage_info = {0};
    static int s_storage_info_valid = 0;

    while (!pri_data->ota_upgrade_end) {
        if (thread_waiter_wait_timeout(&pri_data->DeviceInfo_wait, PKT_TRANS_TIMEOUT_MS))
            continue;

        enum pkt_type type = info->pkt_type;
        cfg.pkt_type = type;

        switch (type) {
        case pkt_storage_info:
            if (!s_storage_info_valid) {
                /* 通过分区信息获取存储参数 */
                struct ota_ab_obj_info ota_info;
                if (ota_ab_obj_get_info(RTOS_OTA_OBJ_NAME, &ota_info) == 0) {
                    uint64_t offset, size;
                    if (ota_ab_get_part_info(ota_info.target_partname, &offset, &size) == 0) {
                        s_storage_info.partsize = size;
                        /* 默认 pagesize 为 4096，从 flash_info 获取更精确的值 */
                        const struct storage_info *sinfo = ota_ab_flash_info_get();
                        s_storage_info.pagesize = sinfo ? sinfo->pagesize : 4096;
                        s_storage_info.blocks = size / s_storage_info.pagesize;
                        s_storage_info_valid = 1;
                    }
                }
                /* 如果获取失败，使用默认值 */
                if (!s_storage_info_valid) {
                    s_storage_info.pagesize = 4096;
                    s_storage_info.partsize = 0xED000;
                    s_storage_info.blocks = 0xED000 / 4096;
                    s_storage_info_valid = 1;
                }
            }
            cfg.size = sizeof(s_storage_info);
            cfg.data = (unsigned char *)&s_storage_info;
            break;

        case pkt_erase_blocks_count: {
            /*
             * A/B 框架擦除是同步的，在 ota_ab_obj_upgrade_start 中一次性完成。
             * 根据 erase_done 标志返回进度：
             *   - erase_done == 0 → 擦除进行中，返回 0（上位机继续轮询）
             *   - erase_done == 1 → 擦除完成，返回总块数（上位机开始发送数据）
             *
             * 注意：erased 必须为 static，否则 cfg.data 指向栈变量会在
             * switch 结束后被覆盖（use-after-scope bug）
             */
            static int erased = 0;
            if (pri_data->erase_error) {
                erased = -1;   // 错误码
            } else if (pri_data->erase_done) {
                erased = (int)s_storage_info.blocks;
            } else {
                erased = 0;
            }
            cfg.data = (unsigned char *)&erased;
            cfg.size = sizeof(erased);
            break;
        }

        case pkt_device_status:
            cfg.size = sizeof(pri_data->devstatus);
            cfg.data = (unsigned char *)&pri_data->devstatus;
            break;

        case pkt_file_verify:
            cfg.size = sizeof(pri_data->file_verify);
            cfg.data = (unsigned char *)&pri_data->file_verify;
            break;

        case pkt_partition_context: {
            /* 返回当前 OTA 对象的分区信息 */
            struct ota_ab_obj_info ota_info;
            if (ota_ab_obj_get_info(RTOS_OTA_OBJ_NAME, &ota_info) == 0) {
                /* 字符串格式: "active_partname,target_partname" */
                static char part_info[OTA_AB_NAME_LEN * 2];
                memset(part_info, 0, sizeof(part_info));
                snprintf(part_info, sizeof(part_info), "%s,%s",
                         ota_info.start_info.used_partname,
                         ota_info.target_partname);
                cfg.size = strlen(part_info) + 1;
                cfg.data = (unsigned char *)part_info;
            } else {
                static char err_info[] = "error";
                cfg.size = sizeof(err_info);
                cfg.data = (unsigned char *)err_info;
            }
            break;
        }

        default:
            printf("ota: unsupported info type %d\n", type);
            return;
        }

        if (pkt_write_sync(transfer, &cfg)) {
            pri_data->devstatus = STATUS_TIMEOUT;
        }
    }
}

static void pkt_cmd_process(struct ota_transfer *transfer, struct transfer_cfg *cfg, int type)
{
    assert(transfer);
    struct ota_ab_private_data *data = (struct ota_ab_private_data *)transfer->data;
    DeviceInfo *info = &data->dev_info;
    info->transfer = transfer;

    // printf("pkt_cmd_process type %d\n", type);

    switch (type) {
    case pkt_start:
        data->img_len = 0;
        memset(data->partition_name, 0, sizeof(data->partition_name));
        data->restart = 1;
        data->ota_upgrade_end = 0;
        data->ota_cmd = OTA_DEFAULT;
        data->erase_done = 0;    /* 重置擦除标志 */
        data->erase_error = 0;   // 重置错误标志
        /* === 重置去重状态 === */
        memset(data->num_processed, 0, sizeof(data->num_processed));
        data->last_num = -1;

        if (data->devstatus == STATUS_TIMEOUT) {
            if (!semaphore_try_wait(&data->cfg.wait))
                semaphore_post(&transfer->r_sem);
        }
        break;

    case pkt_get_partition_info:
        info->pkt_type = pkt_storage_info;
        thread_waiter_wakeup(&data->DeviceInfo_wait);
        break;

    case pkt_get_partition_context:
        info->pkt_type = pkt_partition_context;
        thread_waiter_wakeup(&data->DeviceInfo_wait);
        break;

    case pkt_get_erase_blocks_count:
        info->pkt_type = pkt_erase_blocks_count;
        thread_waiter_wakeup(&data->DeviceInfo_wait);
        break;

    case pkt_get_device_status:
        info->pkt_type = pkt_device_status;
        thread_waiter_wakeup(&data->DeviceInfo_wait);
        break;

    case pkt_get_file_verify:
        info->pkt_type = pkt_file_verify;
        thread_waiter_wakeup(&data->DeviceInfo_wait);
        break;

    case pkt_ota_read_partiton:
        if (data->devstatus == STATUS_OK) {
            data->ota_cmd = OTA_READ;
            thread_waiter_wakeup(&data->ota_cmd_wait);
        }
        break;

    case pkt_ota_upgrade_partiton:
        if (data->devstatus == STATUS_OK) {
            data->ota_cmd = OTA_WRITE;
            thread_waiter_wakeup(&data->ota_cmd_wait);
        }
        break;

    case pkt_ota_upgrade_end:
        data->ota_upgrade_end = 1;
        thread_waiter_wakeup(&data->wait);
        break;

    case pkt_set_device_reset:
        usleep(100 * 1000);
        reset();
        break;

    default:
        printf("ota: unsupported cmd type %d\n", type);
    }
}

static void pkt_data_process(struct ota_transfer *transfer, struct transfer_cfg *cfg, int type)
{
    int cnt = PKT_WAIT_TIMES;
    assert(transfer && cfg->data);
    struct ota_ab_private_data *data = (struct ota_ab_private_data *)transfer->data;

    switch (type) {
    case pkt_update_partition_name:
        memcpy(data->partition_name, cfg->data, cfg->size);
        /* 验证分区名是否与框架目标一致 */
        {
            struct ota_ab_obj_info info;
            if (ota_ab_obj_get_info(RTOS_OTA_OBJ_NAME, &info) == 0) {
                if (strncmp(data->partition_name, info.target_partname,
                            strlen(info.target_partname)) != 0) {
                    printf("ota: partition name mismatch: got %s, expected %s\n",
                           data->partition_name, info.target_partname);
                    data->devstatus = STATUS_ERROR;
                }
            }
        }
        thread_waiter_wakeup(&data->wait);
        break;

    case pkt_ota_update_file_len:
        if (data->devstatus == STATUS_OK) {
            data->img_len = *(uint64_t *)cfg->data;   // 64位读取
            thread_waiter_wakeup(&data->file_len_wait);
            printf("ota: file len: %llu\n", data->img_len);
        }
        break;

    case pkt_ota_update_file:
        if (data->devstatus != STATUS_OK)
            break;

        /* 如果文件长度尚未设置，丢弃该数据包，仅回复 ACK（由上层发送） */
        if (data->img_len == 0) {
            printf("ota: data packet before file length, dropping\n");
            // 释放 r_sem，让读线程继续接收下一个包
            semaphore_post(&transfer->r_sem);
            break;
        }

        while (cnt--) {
            if (!semaphore_wait_timeout(&transfer->r_sem, PKT_TRANS_TIMEOUT_MS)) {
                pkt_copy_cfg_info(cfg, &data->cfg);
                semaphore_post(&data->cfg.wait);
                break;
            }
            if (data->devstatus != STATUS_OK)
                break;
        }
        if (!cnt)
            cope_with_data_timeout();
        break;

    default:
        printf("ota: unsupported data type %d\n", type);
    }
}

static int pkt_callback_my_r(struct ota_transfer *transfer, struct transfer_cfg *cfg, int type)
{
    if (transfer->data == NULL) {
        printf("pkt: data not ready, drop type %d\n", type);
        return 0;
    }

    struct ota_ab_private_data *data = (struct ota_ab_private_data *)transfer->data;

    if (type & PKT_SEND_TYPE_FLAG) {
        printf("ota: error package type %d\n", type);
        return 0;
    }

    if (type & PKT_RECEIVE_DATA_TYPE_FLAG) {
        if (data->devstatus == STATUS_OK) {
            pkt_data_process(transfer, cfg, type);
        } else {
            // 错误状态下丢弃数据包，但依然回复 ACK（由上层发送）
            static int drop_count = 0;
            drop_count++;
            if (drop_count <= 3 || (drop_count % 10) == 0) {
                printf("ota: dropping data packet type %d (devstatus=%d)\n",
                       type, data->devstatus);
            }
        }
    } else {
        pkt_cmd_process(transfer, cfg, type);
    }

    return 0;  // 保持读线程运行
}

/* ======================== OTA 写入子流程 ======================== */

static int ota_ab_update_partition(struct ota_transfer *ota_transfer)
{
    struct ota_ab_private_data *data = (struct ota_ab_private_data *)ota_transfer->data;
    struct transfer_cfg *cfg = &data->cfg;

    data->file_verify = 0;
    uint64_t len = 0;

    /* ---- 擦除目标分区 ---- */
    printf("ota: erasing target partition...\n");
    data->erase_done = 0;          // 擦除进行中
    data->erase_error = 0;

    int start_ok = 0;
    for (int retry = 0; retry < 2; ++retry) {
        if (ota_ab_obj_upgrade_start(RTOS_OTA_OBJ_NAME) == 0) {
            start_ok = 1;
            break;
        }
        printf("ota: start failed (retry %d)\n", retry + 1);
        ota_ab_obj_upgrade_stop(RTOS_OTA_OBJ_NAME);
        if (retry < 1) msleep(100);
    }
    if (!start_ok) {
        data->erase_error = 1;
        data->erase_done = 1;   // 避免上位机无限轮询
        printf("ota: erase failed\n");
        return -1;
    }

    /* 擦除完成，置位标志，上位机轮询将返回总块数 */
    data->erase_done = 1;
    printf("ota: erase completed\n");

    /* 数据缓冲区已在 ota_run_upgrade_ab 中分配，直接使用 */
    if (cfg->data == NULL) {
        printf("ota: cfg->data is NULL\n");
        return -1;
    }

    /* 主循环：接收数据并写入 */
    while (1) {
        if (data->restart) {
            data->devstatus = STATUS_OK;
            return -2;
        }

        /* 等待文件长度 */
        if (data->img_len == 0) {
            if (thread_waiter_wait_timeout(&data->file_len_wait, PKT_TRANS_TIMEOUT_MS)) {
                data->devstatus = STATUS_TIMEOUT;
                printf("ota: timeout waiting for file len\n");
                return -1;
            }
            continue;
        }

        /* 等待数据包，带重试 */
        bool pkt_ok = false;
        for (int retry = 0; retry < OTA_AB_RETRY_MAX; retry++) {
            if (pkt_read_sync(ota_transfer, cfg) == 0) {
                pkt_ok = true;
                break;
            }
            printf("ota: pkt_read_sync timeout, retry %d/%d (len=%llu)\n",
                   retry + 1, OTA_AB_RETRY_MAX, len);
            msleep(50);
        }
        if (!pkt_ok) {
            data->devstatus = STATUS_TIMEOUT;
            printf("ota: pkt_read_sync failed after %d retries\n", OTA_AB_RETRY_MAX);
            return -1;
        }

        if (data->restart) {
            data->devstatus = STATUS_OK;
            return -2;
        }

        /* ---- 去重处理 ---- */
        int current_num = cfg->num;
        // 若发生编号循环（从15回到0），重置位图
        if (current_num < data->last_num) {
            memset(data->num_processed, 0, sizeof(data->num_processed));
        }
        // 若当前编号已处理，则视为重复包，不写入
        if (data->num_processed[current_num]) {
            printf("ota: duplicate packet %d, ignoring\n", current_num);
            // 通知读线程可以发送ACK（上位机等待）
            semaphore_post(&ota_transfer->r_sem);
            continue;   // 跳过写入和长度累加
        }
        // 标记当前编号为已处理
        data->num_processed[current_num] = 1;
        data->last_num = current_num;

        /* 写入驱动 */
        wdt_feed();
        if (ota_ab_obj_upgrade_write(RTOS_OTA_OBJ_NAME, cfg->data, cfg->size) != 0) {
            printf("ota: write failed\n");
            data->devstatus = STATUS_ERROR;
            return -1;
        }

        /* CRC 校验 */
        uint32_t crc_val = crc32(0, cfg->data, cfg->size);
        if (crc_val != cfg->data_verify) {
            printf("ota: crc mismatch: 0x%x != 0x%x\n", crc_val, cfg->data_verify);
            data->devstatus = STATUS_ERROR;
            return -1;
        }

        /* 累加整个文件的校验值 */
        data->file_verify = crc32(data->file_verify, cfg->data, cfg->size);

        len += cfg->size;
        semaphore_post(&ota_transfer->r_sem);

        printf("\rota: receiving: %u/%llu bytes", (uint32_t)len, data->img_len);
        fflush(stdout);

        /* 数据传输完成 */
        if (len == data->img_len)
            break;
    }

    printf("\n");
    printf("ota: total file verify: 0x%x\n", data->file_verify);

    /* 停止升级（驱动会将剩余数据刷入Flash） */
    int written = ota_ab_obj_upgrade_stop(RTOS_OTA_OBJ_NAME);
    if (written < 0) {
        printf("ota: stop failed\n");
        return -1;
    }
    printf("ota: written %d bytes\n", written);
    return 0;
}

/* ======================== OTA 回读子流程 ======================== */

static int ota_ab_read_partition(struct ota_transfer *ota_transfer)
{
    struct ota_ab_private_data *data = (struct ota_ab_private_data *)ota_transfer->data;
    struct transfer_cfg *cfg = &data->cfg;

    struct ota_ab_obj_info ota_info;
    if (ota_ab_obj_get_info(RTOS_OTA_OBJ_NAME, &ota_info) != 0) {
        printf("ota: read: cannot get OTA info\n");
        return -1;
    }

    uint64_t offset, size;
    if (ota_ab_get_part_info(ota_info.target_partname, &offset, &size) != 0) {
        printf("ota: read: partition %s not found\n", ota_info.target_partname);
        return -1;
    }
    printf("ota: read: partition %s offset=0x%llx, size=%llu\n",
           ota_info.target_partname, offset, size);

    const struct storage_info *sinfo = ota_ab_flash_info_get();
    uint32_t pagesize = sinfo ? sinfo->pagesize : 4096;
    printf("ota: read: pagesize=%u\n", pagesize);

    if (cfg->data == NULL) {
        cfg->data = malloc(PKT_SIZE_MAX);
        if (!cfg->data) {
            printf("ota: read: malloc failed\n");
            return -1;
        }
    }

    uint32_t len = 0;
    data->file_verify = 0;

    while (len < size) {
        if (data->restart) {
            printf("ota: read: restart requested, exiting\n");
            data->devstatus = STATUS_OK;
            return -2;  // 允许重新开始
        }

        uint32_t to_read = (size - len) > pagesize ? pagesize : (size - len);
        int ret = ota_ab_flash_read(offset + len, to_read, cfg->data);
        if (ret != (int)to_read) {
            printf("ota: read flash error at 0x%llx (ret=%d, expected=%u)\n",
                   offset + len, ret, to_read);
            return -1;
        }

        data->file_verify = crc32(data->file_verify, cfg->data, to_read);

        /* 发送数据包，带重试 */
        bool send_ok = false;
        for (int retry = 0; retry < OTA_AB_RETRY_MAX; retry++) {
            cfg->pkt_type = pkt_partition_context;
            cfg->size = to_read;
            if (pkt_write_sync(ota_transfer, cfg) == 0) {
                send_ok = true;
                break;
            }
            printf("ota: read: pkt_write_sync timeout, retry %d/%d (len=%u)\n",
                   retry + 1, OTA_AB_RETRY_MAX, len);
            // 简单等待后重试
            msleep(50);
        }
        if (!send_ok) {
            data->devstatus = STATUS_TIMEOUT;
            printf("ota: read: pkt_write_sync failed after %d retries, len=%u/%llu\n",
                   OTA_AB_RETRY_MAX, len, size);
            return -1;
        }

        len += to_read;
        printf("\rota: reading: %u/%llu bytes", len, size);
        fflush(stdout);
    }

    printf("\n");
    printf("ota: read total verify: 0x%x\n", data->file_verify);
    return 0;
}

/* ======================== OTA 工作主循环 ======================== */

static void ota_ab_upgrade_work(void *data)
{
    struct ota_transfer *ota_transfer = (struct ota_transfer *)data;
    struct ota_ab_private_data *pri_data = (struct ota_ab_private_data *)ota_transfer->data;
    int ret = -1;
    int consec_retry = 0;

    while (1) {
        /* 判断是否接收到结束信号 */
        if (pri_data->ota_upgrade_end) {
            printf("ota: received end command, exiting\n");
            break;
        }

        /* 初始化命令变量 */
        if (pri_data->ota_cmd)
            pri_data->ota_cmd = OTA_DEFAULT;

        /* 等待start信号和分区名信息 */
        if (!pri_data->restart || pri_data->partition_name[0] == 0) {
            thread_waiter_wait_timeout(&pri_data->wait, PKT_TRANS_TIMEOUT_MS);
            continue;
        }

        pri_data->devstatus = STATUS_OK;
        pri_data->restart = 0;

        /* 等待操作命令（WRITE / READ） */
        if (!pri_data->ota_cmd) {
            if (thread_waiter_wait_timeout(&pri_data->ota_cmd_wait, PKT_TRANS_TIMEOUT_MS)) {
                pri_data->devstatus = STATUS_TIMEOUT;
                if (++consec_retry >= OTA_AB_MAX_RETRY) {
                    printf("ota: too many timeouts waiting for ota_cmd, resetting\n");
                    reset();
                }
                continue;
            }
        }
        consec_retry = 0;

        if (pri_data->ota_cmd == OTA_WRITE) {
            ret = ota_ab_update_partition(ota_transfer);
        } else if (pri_data->ota_cmd == OTA_READ) {
            ret = ota_ab_read_partition(ota_transfer);
        } else {
            printf("ota: unknown command %d\n", pri_data->ota_cmd);
            continue;
        }

        if (ret == -2) {
            pri_data->img_len = 0;
            if (++consec_retry >= OTA_AB_MAX_RETRY) {
                printf("ota: too many retries, resetting\n");
                reset();
            }
            continue;
        } else if (ret == -1) {
            pri_data->devstatus = STATUS_ERROR;
            if (pri_data->ota_cmd == OTA_WRITE)
                semaphore_post(&ota_transfer->r_sem);
            printf("ota: operation FAILED (devstatus=%d), rebooting system...\n", pri_data->devstatus);
            sleep(1);
            reset();
            // reset 后不会执行到这里，但保留 break 以防万一
            break;
        }

        /* ---- 操作成功 ---- */
        consec_retry = 0;
        printf("ota: operation SUCCESS!\n");

        /* 等待结束命令（超时后自动切换） */
        printf("ota: waiting for end command...\n");
        int wait_ret = 0;
        while (!pri_data->ota_upgrade_end) {
            wait_ret = thread_waiter_wait_timeout(&pri_data->wait, 1000);
            if (wait_ret) {
                printf("ota: timeout waiting for end, proceeding\n");
                break;
            }
        }

        /* 切换启动分区 */
        printf("ota: switching boot slot...\n");
        if (ota_ab_obj_change_start_to_other(RTOS_OTA_OBJ_NAME) != 0) {
            printf("ota: switch boot slot failed, rebooting anyway...\n");
            // break;
        }

        printf("ota: upgrade successful, rebooting...\n");
        sleep(1);
        reset();
        break;
    }
}

/* ======================== 升级线程入口 ======================== */

/* 防止重入 */
static volatile int g_ota_busy = 0;

/* OTA waiter declaration for vendor.c main loop blocking */
extern SemaphoreHandle_t ota_sem;

static void ota_run_upgrade_ab(void *data)
{
    const ota_transport_iface_t *transport = (const ota_transport_iface_t *)data;

    if (g_ota_busy) {
        printf("ota: upgrade already in progress\n");
        return;
    }
    g_ota_busy = 1;

    printf("ota: === A/B OTA upgrade (%s) ===\n", transport->name);
    msleep(50); // 等待主线程状态重置完成

    /* 先发送 OK 响应给上位机，告知已收到 otaUpgrade 命令。 */
    const char *ok_response = "OK\r\n";
    int ret = transport->cb->transfer_write((uint8_t *)ok_response, strlen(ok_response));
    if (ret != strlen(ok_response))
        printf("ota: failed to send OK\n");
    else
        printf("ota: OK sent\n");

    /* 挂起所有接收任务，避免干扰 */
    uart_reset_read_wait_ab();
    gadget_serial_reset_read_wait_ab();
    uart_reset_write_wait_ab();
    gadget_serial_reset_write_wait_ab();
    uart_suspend_recv_task();
    gadget_serial_suspend_recv_task();

    /*
     * 先分配并完全初始化私有数据，再创建 ota_transfer（pkt 协议）。
     *
     * 注意：ota_transfer_init 内部会创建 pkt_read_thread 和 pkt_send_thread，
     * 这两个线程启动后立即开始接收数据并回调 pkt_callback_my_r。
     */
    struct ota_ab_private_data *pri_data = malloc(sizeof(struct ota_ab_private_data));
    if (!pri_data) {
        printf("ota: malloc pri_data failed\n");
        goto cleanup;
    }
    memset(pri_data, 0, sizeof(struct ota_ab_private_data));

    /* 先初始化，确保线程安全 */
    semaphore_init(&pri_data->cfg.wait, 0);

    thread_waiter_init(&pri_data->wait);
    thread_waiter_init(&pri_data->file_len_wait);
    thread_waiter_init(&pri_data->DeviceInfo_wait);
    thread_waiter_init(&pri_data->ota_cmd_wait);

    /* 分配数据缓冲区（供写入和回读共用） */
    pri_data->cfg.data = malloc(PKT_SIZE_MAX);
    if (!pri_data->cfg.data) {
        printf("ota: malloc cfg.data failed\n");
        free(pri_data);
        goto cleanup;
    }
    memset(pri_data->cfg.data, 0, PKT_SIZE_MAX);

    /* 初始化传输层 */
    struct ota_transfer *transfer = ota_transfer_init(transport->cb, pkt_callback_my_r);
    if (!transfer) {
        printf("ota: ota_transfer_init failed\n");
        free(pri_data->cfg.data);
        free(pri_data);
        goto cleanup;
    }

    transfer->data = (void *)pri_data;
    pri_data->dev_info.transfer = transfer;

    /* 启动 DeviceInfo 检索线程 */
    pri_data->work_thread = thread_create("dev_info_retriever", 1024,
                                          DeviceInfoRetriever, &pri_data->dev_info);
    if (!pri_data->work_thread) {
        printf("ota: create dev_info thread failed\n");
        free(pri_data->cfg.data);
        free(pri_data);
        ota_transfer_deinit(transfer);
        goto cleanup;
    }

    /* 执行 OTA 主流程（阻塞） */
    ota_ab_upgrade_work(transfer);

    /*
     * ===== 线程清理阶段 =====
     *
     * 清理顺序必须与创建顺序反向，确保所有子线程退出后再释放资源：
     *   1. 通知 DeviceInfoRetriever 线程退出（设置标志 + 唤醒等待）
     *   2. 等待 DeviceInfoRetriever 线程结束
     *   3. 反初始化 pkt 协议（停止读写线程）
     *   4. 释放内存
     *   5. 恢复传输通道接收任务
     *   6. Wake up vendor.c main loop
     */

    /* 步骤 1-2：停止 DeviceInfoRetriever 线程 */
    pri_data->ota_upgrade_end = 1;
    thread_waiter_wakeup(&pri_data->DeviceInfo_wait);
    if (thread_join(pri_data->work_thread, NULL) != 0) {
        printf("ota: warning: DeviceInfoRetriever did not exit cleanly\n");
    }

    /* 步骤 3-4：停止 pkt 协议读写线程并释放资源 */
    int final_devstatus = pri_data->devstatus;
    ota_transfer_deinit(transfer);
    if (pri_data->cfg.data) {
        free(pri_data->cfg.data);
        pri_data->cfg.data = NULL;
    }
    free(pri_data);
    g_ota_busy = 0;

    /* 如果升级失败，尝试清理 OTA 状态 */
    if (final_devstatus != STATUS_OK) {
        ota_ab_obj_upgrade_stop(RTOS_OTA_OBJ_NAME);
    }

    /* 恢复所有接收任务 */
    uart_resume_recv_task();
    gadget_serial_resume_recv_task();

    /* 步骤 6：Wake up vendor.c main loop - OTA completed (success or failure) */
    xSemaphoreGive(ota_sem);

    if (final_devstatus == STATUS_OK)
        printf("ota: upgrade task exited successfully\n");
    else
        printf("ota: upgrade task exited with error %d\n", final_devstatus);
    return;

cleanup:
    g_ota_busy = 0;

    /* 恢复所有传输通道的接收任务 */
    uart_resume_recv_task();
    gadget_serial_resume_recv_task();

    /* Wake up vendor.c main loop even on init failure */
    xSemaphoreGive(ota_sem);
    return;
}

/* ======================== CLI 命令回调 ======================== */

/**
 * @brief 检查 OTA 升级是否正在运行
 * @return 0 未运行，1 正在运行
 */
int ota_is_busy(void)
{
    return g_ota_busy;
}

/**
 * @brief CLI 命令 "otaUpgrade" 的处理函数
 *
 * 由 vendor/uart_cli/cli.c 中的 CliSiganlProcess() 在解析到 "otaUpgrade"
 * 命令时调用，或由 UART3 接收任务（uart_trans.c）直接调用。
 *
 * transport 参数标识命令来源的传输通道，本函数据此选择对应的
 * 传输接口实例（USB CDC 或 UART3），传入升级线程。
 *
 * @param argc      参数个数
 * @param argv      参数字符串数组
 * @param transport 命令来源的传输通道（OTA_TRANSPORT_USB / OTA_TRANSPORT_UART）
 * @return 0 表示命令已处理，-1 表示出错
 */
int32_t ota_cmd_handler(int32_t argc, char *argv[], ota_transport_t transport)
{
    (void)argc;
    (void)argv;

    if (g_ota_busy) {
        printf("ota: upgrade already running\n");
        return 0;
    }

    /* 根据传入的传输通道参数选择对应的接口实例 */
    const ota_transport_iface_t *transport_iface;
    switch (transport) {
    case OTA_TRANSPORT_UART:
        transport_iface = &g_ota_transport_uart;
        break;
    case OTA_TRANSPORT_USB:
    default:
        transport_iface = &g_ota_transport_usb;
        break;
    }

    printf("ota: starting upgrade via %s...\n", transport_iface->name);

    /*
     * 创建独立线程执行升级，将传输接口实例指针作为参数传入。
     * 注意：g_ota_transport_usb / g_ota_transport_uart 是静态常量，
     * 其生命周期贯穿整个系统运行，线程中解引用是安全的。
     */
    thread_ptr_t thread = thread_create("ota_upgrade_ab", 8192,
                                        ota_run_upgrade_ab, (void *)transport_iface);
    if (!thread) {
        printf("ota: failed to create upgrade thread\n");
        return -1;
    }

    /* OTA 升级任务使用最高优先级，确保升级过程中不被其他任务打断 */
    thread_set_priority(thread, OS_priority_realtime);

    return 0;
}

/**
 * @brief 初始化 OTA 触发模块
 *
 * 在系统启动时调用，完成：
 *   1. 注册 RTOS OTA 升级对象（rtos_ota_init）
 */
void ota_trigger_ab_init(void)
{
    /* 注册 RTOS 为 OTA 升级对象
     * 启动标志检测由 rtos_ota_init 内部回调自动完成 */
    if (rtos_ota_init() != 0) {
        printf("ota: rtos_ota_init failed\n");
    } else {
        printf("ota: A/B OTA framework ready (full pkt protocol)\n");
    }
}
