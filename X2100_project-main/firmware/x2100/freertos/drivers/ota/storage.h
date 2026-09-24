#ifndef __STORAGE_H__
#define __STORAGE_H__
#include <stdlib.h>
#include <stdint.h>
#include <driver/ota.h>
#include <list.h>

struct ota_storage {
    uint32_t blocksize;
    uint32_t pagesize;
    uint64_t partsize;
    uint64_t partoffset;

    unsigned char *date_buf;
    struct storage_cb *cb;
};

int get_partition_information(struct ota_storage *storage, char *name, uint64_t *offset, uint64_t *size);
int write_update_partition(struct ota_storage *storage, uint64_t *offset, uint64_t size, uint8_t *data);
int read_update_partition(struct ota_storage *storage, uint64_t *offset, uint64_t size, uint8_t *buf);
int erase_update_partition(struct ota_storage *storage, uint64_t *offset);

struct ota_storage *ota_storage_init(struct storage_cb *cb);
void ota_storage_deinit(struct ota_storage *storage);

#endif