#include <common.h>
#include <os.h>
#include <string.h>
#include "storage.h"
#include <malloc.h>
#include <driver/storage_info.h>

int get_partition_information(struct ota_storage *storage, char *name, uint64_t *offset, uint64_t *size)
{
    struct storage_cb *cb = storage->cb;

    return cb->storage_get_partition_information(name, offset, size);
}

int write_update_partition(struct ota_storage *storage, uint64_t *offset, uint64_t size, uint8_t *data)
{
    if (storage == NULL || data == NULL || size == 0) {
        printf("storage: storage or data cannot be NULL, or size cannot be 0\n");
        return -1;
    }

    int ret;
    uint8_t *buf = storage->date_buf;
    struct storage_cb *cb = storage->cb;

    unsigned int pagesize = storage->pagesize;
    unsigned int blocksize = storage->blocksize;

    unsigned int len = *offset % blocksize;

    /* stuff buf */
    memcpy(buf + len, data, size);
    if (size < pagesize) {
        memset(buf + len + size, 0xff, pagesize - size);
        size = pagesize;
    }

    while (1) {
        /* the uint of write storage align pagesize */
        ret = cb->storage_write(*offset, size, buf + len);
        if (ret == -EIO)
            return -1;
        else if (ret == -1) {
            size += len;
            *offset = (*offset & ~(blocksize - 1)) + blocksize;
            ret = cb->storage_erase(*offset, blocksize);
            if (ret < 0) {
                printf("storage: erase failed \n");
                return -1;
            }
            len = 0;
            continue;
        }
        break;
    }
    *offset = *offset + size;
    return ret;
}

int erase_update_partition(struct ota_storage *storage, uint64_t *offset)
{
    int ret;
    uint32_t size = storage->blocksize;

    ret = storage->cb->storage_erase(*offset, size);
    if (ret < 0) {
        printf("storage: Erase update partition failed \n");
        return -1;
    }

    *offset = *offset + size;
    return ret;
}

/* uint: pagesize */
int read_update_partition(struct ota_storage *storage, uint64_t *offset, uint64_t size, uint8_t *buf)
{
    if (storage == NULL || buf == NULL || size == 0) {
        printf("storage: storage or data cannot be NULL, or size cannot be 0\n");
        return -1;
    }

    int ret;
    struct storage_cb *cb = storage->cb;
    unsigned int pagesize = storage->pagesize;

    ret = cb->storage_read(offset, pagesize, buf);
    if (ret != pagesize)
        return -1;

    return 0;
}

struct ota_storage *ota_storage_init(struct storage_cb *cb)
{
    if(cb == NULL) {
        printf("storage: cb cannot be NULL\n");
        return NULL;
    }

    struct ota_storage *storage = malloc(sizeof(struct ota_storage));
    if (storage == NULL) {
        printf("storage: malloc storage failed\n ");
        return NULL;
    }

    memset(storage, 0, sizeof(struct ota_storage));

    const struct storage_info *info = cb->storage_info_get();
    if (info == NULL) {
        printf("storage: get storage information failed\n");
        free(storage);
        return NULL;
    }

    storage->blocksize = info->erasesize;
    storage->pagesize = info->pagesize;
    storage->cb = cb;

    storage->date_buf = malloc(storage->blocksize);
    if (storage->date_buf == NULL) {
        printf("storage: malloc data buf failed\n");
        free(storage);
        return NULL;
    }

    return storage;
}

void ota_storage_deinit(struct ota_storage *storage)
{
    if (storage->date_buf != NULL) {
        free(storage->date_buf);
        storage->date_buf = NULL;
    }

    free(storage);
    storage = NULL;
}