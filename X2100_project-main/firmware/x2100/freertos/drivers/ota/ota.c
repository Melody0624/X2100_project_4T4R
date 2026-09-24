#include "storage.h"
#include "pkt.h"
#include <driver/watchdog.h>
#include <sys/time.h>
#include <driver/ota.h>
#include <driver/cache.h>
#include <crc32.h>

#define CONFIG_RTOS_OTA_NAME "ota"
#define OTA_DEFAULT    0
#define OTA_READ       1
#define OTA_WRITE      2

struct ota_updater {
    struct ota_private_data *data;

    struct ota_transfer *ota_transfer;
    struct ota_storage *ota_storage;
};

struct ota_storage_info {
    uint32_t pagesize;
    uint64_t partsize;
    uint32_t blocks;
};

typedef struct device_info {
    enum pkt_type pkt_type;
    struct ota_transfer *transfer;
} DeviceInfo;

struct ota_private_data {
    struct ota_storage_info storage_info;

    unsigned int img_len;
    char partition_name[128];

    struct transfer_cfg cfg;
    unsigned long file_verify;/* 整个文件的校验值 */
    DeviceInfo dev_info;

    int restart;   //默认为0
    int ota_cmd;   //默认为OTA_DEFAULT
    int devstatus; //默认为STATUS_OK

    int erase_blocks_cnt;
    int ota_upgrade_end;

    thread_waiter_t wait;
    thread_waiter_t ota_cmd_wait;
    thread_waiter_t file_len_wait;
    thread_waiter_t DeviceInfo_wait;

    thread_ptr_t work_thread;
};

static void DeviceInfoRetriever(void *data)
{
    int ret;
    DeviceInfo *info = (DeviceInfo *)data;
    enum pkt_type type = -1;
    struct ota_transfer *transfer = info->transfer;
    struct ota_private_data *pri_data = (struct ota_private_data *)transfer->data;
    struct transfer_cfg cfg;

    thread_waiter_init(&pri_data->DeviceInfo_wait);

    while (!pri_data->ota_upgrade_end) {
        //不需要发送dev info的消息时，每三秒判断一下ota是否需要退出
        ret = thread_waiter_wait_timeout(&pri_data->DeviceInfo_wait, PKT_TRANS_TIMEOUT_MS);
        if (ret)
            continue;

        type = info->pkt_type;
        cfg.pkt_type = type;

        switch (type) {
        case pkt_storage_info:
            cfg.size = sizeof(struct ota_storage_info);
            cfg.data = (unsigned char *)&pri_data->storage_info;
            break;
        case pkt_erase_blocks_count:
            cfg.size = sizeof(pri_data->erase_blocks_cnt);
            cfg.data = (unsigned char *)&pri_data->erase_blocks_cnt;
            break;
        case pkt_device_status:
            cfg.size = sizeof(pri_data->devstatus);
            cfg.data = (unsigned char *)&pri_data->devstatus;
            break;
        case pkt_file_verify:
            cfg.size = sizeof(pri_data->file_verify);
            cfg.data = (unsigned char *)&pri_data->file_verify;
            break;
        default:
            printf("not support to send this info type\n");
            return;
        }

        //发送超时将设备设置为超时的状态
        if (pkt_write_sync(transfer, &cfg)) {
            pri_data->devstatus = STATUS_TIMEOUT;
        }
    }
}

void pkt_save_data(struct transfer_cfg *src, struct transfer_cfg *dst)
{
    if (dst->data == NULL)
        return;

    pkt_copy_cfg_info(src, dst);
}

static void pkt_cmd_process(struct ota_transfer *transfer, int type)
{
    assert(transfer);
    struct ota_private_data *data = (struct ota_private_data *)transfer->data;
    DeviceInfo *info = &data->dev_info;

    info->transfer = transfer;

    switch (type) {
    case pkt_start:
        data->storage_info.blocks = 0;
        data->storage_info.partsize = 0;
        memset(data->partition_name, 0, sizeof(data->partition_name));
        data->restart = 1;

        /* 如果设备状态为timeout，检查是否有接收到数据但是没有处理的情况 */
        if (data->devstatus == STATUS_TIMEOUT) {
            int ret = semaphore_try_wait(&data->cfg.wait);
            if (!ret)
                semaphore_post(&transfer->r_sem);
        }

        break;
    case pkt_get_partition_info:
    case pkt_get_erase_blocks_count:
    case pkt_get_device_status:
    case pkt_get_file_verify:
        info->pkt_type = type + PKT_DEV_INFO_OFFSET;
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
        printf("ota: Unsupport package type: %d!\n", type);
    }
}

