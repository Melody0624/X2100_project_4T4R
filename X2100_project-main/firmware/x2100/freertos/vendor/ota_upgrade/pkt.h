#ifndef __PKT_H__
#define __PKT_H__
#include <list.h>
#include <common.h>
#include <os.h>
#include <os/semaphore.h>
#include <cpu/spinlock.h>
#include <driver/ota.h>

#define ACK_DATA "OKAY"

#define PKT_SIZE_MAX 4096

#define PKT_RECEIVE_CMD_TYPE_FLAG 0x100
#define PKT_RECEIVE_DATA_TYPE_FLAG 0x200
#define PKT_SEND_TYPE_FLAG 0x400
#define PKT_DEV_INFO_OFFSET PKT_SEND_TYPE_FLAG - PKT_RECEIVE_CMD_TYPE_FLAG - 1

/**错误类型：
 *  STATUS_ERROR：1.flash回读校验错误;
 *                2.flash读写失败;
 *                3.设备申请内存失败;
 *  STATUS_TIMEOUT: 设备获取包超时。
 */
typedef enum {
    STATUS_OK,
    STATUS_ERROR,
    STATUS_TIMEOUT,
} DeviceStatus;

enum pkt_type {
    /* common pkt type. */
    pkt_ack = 0x1,

    /* recive cmd pkt type. */
    pkt_start = PKT_RECEIVE_CMD_TYPE_FLAG,
    pkt_get_partition_info,
    pkt_get_erase_blocks_count,
    pkt_get_device_status,
    pkt_get_file_verify,
    pkt_set_device_reset,
    pkt_ota_upgrade_end,
    pkt_ota_read_partiton,
    pkt_ota_upgrade_partiton,
    pkt_get_partition_context,

    /* recive data pkt type. */
    pkt_update_partition_name = PKT_RECEIVE_DATA_TYPE_FLAG,
    pkt_ota_update_file_len,
    pkt_ota_update_file,

    /* send pkt type */
    pkt_storage_info = PKT_SEND_TYPE_FLAG,
    pkt_erase_blocks_count,
    pkt_device_status,
    pkt_file_verify,
    pkt_partition_context,
};

struct package_info {
    int32_t num;
    uint32_t sig;
    uint32_t code_len;
    uint32_t code_verify;
    uint32_t head_verify;
    enum pkt_type pkt_type;
};

struct transfer_cfg {
    int num;
    enum pkt_type pkt_type;
    unsigned char *data;
    unsigned int size;
    unsigned int data_verify;

    semaphore_t wait;
    struct list_head link;
};

struct ota_transfer;
typedef int (*pkt_callback)(struct ota_transfer *transfer, struct transfer_cfg *cfg, int type);

struct ota_transfer {
    int is_work;
    void *data;

    int num;/* package num */
    struct transfer_cb *transfer_cb;
    pkt_callback pkt_cb;

    spinlock_t w_lock;
    struct list_head w_list;

    spinlock_t w_down_lock;
    struct list_head w_down_list;

    semaphore_t r_sem;
    semaphore_t w_sem;

    thread_ptr_t read_thread;
    thread_ptr_t write_thread;
};

int package_send(struct transfer_cb *cb, struct transfer_cfg *cfg, unsigned char *package_buf);

int package_read(struct transfer_cb *cb, struct transfer_cfg *cfg, unsigned char *package_buf);

void ack_package_send(struct transfer_cb *cb, int num);

int pkt_read_sync(struct ota_transfer *transfer, struct transfer_cfg *cfg);

int pkt_write_sync(struct ota_transfer *transfer, struct transfer_cfg *cfg);

struct ota_transfer *ota_transfer_init(struct transfer_cb *cb, pkt_callback pkt_r_cb);

void ota_transfer_deinit(struct ota_transfer *transfer);

void pkt_copy_cfg_info(struct transfer_cfg *src, struct transfer_cfg *dst);

#endif