#ifndef OTA_H
#define OTA_H
#include <common.h>
#include <stdio.h>
#include <driver/storage_info.h>
#include <list.h>

#define PKT_TIMEOUT_MS 500
#define PKT_TRANS_TIMEOUT_MS 3 * 1000
#define PKT_WAIT_TIMES 25

struct ota_updater;
/* common */
struct transfer_cb {
    int (*transfer_init)(void);
    void (*transfer_exit)(void);
    int (*transfer_read)(uint8_t *buf, ssize_t size, uint32_t timeout_ms);
    int (*transfer_write)(uint8_t *buf, ssize_t size);
};

struct storage_cb {
    int (*storage_read)(uint64_t *from, uint64_t len, uint8_t *buf);
    int (*storage_write)(uint64_t to, uint64_t len, const uint8_t *buf);
    int (*storage_erase)(uint64_t addr, uint64_t len);
    const struct storage_info *(*storage_info_get)(void);
    int (*storage_get_partition_information)(char *name, uint64_t *offset, uint64_t *size);
};

/**
 * @brief ota_updater init
 * @param pkt_cb transfer callback, include init, exit, read and write func
 * @param storage_cb storage callback, include read, write, erase, get storage dev info and partition info func
 * @return return a valid ota_updater if success, or return NULL if failed
 * */
struct ota_updater *ota_init(struct transfer_cb *transfer_cb, struct storage_cb *storage_cb);

/**
 *@brief ota work func
 *@param ota_updater ota_updater
 *@return return 0 if success, or return -1 if failed
 **/
int ota_work(struct ota_updater *ota_updater);

/**
 *@brief ota_updater deinit
 *@param ota_updater ota_updater
 **/
void ota_deinit(struct ota_updater *ota_updater);

#endif /* OTA_H */