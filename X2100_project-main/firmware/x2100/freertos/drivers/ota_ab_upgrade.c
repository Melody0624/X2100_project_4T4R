#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <common.h>
#include <os.h>
#include <crc32.h>
#include <driver/mmc_device.h>
#include <driver/sfc_nand.h>
#include <driver/sfc_nor.h>
#include <driver/cache.h>
#include <os/thread.h>
#include "config.h"
#include <driver/ota_ab_upgrade.h>

#if OTA_AB_DEBUG
    #define ota_err(fmt, ...) printf("[ota_ab_err][%s] " fmt, __func__, ##__VA_ARGS__)
#else
    #define ota_err(...) do {} while (0)
#endif

// ota设备升级状态枚举体
enum ota_upgrade_status {
    Upgrade_None = 0, //未升级或升级结束
    Upgrade_Ongoing, //升级中
    Upgrade_Failed, //升级过程中操作失败
};

// ota固件设备句柄
struct ota_ab_obj_handle {
    struct mutex lock;
    struct ota_ab_obj_ops ops; //操作接口

    char obj_name[OTA_AB_NAME_LEN]; //ota固件设备名，唯一标识
    struct ota_ab_obj_info info; //OTA对象信息

    enum ota_upgrade_status upgrade_status; //升级状态
    volatile int written; //目标分区的写入偏移
    volatile int buf_offset; //缓冲区当前已写入偏移
    unsigned int buf_size; //缓冲区大小
    unsigned char *buf; //单次写入flash目标分区的数据缓冲区
};

static DEFINE_MUTEX_RECURSIVE(lock);
static struct ota_ab_obj_handle ota_dev_array[OTA_AB_OBJ_MAX_NUM] = {0};
static int ota_dev_num = 0;

