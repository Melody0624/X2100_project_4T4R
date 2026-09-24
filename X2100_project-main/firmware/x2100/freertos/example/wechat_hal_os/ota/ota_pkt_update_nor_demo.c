#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <os.h>
#include <semaphore.h>
#include <common.h>
#include <ring_mem.h>
#include <driver/sfc_nor.h>
#include <driver/watchdog.h>
#include <include_bin.h>

INCBIN(ota_pkt, "example/wechat_hal_os/ota/ota.bin"); //要用于升级的ota升级包

#if 1
#define OTA_PKT_ERR(fmt, ...) printf("[simple_ota_pkt] " fmt, ##__VA_ARGS__)
#else
#define OTA_PKT_ERR(...) do {} while (0)
#endif

#define PARTITION_RTOS_NAME      "rtos"
#define PARTITION_RTOS_OTA_NAME  "rtos_ota"
#define PARTITION_APP_NAME       "app"
#define PARTITION_APP_OTA_NAME   "app_ota"
#define PARTITION_OTA_NAME       "ota"

#define RTOS_OTA_INFO            "ota:rtos_ota"
#define RTOS_INFO                "ota:rtos"
#define APP_OTA_INFO             "ota:app_ota"
#define APP_INFO                 "ota:app"

#define OTA_INFO_STR_LEN         16
#define OTA_SCRATCH_MAGIC        0x0041544F

#define FLASH_SINGLR_WRITE_SIZE  2048   //flash单次写入大小

//ota分区标志结构体
struct ota_flag {
    char rtos_flag[OTA_INFO_STR_LEN];
    char app_flag[OTA_INFO_STR_LEN];
};

//ota升级包包头结构体
struct ota_pkg_hdr {
    uint32_t hdr_size;
    uint32_t rtos_version;
    uint32_t app_version;

    uint32_t rtos_flags;
    uint32_t app_flags;

    uint32_t userdata_size;
    uint32_t rtos_size;
    uint32_t app_size;
};

//ota升级目标分区结构体
struct ota_pkg_target {
    uint32_t offset_rtos;
    uint32_t size_rtos;

    uint32_t offset_app;
    uint32_t size_app;

    uint32_t rtos_written;
    uint32_t app_written;
};

//获取ota分区标志
int ota_nor_read_flag(int *use_rtos_ota, int *use_app_ota)
{
    uint8_t buf[sizeof(struct ota_flag)];
    uint32_t offset, size;
    int ret;

    if (get_nor_partition_information_by_name(PARTITION_OTA_NAME, &offset, &size)) {
        OTA_PKT_ERR("get ota partition info failed\n");
        return -1;
    }

    memset(buf, 0, sizeof(buf));
    ret = sfc_nor_flash_read(offset, sizeof(struct ota_flag), buf);
    if (ret != (int)sizeof(struct ota_flag)) {
        OTA_PKT_ERR("read ota flag failed\n");
        return -1;
    }

    *use_rtos_ota = (memcmp(buf, RTOS_OTA_INFO, strlen(RTOS_OTA_INFO)) == 0);
    *use_app_ota = (memcmp(buf + OTA_INFO_STR_LEN, APP_OTA_INFO, strlen(APP_OTA_INFO)) == 0);
    return 0;
}

//设置ota分区标志
int ota_nor_write_flag(int use_rtos_ota, int use_app_ota)
{
    int ret;
    uint32_t offset, size;
    struct ota_flag flag = {0};
    const struct storage_info *info = sfc_nor_flash_info();
    if (!info) {
        OTA_PKT_ERR("nor info null\n");
        return -1;
    }

    if (get_nor_partition_information_by_name(PARTITION_OTA_NAME, &offset, &size)) {
        OTA_PKT_ERR("get ota partition info failed\n");
        return -1;
    }

    if (use_rtos_ota)
        memcpy(flag.rtos_flag, RTOS_OTA_INFO, strlen(RTOS_OTA_INFO));
    else
        memcpy(flag.rtos_flag, RTOS_INFO, strlen(RTOS_INFO));

    if (use_app_ota)
        memcpy(flag.app_flag, APP_OTA_INFO, strlen(APP_OTA_INFO));
    else
        memcpy(flag.app_flag, APP_INFO, strlen(APP_INFO));

    ret = sfc_nor_flash_erase(offset, sizeof(struct ota_flag));
    if (ret != 0) {
        OTA_PKT_ERR("erase ota flag failed: %d\n", ret);
        return -1;
    }

    ret = sfc_nor_flash_write(offset, sizeof(struct ota_flag), (const uint8_t *)&flag);
    if (ret != sizeof(struct ota_flag)) {
        OTA_PKT_ERR("write ota flag failed: %d\n", ret);
        return -1;
    }

    return 0;
}

