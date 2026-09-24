#ifndef __PKT_COMMON_H__
#define __PKT_COMMON_H__

#include "list.h"
#include <sys/types.h>
#include <semaphore.h>
#include <pthread.h>

#define ACK_DATA "OKAY"
#define CMD_STUFF "SCMD"

#define PKT_SIZE_MAX 4096

#define PKT_SEND_CMD_TYPE_FLAG 0x100
#define PKT_SEND_DATA_TYPE_FLAG 0x200
#define PKT_RECEIVE_TYPE_FLAG 0x400

struct pkt_cfg;
struct pkt_dev;
typedef void (*pkt_callback)(struct pkt_dev *dev, struct pkt_cfg *cfg, int type);
extern int pkt_init_tty(unsigned char *port, int baud, pkt_callback cb);
extern struct pkt_dev *pkt_get_usb_dev(void);

struct pkt_ops {
    int (*pkt_read)(char *buf, ssize_t size);
    void (*pkt_write)(char *buf, ssize_t size);
    void (*pkt_exit)(void);
};

struct pkt_dev {
    int is_work;
    char pkt_name[12];

    struct pkt_ops *pkt_ops;
    pkt_callback cb;

    pthread_spinlock_t w_lock;
    struct list_head w_list;

    pthread_spinlock_t w_down_lock;
    struct list_head w_down_list;

    sem_t r_sem;
    sem_t w_sem;

    struct list_head link;
};

struct pkt_cfg {
    int pkt_type;
    unsigned char *data;
    unsigned int size;
    int num;

    struct pkt_dev *pdev;

    sem_t wait;
    struct list_head link;
};

/**错误类型：
 *  STATUS_ERROR：1.flash回读校验错误;
 *                2.flash读写失败;
 *                3.设备申请内存失败;
 *  STATUS_TIMEOUT: 设备获取包超时。
 */
typedef enum {
    STATUS_OK,
    STATUS_ERROR,
    STATUS_TIMEOUT
} DeviceStatus;

typedef enum {
    OTA_DEFAULT = 0,
    OTA_READ    = 1,
    OTA_WRITE   = 2,
} OtaCmdType;

enum pkt_type {
    /* common pkt type. */
    pkt_ack = 0x1,

    /* send cmd pkt type. */
    pkt_start = PKT_SEND_CMD_TYPE_FLAG,
    pkt_get_partition_info,
    pkt_get_erase_blocks_count,
    pkt_get_device_status,
    pkt_get_file_verify,
    pkt_set_device_reset,
    pkt_ota_upgrade_end,
    pkt_ota_read_partiton,
    pkt_ota_upgrade_partiton,

    /* send data pkt type. */
    pkt_update_partition_name = PKT_SEND_DATA_TYPE_FLAG,
    pkt_ota_update_file_len,
    pkt_ota_update_file,

    /* receive pkt type */
    pkt_flash_info = PKT_RECEIVE_TYPE_FLAG,
    pkt_erase_blocks_count,
    pkt_device_status,
    pkt_file_verify,
    pkt_partition_context,
};



#endif /* __PKT_COMMON_H__ */