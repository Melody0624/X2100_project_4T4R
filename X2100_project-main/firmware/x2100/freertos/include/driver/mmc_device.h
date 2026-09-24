#ifndef _MMC_DEVICE_H_
#define _MMC_DEVICE_H_
#include <driver/storage_info.h>

int get_mmc_partition_information_by_name(char *name, uint64_t *offset, uint64_t *size);
const struct storage_info *mmc_device_storage_info(void);
int mmc_device_block_erase(uint64_t address, uint32_t length);
uint32_t mmc_device_block_read(uint64_t from, uint32_t len, uint8_t *buf);
uint32_t mmc_device_block_write(uint64_t to, uint32_t len, const uint8_t *buf);
#endif /* _MMC_DEVICE_H_ */