//解析要升级的目标分区的偏移和大小
struct ota_pkg_target target_ota_partition_parse(struct ota_pkg_hdr *hdr)
{
    int ret;
    int ota_rtos_flag;
    int ota_app_flag;
    struct ota_pkg_target target = {0};
    const struct storage_info *info = sfc_nor_flash_info();
    if (!info)
        goto err_info;

    //获取当前正在使用ota分区
    ret = ota_nor_read_flag(&ota_rtos_flag, &ota_app_flag);
    if (ret)
        goto err_info;

    //获取目标分区offset和size大小
    if (hdr->app_flags) {
        if (get_nor_partition_information_by_name(
            ota_app_flag ? PARTITION_APP_NAME : PARTITION_APP_OTA_NAME ,
            &target.offset_app, &target.size_app)) {
                OTA_PKT_ERR("get app partition info failed\n");
                goto err_info;
        }
        if (hdr->app_size > target.size_app)
            goto err_info;
    }

    if (hdr->rtos_flags) {
        if (get_nor_partition_information_by_name(
            ota_rtos_flag ? PARTITION_RTOS_NAME: PARTITION_RTOS_OTA_NAME,
            &target.offset_rtos, &target.size_rtos)) {
            OTA_PKT_ERR("get rtos partition info failed\n");
            goto err_info;
        }
        if (hdr->rtos_size > target.size_rtos)
            goto err_info;
    }

    return target;

err_info:
    target.size_app = 0;
    target.size_rtos = 0;
    return target;
}

//停止看门狗，擦除ota重启标志
int simple_ota_clear_rtos_flag(void)
{
    int use_rtos_ota = 0;
    int use_app_ota = 0;
    int ret = 0;

    wdt_start(1000);
    wdt_stop();

    if (scratch_pad_read() == OTA_SCRATCH_MAGIC) {
        OTA_PKT_ERR("OTA upgrade failed, switching to the next boot partition\n");

        if (ota_nor_read_flag(&use_rtos_ota, &use_app_ota)) {
            OTA_PKT_ERR("read ota flag failed\n");
            ret = -1;
        } else if (ota_nor_write_flag(!use_rtos_ota, use_app_ota)) {
            OTA_PKT_ERR("update ota flag failed\n");
            ret = -1;
        }
    }

    scratch_pad_wirte(0);
    return ret;
}

//更新ota分区标志
void simple_update_ota_flags(int rtos_flags, int app_flags)
{
    int use_rtos_ota, use_app_ota;

    //当前正在使用的固件分区
    ota_nor_read_flag(&use_rtos_ota, &use_app_ota);

    //设置要更新到ota分区的标志
    if (rtos_flags)
        use_rtos_ota = !use_rtos_ota;
    if (app_flags)
        use_app_ota = !use_app_ota;

    //设置ota分区标志
    ota_nor_write_flag(use_rtos_ota, use_app_ota);
}