/** temporary commit
 * 数据长时间未被处理，可能是flash的通讯异常导致
 * 此类情况，将CPU复位
*/
static void cope_with_data_timeout(void)
{
    printf("Why cope with data timeout!!!???\n");
    printf("Cpu reset...\n");
    sleep(2);
    reset();
}

static void pkt_data_process(struct ota_transfer *transfer, struct transfer_cfg *cfg, int type)
{
    int cnt = PKT_WAIT_TIMES;
    assert(transfer && cfg->data);
    struct ota_private_data *data = (struct ota_private_data *)transfer->data;

    switch(type) {
    case pkt_update_partition_name:
        memcpy(data->partition_name, cfg->data, cfg->size);
        thread_waiter_wakeup(&data->wait);
        break;
    case pkt_ota_update_file_len:
        if (data->devstatus == STATUS_OK) {
            data->img_len = *(unsigned int *)cfg->data;
            thread_waiter_wakeup(&data->file_len_wait);
            printf("ota:file len : %d \n", data->img_len);
        }
        break;
    case pkt_ota_update_file:

        /* 进入接收数据的流程，先检查设备状态是否为ok，
        ** r_sem信号量唤醒没有超时，正常处理数据
        ** r_sem信号量唤醒超时，判断设备状态，若为ok，继续等待，不为ok，退出本次数据处理
        */

        if (data->devstatus != STATUS_OK)
            break;
        while (cnt--) {
            int ret = semaphore_wait_timeout(&transfer->r_sem, PKT_TRANS_TIMEOUT_MS);
            if (!ret) {
                pkt_save_data(cfg, &data->cfg);
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
        printf("ota: Unsupport package type: %d!\n", type);
    }
}

static int pkt_callback_my_r(struct ota_transfer *transfer, struct transfer_cfg *cfg, int type)
{
    int ret = 0;
    struct ota_private_data *data = (struct ota_private_data *)transfer->data;

    if (type & PKT_SEND_TYPE_FLAG) {
        printf("ota: error package type : %d\n", type);
        return ret;
    }

    if (type & PKT_RECEIVE_DATA_TYPE_FLAG) {
        pkt_data_process(transfer, cfg, type);
    } else {
        pkt_cmd_process(transfer, type);
    }

    if (data->devstatus == STATUS_ERROR)
        ret = 1;

    return ret;
}

/**
 * @brief ota update receive bin data and write storage func
 * @param ota_updater ota ota_updater
 * @return return -2 if receive restart pkt
 *         return -1 if ota update failed
 *         return  0 if success
 * */
static int ota_update_patition(struct ota_updater *ota_updater)
{
    int ret = 0;
    struct ota_private_data *data = ota_updater->data;
    struct transfer_cfg *cfg = &data->cfg;
    struct ota_transfer *ota_transfer = ota_updater->ota_transfer;
    struct ota_storage *ota_storage = ota_updater->ota_storage;

    int ebcnt = data->storage_info.blocks;
    uint64_t offset = ota_storage->partoffset;
    uint64_t end = offset + ota_storage->partsize;
    unsigned int crc_val = 0;
    data->file_verify = 0;

    /* 擦除分区 */
    for (data->erase_blocks_cnt = 0; data->erase_blocks_cnt < ebcnt; data->erase_blocks_cnt++) {
        erase_update_partition(ota_storage, &offset);
        if (data->devstatus == STATUS_TIMEOUT)
            return -2;
        if (data->restart) {
            data->devstatus = STATUS_OK;
            return -2;
        }
    }

    cfg->data = cache_align_malloc(PKT_SIZE_MAX);
    if (cfg->data == NULL) {
        printf("ota: malloc data->cfg.data failed\n");
        return -1;
    }
    memset(cfg->data, 0, PKT_SIZE_MAX);

    uint32_t len = 0;
    offset = ota_storage->partoffset;
    uint64_t offset_tmp = offset;
    while(1) {

        /* 检查restart变量的值 */
        if (data->restart) {
            free(cfg->data);
            data->devstatus = STATUS_OK;
            return -2;
        }

        /* 等待接收到文件长度 */
        if (data->img_len == 0) {
            ret = thread_waiter_wait_timeout(&data->file_len_wait, PKT_TRANS_TIMEOUT_MS);
            if (ret) {
                free(cfg->data);
                data->devstatus = STATUS_TIMEOUT;
                return -2;
            }
            continue;
        }

        /* 此处的read只等待数据，一直没有等到数据就将设备状态设置为超时 */
        ret = pkt_read_sync(ota_transfer, cfg);
        if (ret) {
            free(cfg->data);
            data->devstatus = STATUS_TIMEOUT;
            return -2;
        }

        /* 检查restart变量的值 */
        if (data->restart) {
            free(cfg->data);
            data->devstatus = STATUS_OK;
            return -2;
        }

        /* 将接收到的数据写入到flash */
        ret = write_update_partition(ota_storage, &offset, cfg->size, cfg->data);
        if (ret < 0 || offset >= (end + ota_storage->pagesize)) {
            free(cfg->data);
            printf("ota: write update flash partition failed or offset exceed partition end\n");
            return -1;
        }

        /* 将写入的数据回读 检查校验值 */
        ret = read_update_partition(ota_storage, &offset_tmp, cfg->size, cfg->data);
        if (ret < 0) {
            free(cfg->data);
            printf("ota: read flash partition failed \n");
            return -1;
        }

        /* 获取数据的校验值 */
        crc_val = crc32(0, cfg->data, cfg->size);
        if (crc_val != cfg->data_verify) {
            printf("ota: read data not equal to pkt data crc_val: %x cfg->data_verify : %x\n", crc_val, cfg->data_verify);
            free(cfg->data);
            return -1;
        }

        /* 获取整个文件的crc校验值 */
        data->file_verify = crc32(data->file_verify, cfg->data, cfg->size);

        offset_tmp += cfg->size;
        len += cfg->size;
        semaphore_post(&ota_transfer->r_sem);

        printf("\rota: receiving : %d", len);
        fflush(stdout);

        /* 接收到的数据和文件长度一致就退出 */
        if (len == data->img_len) {
            break;
        }
    }

    printf("\n");
    fflush(stdout);
    printf("file verify : %lx\n", data->file_verify);

    free(cfg->data);
    return 0;
}

/**
 * @brief ota read partitoon context
 * @param ota_updater ota ota_updater
 * @return return -2 if receive restart pkt
 *         return -1 if ota read partition context failed
 *         return  0 if success
 * */
static int ota_read_partiton(struct ota_updater *ota_updater)
{
    int ret = 0;
    uint32_t len = 0;

    struct ota_private_data *data = ota_updater->data;
    struct transfer_cfg *cfg = &data->cfg;
    struct ota_transfer *ota_transfer = ota_updater->ota_transfer;
    struct ota_storage *ota_storage = ota_updater->ota_storage;

    uint64_t offset = ota_storage->partoffset;
    uint32_t pagesize = ota_storage->pagesize;
    data->file_verify = 0;

    cfg->data = cache_align_malloc(PKT_SIZE_MAX);
    if (cfg->data == NULL) {
        printf("ota: malloc data->cfg.data failed\n");
        return -1;
    }
    memset(cfg->data, 0, PKT_SIZE_MAX);

    while(1) {
        /* 读flash中offset一页大小的数据 */
        ret = read_update_partition(ota_storage, &offset, pagesize, cfg->data);
        if (data->restart) {
            free(cfg->data);
            data->devstatus = STATUS_OK;
            return -2;
        }

        /* 读错误退出 */
        if (ret < 0) {
            free(cfg->data);
            printf("ota: read update flash partition failed\n");
            return -1;
        }

        data->file_verify = crc32(data->file_verify, cfg->data, pagesize);

        /* 发送一页分区的信息 */
        cfg->pkt_type = pkt_partition_context;
        cfg->size = pagesize;
        ret = pkt_write_sync(ota_transfer, cfg);

        /* 写获取ack超时，此时设备认为上位机未接收到信息，是否需要重新发送，还是直接进到新的一轮的等待 */
        if (ret) {
            free(cfg->data);
            data->devstatus = STATUS_TIMEOUT;
            return -2;
        }

        /* 检查restart变量的值 */
        if (data->restart) {
            free(cfg->data);
            data->devstatus = STATUS_OK;
            return -2;
        }

        len += pagesize;
        offset += pagesize;

        printf("\rota: sending %d", len);
        fflush(stdout);

        if (len == ota_storage->partsize) {
            break;
        }
    }

    printf("\n");
    fflush(stdout);
    printf("data->file_verify : %lx\n", data->file_verify);

    free(cfg->data);
    return 0;
}

/**
 * @brief ota update start func
 * @param ota_updater ota ota_updater
 * @return success return 0, failed return -1, exit return 1
 * */
int ota_work(struct ota_updater *ota_updater)
{
    int ret = -1;
    struct ota_storage *ota_storage = ota_updater->ota_storage;
    struct ota_transfer *ota_transfer = ota_updater->ota_transfer;
    struct ota_private_data *data = ota_updater->data;

    while(1) {
        /* 判断是否接收到结束信号 */
        if (data->ota_upgrade_end) {
            ret = 1;
            break;
        }

        /* 初始化ota_cmd变量 */
        if(data->ota_cmd)
            data->ota_cmd = OTA_DEFAULT;

        /* 等待start信号和分区名信息,每三秒检查ota_upgrade_end的状态 */
        if (!data->restart || data->partition_name[0] == 0) {
            thread_waiter_wait_timeout(&data->wait, PKT_TRANS_TIMEOUT_MS);
            continue;
        }

        data->devstatus = STATUS_OK;
        data->restart = 0;

        ret = get_partition_information(ota_storage, data->partition_name, &ota_storage->partoffset, &ota_storage->partsize);
        if (ret == -1) {
            printf("ota: get upgrade partition information failed, please check partition name is correct\n");
            continue;
        }

        data->storage_info.partsize = ota_storage->partsize;
        data->storage_info.blocks = ota_storage->partsize / ota_storage->blocksize;

        /* 等待接收ota_cmd命令 */
        if (!data->ota_cmd) {
            ret = thread_waiter_wait_timeout(&data->ota_cmd_wait, PKT_TRANS_TIMEOUT_MS);
            if (ret) {
                data->devstatus = STATUS_TIMEOUT;
                continue;
            }
        }

        if (data->ota_cmd == OTA_WRITE) {
            ret = ota_update_patition(ota_updater);
        } else if (data->ota_cmd == OTA_READ) {
            ret = ota_read_partiton(ota_updater);
        }

        if (ret == -2) {
            data->img_len = 0;
            continue;
        } else if(ret == -1) {
            data->devstatus = STATUS_ERROR;
            if (data->ota_cmd == OTA_WRITE)
                semaphore_post(&ota_transfer->r_sem);

            printf("ota: update or read partition failed!\n");
            break;
        }
        printf("ota: update or read partition success!\n");
        printf("\n");
    }

    return ret;
}

struct ota_updater *ota_init(struct transfer_cb *transfer_cb, struct storage_cb *storage_cb)
{
    if (transfer_cb == NULL || storage_cb == NULL) {
        printf("ota init : pkt_cb or storage_cb cannot be NULL!\n");
        return NULL;
    }

    struct ota_updater *ota_updater = malloc(sizeof(struct ota_updater));
    if (ota_updater == NULL) {
        printf("ota init: malloc ota_updater failed!\n");
        return NULL;
    }
    memset(ota_updater, 0, sizeof(struct ota_updater));

    ota_updater->ota_storage = ota_storage_init(storage_cb);
    if (ota_updater->ota_storage == NULL) {
        printf("ota init : init storage dev failed!\n");
        goto free_handle;
    }

    ota_updater->ota_transfer = ota_transfer_init(transfer_cb, pkt_callback_my_r);
    if (ota_updater->ota_transfer == NULL) {
        printf("ota init: init pkt dev failed!\n");
        goto free_storage_handle;
    }

    ota_updater->data = malloc(sizeof(struct ota_private_data));
    if (ota_updater->data == NULL) {
        printf("ota init: malloc ota_updater->data failed!\n");
        goto free_transfer_handle;
    }
    memset(ota_updater->data, 0, sizeof(struct ota_private_data));

    ota_updater->ota_transfer->data = (void *)ota_updater->data;
    ota_updater->data->storage_info.pagesize = ota_updater->ota_storage->pagesize;
    ota_updater->data->dev_info.transfer = ota_updater->ota_transfer;

    ota_updater->data->work_thread = thread_create("device_info_retriever", 1024, DeviceInfoRetriever, &ota_updater->data->dev_info);
    if (ota_updater->data->work_thread == NULL) {
        printf("ota init: create devinforetriever thread failed!\n");
        goto free_transfer_handle;
    }

    semaphore_init(&ota_updater->data->cfg.wait, 0);

    thread_waiter_init(&ota_updater->data->wait);
    thread_waiter_init(&ota_updater->data->file_len_wait);
    thread_waiter_init(&ota_updater->data->DeviceInfo_wait);
    thread_waiter_init(&ota_updater->data->ota_cmd_wait);

    return ota_updater;

free_transfer_handle:
    free(ota_updater->ota_transfer);
    ota_updater->ota_transfer = NULL;
free_storage_handle:
    free(ota_updater->ota_storage);
    ota_updater->ota_storage = NULL;
free_handle:
    free(ota_updater);
    ota_updater = NULL;
    return NULL;
}

void ota_deinit(struct ota_updater *ota_updater)
{
    if (ota_updater->data->work_thread != NULL)
        thread_join(ota_updater->data->work_thread, NULL);

    ota_transfer_deinit(ota_updater->ota_transfer);
    ota_storage_deinit(ota_updater->ota_storage);

    free(ota_updater->ota_storage);
    free(ota_updater->ota_transfer);
    free(ota_updater->data);
    free(ota_updater);
}