static inline void set_partname(char *dest, const char *src, size_t dest_size)
{
    if (!dest || dest_size == 0)
        return;

    if (src == NULL) {
        dest[0] = '\0';
    } else {
        strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}

static int flash_read(uint64_t from, uint64_t len, uint8_t *buf)
{
    #ifdef CONFIG_OTA_AB_MMC
        return mmc_device_block_read(from, (uint32_t)len, buf);
    #elif defined(CONFIG_OTA_AB_SFC_NAND)
        return sfc_nand_flash_read((uint32_t)from, (uint32_t)len, buf);
    #else
        return sfc_nor_flash_read((uint32_t)from, (uint32_t)len, buf);
    #endif
}

static int flash_write(uint64_t from, uint64_t len, const uint8_t *buf)
{
    #ifdef CONFIG_OTA_AB_MMC
        return mmc_device_block_write(from, (uint32_t)len, buf);
    #elif defined(CONFIG_OTA_AB_SFC_NAND)
        return sfc_nand_flash_write((uint32_t)from, (uint32_t)len, buf);
    #else
        return sfc_nor_flash_write((uint32_t)from, (uint32_t)len, buf);
    #endif
}

static int flash_erase(uint64_t addr, uint64_t len)
{
    #ifdef CONFIG_OTA_AB_MMC
        return mmc_device_block_erase(addr, (uint32_t)len);
    #elif defined(CONFIG_OTA_AB_SFC_NAND)
        return sfc_nand_flash_erase((uint32_t)addr, (uint32_t)len);
    #else
        return sfc_nor_flash_erase((uint32_t)addr, (uint32_t)len);
    #endif
}

int flash_get_part_info(char *name, uint64_t *offset, uint64_t *size)
{

    #ifdef CONFIG_OTA_AB_MMC
        return get_mmc_partition_information_by_name(name, offset, size);
    #else
        int ret = 0;
        uint32_t offset_32, size_32;
        #ifdef CONFIG_OTA_AB_SFC_NAND
            ret = get_nand_partition_information_by_name(name, &offset_32, &size_32);
        #else
            ret = get_nor_partition_information_by_name(name, &offset_32, &size_32);
        #endif

        *offset = offset_32;
        *size = size_32;

        return ret;
    #endif
}

const struct storage_info *flash_info_get(void)
{
    const struct storage_info *storage_info = NULL;

    #ifdef CONFIG_OTA_AB_MMC
        storage_info = mmc_device_storage_info();
    #elif defined(CONFIG_OTA_AB_SFC_NAND)
        storage_info = sfc_nand_flash_info();
    #else
        storage_info = sfc_nor_flash_info();
    #endif

    return storage_info;
}

static unsigned int flash_get_single_write_size(struct ota_ab_obj_handle *handle)
{
    if (handle->buf_size)
        return 0;

    #ifdef CONFIG_OTA_AB_MMC
        handle->buf_size = 512;
    #else
        const struct storage_info *info = flash_info_get();
        if (!info || !info->pagesize) {
            ota_err("flash info invalid\n");
            return -1;
        }
        handle->buf_size = info->pagesize;
    #endif

    return 0;
}

/* ======================== 外部安全接口（供其他模块调用） ======================== */

int ota_ab_flash_read(uint64_t from, uint64_t len, uint8_t *buf)
{
    if (!buf || len == 0)
    {
        ota_err("invalid parameters: buf=%p, len=%llu\n", buf, len);
        return -1;
    }
    return flash_read(from, len, buf);
}

int ota_ab_flash_write(uint64_t from, uint64_t len, const uint8_t *buf)
{
    if (!buf || len == 0)
    {
        ota_err("invalid parameters: buf=%p, len=%llu\n", buf, len);
        return -1;
    }
    return flash_write(from, len, buf);
}

int ota_ab_flash_erase(uint64_t addr, uint64_t len)
{
    if (len == 0)
    {
        ota_err("invalid erase length: %llu\n", len);
        return -1;
    }
    return flash_erase(addr, len);
}

int ota_ab_get_part_info(const char *name, uint64_t *offset, uint64_t *size)
{
    if (!name || !offset || !size)
    {
        ota_err("invalid parameters\n");
        return -1;
    }
    return flash_get_part_info((char *)name, offset, size);
}

const struct storage_info *ota_ab_flash_info_get(void)
{
    return flash_info_get();
}

// 打开ota设备节点
static struct ota_ab_obj_handle *ota_ab_obj_open(const char *name)
{
    if (!name) {
        ota_err("name is NULL\n");
        return NULL;
    }

    int i;
    for(i = 0; i < ota_dev_num; i++) {
        if (!strcmp(ota_dev_array[i].obj_name, name)) {
            return &ota_dev_array[i];
        }
    }

    return NULL;
}

static int ota_ab_obj_upgrade_write_flash(struct ota_ab_obj_handle *handle)
{
    // 获取目标分区的偏移及大小
    uint64_t offset, size;
    struct ota_ab_obj_info *info = &handle->info;
    int ret = flash_get_part_info(info->target_partname, &offset, &size);
    if (ret) {
        ota_err("get %s part information err\n", info->target_partname);
        return -1;
    }

    // 写入数据大小超过目标分区大小则直接写入失败
    unsigned int buf_size = handle->buf_size;
    if (handle->written + buf_size > size) {
        ota_err("The size that %s wants to write exceeds partsize\n", handle->obj_name);
        return -1;
    }

    //按块大小写入目标分区
    uint64_t write_offset = offset + handle->written;
    ret = flash_write(write_offset, buf_size, handle->buf);
    if (ret != buf_size) {
        ota_err("write size:%u data to %s part err, ret = %d\n", buf_size, info->target_partname, ret);
        return -1;
    }

    // crc回读校验写入数据
    unsigned int crc_read = crc32(0, handle->buf, buf_size);
    memset(handle->buf, 0, buf_size);
    ret = flash_read(write_offset, buf_size, handle->buf);
    if (ret != buf_size) {
        ota_err("read size:%u data to %s part err, ret = %d\n", buf_size, info->target_partname, ret);
        return -1;
    }

    /*
     * Cache一致性处理：
     * SFC驱动在DMA传输前通过 sfc_dma_cache_sync_from_device 清除Cache，
     * 但DMA完成后没有再次同步。在DMA传输期间（当前线程阻塞等待），
     * 其他线程可能访问同一内存区域导致Cache被重新填充为旧数据。
     * 此处显式失效Cache并添加内存屏障，确保CPU读取到DMA写入内存的
     * 最新数据，而非Cache中的残留值。
     * invalidate_dcache 要求size对齐到 cpu_scache.linesz，需向上对齐。
     */
    unsigned long scache_linesz = cpu_scache.linesz;
    unsigned long aligned_len = (buf_size + scache_linesz - 1) & ~(scache_linesz - 1);
    invalidate_dcache((unsigned long)handle->buf, aligned_len);
    __sync_synchronize();

    unsigned int crc_reread = crc32(0, handle->buf, buf_size);
    if (crc_reread != crc_read) {
        printf("ota: CRC mismatch @0x%llx+%u: write=0x%08x, readback=0x%08x\n",
               write_offset, buf_size, crc_read, crc_reread);

        /*
         * 重试机制：NOR Flash Page Program 后需要 tPP 时间完成内部编程，
         * 虽然SFC硬件已轮询WIP位，但极端情况下仍可能存在短暂延迟。
         * 延时2ms后重新读回校验。
         */
        usleep(2000);
        memset(handle->buf, 0, buf_size);
            ret = flash_read(write_offset, buf_size, handle->buf);
            if (ret == buf_size) {
                unsigned long scache_linesz = cpu_scache.linesz;
                unsigned long aligned_len = (buf_size + scache_linesz - 1) & ~(scache_linesz - 1);
                invalidate_dcache((unsigned long)handle->buf, aligned_len);
                __sync_synchronize();
                unsigned int crc_retry = crc32(0, handle->buf, buf_size);
            if (crc_retry == crc_read) {
                printf("ota: CRC retry SUCCESS after 2ms delay\n");
                return 0;
            }
            printf("ota: CRC retry FAILED (0x%08x), partition may be damaged @0x%llx\n",
                   crc_retry, write_offset);
        }

        printf("read back the written data crc32 check err\n");
        return -1;
    }

    return 0;
}

// ota_ab升级设备注册
int ota_ab_obj_register(const char *name, const char *ab_partname[2], const struct ota_ab_obj_ops *ops)
{
    mutex_lock(&lock);

    if (!name || !ab_partname || !ops || !ops->fw_start_manage) {
        ota_err("parameter invalid\n");
        goto done;
    }

    if (!ab_partname[0] || !ab_partname[1]) {
        ota_err("The AB partition name does not meet the requirements\n");
        goto done;
    }

    // 检查已注册的ota设备数量
    if (ota_dev_num >= OTA_AB_OBJ_MAX_NUM) {
        ota_err("ota device num has reached the upper limit\n");
        goto done;
    }

    // 检查是否已注册设备名
    struct ota_ab_obj_handle *handle = ota_ab_obj_open(name);
    if (handle) {
        ota_err("%s ota device is registered\n", name);
        goto done;
    }

    // 注册设备
    handle = &ota_dev_array[ota_dev_num];
    struct ota_ab_obj_info *info = &handle->info;

    mutex_init(&handle->lock);
    handle->ops.fw_start_manage = ops->fw_start_manage;
    handle->ops.change_start_to_other = ops->change_start_to_other;

    handle->buf_size = 0;
    handle->buf_offset = 0;
    handle->buf = NULL;
    handle->written = 0;
    handle->upgrade_status = Upgrade_None;

    set_partname(handle->obj_name, name, OTA_AB_NAME_LEN);
    set_partname(info->ab_partname[0], ab_partname[0], OTA_AB_NAME_LEN);
    set_partname(info->ab_partname[1], ab_partname[1], OTA_AB_NAME_LEN);
    set_partname(info->target_partname, NULL, OTA_AB_NAME_LEN);
    set_partname(info->start_info.used_partname, NULL, OTA_AB_NAME_LEN);
    info->start_info.version = -1;
    info->start_info.userdata = NULL;

    // 调用启动管理接口
    int ret = handle->ops.fw_start_manage(ab_partname, &info->start_info);
    if (ret) {
        ota_err("%s firmware start manage err\n", name);
        goto done;
    }

    // 同步设置升级目标，若使用分区不为A分区，则目标分区都为A分区，否则为B分区
    set_partname(info->target_partname, info->ab_partname[0], OTA_AB_NAME_LEN);
    if (!strcmp(info->start_info.used_partname, info->ab_partname[0]))
        set_partname(info->target_partname, info->ab_partname[1], OTA_AB_NAME_LEN);

    ota_dev_num++;
    mutex_unlock(&lock);
    return 0;

done:
    mutex_unlock(&lock);
    return -1;
}

// 获取OTA对象信息
int ota_ab_obj_get_info(const char *name, struct ota_ab_obj_info *info)
{
    struct ota_ab_obj_handle *handle = ota_ab_obj_open(name);
    if (!handle || !info) {
        ota_err("parameter invalid\n");
        return -1;
    }

    mutex_lock(&handle->lock);
    memcpy(info, &handle->info, sizeof(struct ota_ab_obj_info));
    mutex_unlock(&handle->lock);

    return 0;
}

// 修改启动标志，下次启动使用另一分区固件
int ota_ab_obj_change_start_to_other(const char *name)
{
    struct ota_ab_obj_handle *handle = ota_ab_obj_open(name);
    if (!handle) {
        ota_err("%s is no registered\n", name);
        return -1;
    }

    mutex_lock(&handle->lock);

    // 只有在未升级时才可以切换
    if (handle->upgrade_status != Upgrade_None) {
        mutex_unlock(&handle->lock);
        return -1;
    }

    // 切换下一次的启动分区
    int ret = 0;
    if (handle->ops.change_start_to_other)
        ret = handle->ops.change_start_to_other(&handle->info);

    mutex_unlock(&handle->lock);
    return ret;
}

// ota_ab升级设备开始升级
int ota_ab_obj_upgrade_start(const char *name)
{
    struct ota_ab_obj_handle *handle = ota_ab_obj_open(name);
    if (!handle) {
        ota_err("%s is no registered\n", name);
        return -1;
    }

    mutex_lock(&handle->lock);
    struct ota_ab_obj_info *info= &handle->info;

    // 只有不在升级中才能开始升级
    if (handle->upgrade_status != Upgrade_None) {
        mutex_unlock(&handle->lock);
        return -1;
    }

    // 申请单次写入flash的数据缓冲区内存空间
    int ret = flash_get_single_write_size(handle);
    if (ret)
        goto done;

    handle->buf_offset = 0;
    handle->buf = (unsigned char *)cache_align_malloc(handle->buf_size);
    if (!handle->buf) {
        ota_err("Failed to malloc %s blk buffer memory\n", name);
        goto done;
    }

    // 获取目标分区的偏移及大小
    uint64_t offset, size;
    ret = flash_get_part_info(info->target_partname, &offset, &size);
    if (ret) {
        ota_err("get %s part information err\n", info->target_partname);
        goto done;
    }

    // 擦除要升级的目标分区
    ret = flash_erase(offset, size);
    if (ret) {
        ota_err("erase %s partition failed: %d\n", info->target_partname, ret);
        goto done;
    }

    handle->upgrade_status = Upgrade_Ongoing;
    handle->written = 0;

    mutex_unlock(&handle->lock);
    return 0;

done:
    if (handle->buf)
        free(handle->buf);
    handle->buf = NULL;
    mutex_unlock(&handle->lock);
    return -1;
}

// 停止ota设备的固件升级，成功传回升级写入目标分区的固件大小
int ota_ab_obj_upgrade_stop(const char *name)
{
    struct ota_ab_obj_handle *handle = ota_ab_obj_open(name);
    if (!handle) {
        ota_err("%s is no registered\n", name);
        return -1;
    }

    mutex_lock(&handle->lock);

    if (handle->upgrade_status == Upgrade_Ongoing && handle->buf_offset) { // 还处于升级中
        // 获取目标分区的偏移及大小
        uint64_t offset, size;
        struct ota_ab_obj_info *info = &handle->info;
        int ret = flash_get_part_info(info->target_partname, &offset, &size);
        if (ret == 0) {
            // 只写入实际数据字节，不填充 0xFF 到整个页
            // 避免 NOR flash 重复编程同一页（Page Program 重试）导致数据损坏
            unsigned int actual_len = handle->buf_offset;
            uint64_t write_offset = offset + handle->written;
            ret = flash_write(write_offset, actual_len, handle->buf);
            if (ret != actual_len) {
                ota_err("%s ota upgrade last write failed, ret=%d\n", name, ret);
                handle->upgrade_status = Upgrade_Failed;
            } else {
                // 只校验实际写入的数据字节
                unsigned int crc_before = crc32(0, handle->buf, actual_len);
                memset(handle->buf, 0, actual_len);
                ret = flash_read(write_offset, actual_len, handle->buf);
                if (ret == actual_len) {
                    /*
                     * Cache一致性处理：与 ota_ab_obj_upgrade_write_flash 同理，
                     * 确保CPU读取DMA写入的数据而非Cache残留值。
                     * actual_len 可能是最后不足一页的剩余字节数，不保证对齐到
                     * cpu_scache.linesz，需向上对齐后执行Cache失效。
                     */
                    unsigned long scache_linesz = cpu_scache.linesz;
                    unsigned long aligned_len = (actual_len + scache_linesz - 1) & ~(scache_linesz - 1);
                    invalidate_dcache((unsigned long)handle->buf, aligned_len);
                    __sync_synchronize();
                    unsigned int crc_after = crc32(0, handle->buf, actual_len);
                    if (crc_after != crc_before) {
                        printf("ota: upgrade_stop CRC mismatch @0x%llx+%u: write=0x%08x, readback=0x%08x\n",
                               write_offset, actual_len, crc_before, crc_after);
                        printf("read back the written data crc32 check err\n");
                        handle->upgrade_status = Upgrade_Failed;
                    }
                } else {
                    ota_err("read back last write data failed, ret=%d\n", ret);
                    handle->upgrade_status = Upgrade_Failed;
                }
            }
        } else {
            ota_err("get %s part information err\n", info->target_partname);
            handle->upgrade_status = Upgrade_Failed;
        }
        handle->written += handle->buf_offset;
        handle->buf_offset = 0;
    }

    //释放缓冲区
    if (handle->buf)
        free(handle->buf);
    handle->buf = NULL;

    //若不在升级中则返回 -1，否则认为升级成功返回写入flash的数据大小
    int written = handle->written;;
    if (handle->upgrade_status != Upgrade_Ongoing)
        written = -1;

    //设置升级状态为未升级
    handle->upgrade_status = Upgrade_None;

    mutex_unlock(&handle->lock);
    return written;
}

// 写入ota设备升级的新固件数据
int ota_ab_obj_upgrade_write(const char *name, const unsigned char *data, unsigned int len)
{
    struct ota_ab_obj_handle *handle = ota_ab_obj_open(name);
    if (!handle) {
        ota_err("%s is no registered\n", name);
        return -1;
    }

    mutex_lock(&handle->lock);

    // 只有在升级中才能写入
    if (handle->upgrade_status != Upgrade_Ongoing) {
        ota_err("%s is not upgraded\n", name);
        goto done;
    }

    // 将传入数据填充到数据缓冲区中，填充满后写入flash中
    unsigned int buf_size = handle->buf_size;
    while (len > 0) {
        unsigned int copy = buf_size - handle->buf_offset;
        if (copy > len)
            copy = len;

        memcpy(handle->buf + handle->buf_offset, data, copy);
        handle->buf_offset += copy;
        data += copy;
        len -= copy;

        if (handle->buf_offset == buf_size) {
            int ret = ota_ab_obj_upgrade_write_flash(handle);
            if (ret) {
                ota_err("%s ota upgrade write failed", name);
                handle->upgrade_status = Upgrade_Failed;
                goto done;
            }

            handle->buf_offset = 0;
            handle->written += buf_size;
            memset(handle->buf, 0xFF, buf_size);
        }
    }

    mutex_unlock(&handle->lock);
    return 0;

done:
    mutex_unlock(&handle->lock);
    return -1;
}