void ota_pkt_hdr_dump(struct ota_pkg_hdr hdr)
{
     //打印显示包头数据
     OTA_PKT_ERR("ota pkg header info:\n");
     OTA_PKT_ERR("hdr_size     = %u\n", hdr.hdr_size);
     OTA_PKT_ERR("rtos_flags   = %u\n", hdr.rtos_flags);
     OTA_PKT_ERR("rtos_size    = %u\n", hdr.rtos_size);
     OTA_PKT_ERR("rtos_version = %u\n", hdr.rtos_version);
     OTA_PKT_ERR("app_flags    = %u\n", hdr.app_flags);
     OTA_PKT_ERR("app_size     = %u\n", hdr.app_size);
     OTA_PKT_ERR("app_version  = %u\n", hdr.app_version);
     OTA_PKT_ERR("userdata_size  = %u\n", hdr.userdata_size);
     //包头数据处理
     if (hdr.hdr_size > sizeof(struct ota_pkg_hdr))
         OTA_PKT_ERR("The new OTA packet header used in this upgrade cannot be used in the next upgrade with the current packet header size");

}

static uint8_t ota_buf[4096];
struct ring_mem ota_ring_mem;
static sem_t r_sem, w_sem;
static thread_ptr_t thread;
static volatile bool is_ota_pkt_end = false;

/* 将ota升级包中的数据顺序写入环形缓冲区*/
void ota_buf_writer_thread(void *data)
{
    int sem_num;
    int len = 0;
    int free = 0;
    int w_size = ota_pktSize;
    void *ota_data = (void *)ota_pktData;

    while (w_size > 0) {
        free = ring_mem_writable_size(&ota_ring_mem);
        if (!free) {
            sem_wait(&w_sem);
            continue;
        }

        len = w_size < free ? w_size : free;
        len = ring_mem_write(&ota_ring_mem, ota_data, len);

        sem_getvalue(&r_sem, &sem_num);
        if (sem_num <= 0)
            sem_post(&r_sem);

        w_size -= len;
        ota_data += len;
    }

    is_ota_pkt_end = true;
    OTA_PKT_ERR("ota pkt write end\n");
}

//提供ota升级数据，复制size大小的数据传回data,并返回实际传出的数据大小
unsigned int ota_read_buf(void *data, int size)
{
    int sem_num;
    int used = 0;
    int len = 0;
    int r_len = size; //剩余要读大小
    void *ota_data = data;

    while (r_len > 0) {
        used = ring_mem_readable_size(&ota_ring_mem);
        if (!used) {
            if (is_ota_pkt_end)
                return size - r_len; //已读大小
            sem_wait(&r_sem);
            continue;
        }

        len = r_len < used ? r_len : used;
        len = ring_mem_read(&ota_ring_mem, ota_data, len);

        sem_getvalue(&w_sem, &sem_num);
        if (sem_num <= 0)
            sem_post(&w_sem);

        r_len -= len;
        ota_data += len;
    }

    return size - r_len; //已读大小
}

int ota_pkt_update_demo(void)
{
    unsigned int len;
    struct ota_pkg_hdr hdr;
    struct ota_pkg_target target;
    uint8_t *read_userdata_buf = NULL;
    uint8_t write_buf[FLASH_SINGLR_WRITE_SIZE];

    //1. 停止看门狗，擦除ota升级重启标志
    if (simple_ota_clear_rtos_flag()) {
        OTA_PKT_ERR("clear ota flag failed\n");
        return -1;
    }

    /* 初始化ota环形缓冲区， 初始化write线程及信号量 */
    sem_init(&r_sem, 0, 1);
    sem_init(&w_sem, 0, 1);
    ring_mem_init(&ota_ring_mem, ota_buf, sizeof(ota_buf));
    thread = thread_create("ota_buf_writer", 8192, ota_buf_writer_thread, NULL);


    /* read header */
    //2. 读取ota升级包数据，解析包头
    //先读sizeof(uint32_t)大小数据获取包头大小
    len = ota_read_buf(&hdr, sizeof(hdr.hdr_size));
    if (len != sizeof(hdr.hdr_size))
        goto update_err;

    //包头大小不满足退出
    int over_size = hdr.hdr_size - sizeof(hdr);
    if (over_size < 0)
        goto update_err;

    //读取剩余包头大小的数据
    int remain_hdr = sizeof(hdr) - sizeof(hdr.hdr_size);
    len = ota_read_buf(((uint8_t *)&hdr) + sizeof(hdr.hdr_size), remain_hdr);
    if (len != remain_hdr)
        goto update_err;

    //打印显示包头信息
    ota_pkt_hdr_dump(hdr);

    //读取多余的包头数据
    if (over_size > 0) {
        uint8_t byte;
        int i;
        for (i = 0; i < over_size; i++) {
            len = ota_read_buf(&byte, sizeof(byte));
            if (len != sizeof(byte))
                goto update_err;
        }
    }


    /* read userdata */
    //3. 读取ota升级包数据，解析userdata段
    if (hdr.userdata_size) {
        read_userdata_buf = malloc(hdr.userdata_size);
        if (!read_userdata_buf)
            goto update_err;
        len = ota_read_buf(read_userdata_buf, hdr.userdata_size);
        if (len != hdr.userdata_size)
            goto update_err;
    }

    //userdata段处理，打印显示userdata段数据
    if (hdr.userdata_size && read_userdata_buf) {
        OTA_PKT_ERR("  userdata(hex): \n");
        int i = 0;
        for (i = 0; i < hdr.userdata_size; i++){
            printf(" %02x", read_userdata_buf[i]);
            if ((i + 1) % 8 == 0)
                printf("\n");
        }
        printf("\n");

        free(read_userdata_buf);
        read_userdata_buf = NULL;
    }


    //4. 解析目标分区，检查是否进行ota升级
    //解析出目标分区偏移以及分区大小
    target = target_ota_partition_parse(&hdr);
    if (!target.size_app && !target.size_rtos)
        goto update_err;


    /* write firmware */
    //5. 将ota升级包中的rtos固件写入目标分区
    if (target.size_rtos) {
        //擦除目标分区
        sfc_nor_flash_erase(target.offset_rtos, target.size_rtos);

        int rsize = 0;
        int w_size = hdr.rtos_size;
        int offset = target.offset_rtos;

        while (w_size > 0) {
            memset(write_buf, 0xFF, FLASH_SINGLR_WRITE_SIZE);
            rsize = w_size < FLASH_SINGLR_WRITE_SIZE ? w_size : FLASH_SINGLR_WRITE_SIZE;
            len = ota_read_buf(write_buf, rsize);
            if (len == 0 && w_size > 0)
                goto update_err;

            sfc_nor_flash_write(offset, len, write_buf);
            offset += len;
            w_size -= len;

            target.rtos_written += len;
            // if (support_resum) {
            //     write_rootfs(rtos_written);
            // }
        }

        /*其他操作*/

        OTA_PKT_ERR("new rtos firmware write end\n");
    }

    //6. 将ota升级包中的app固件写入目标分区
    if (target.size_app) {
        //擦除目标分区
        sfc_nor_flash_erase(target.offset_app, target.size_app);

        int rsize;
        int w_size = hdr.app_size;
        int offset = target.offset_app;

        while (w_size > 0) {
            memset(write_buf, 0xFF, FLASH_SINGLR_WRITE_SIZE);
            rsize = w_size < FLASH_SINGLR_WRITE_SIZE ? w_size : FLASH_SINGLR_WRITE_SIZE;
            len = ota_read_buf(write_buf, rsize);
            if (len == 0 && w_size > 0)
                goto update_err;

            sfc_nor_flash_write(offset, len, write_buf);
            offset += len;
            w_size -= len;

            target.app_written += len;
            // if (support_resum) {
            //     write_rootfs(rtos_written);
            // }
        }

        /*其他操作*/

        OTA_PKT_ERR("new app firmware write end\n");
    }

    /* update ota flags */
    //7.ota升级包写入目标分区结束,更新ota分区
    simple_update_ota_flags(hdr.rtos_flags, hdr.app_flags);

    /* reset */
    //8.重启
    reset();
    return 0;

update_err:
    sem_destroy(&r_sem);
    sem_destroy(&w_sem);
    thread_delete(thread);

    if (read_userdata_buf)
        free(read_userdata_buf);

    return -1;
}
