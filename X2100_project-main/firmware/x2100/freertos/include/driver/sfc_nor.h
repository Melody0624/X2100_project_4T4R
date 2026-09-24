#ifndef _SFC_NOR_H_
#define _SFC_NOR_H_
#include <stdint.h>
#include <driver/storage_info.h>

void sfc_nor_flash_init(void);
int sfc_nor_flash_read(uint32_t from, uint32_t len, uint8_t *buf);
int sfc_nor_flash_write(uint32_t to, uint32_t len, const uint8_t *buf);
int sfc_nor_flash_erase(uint32_t addr, uint32_t len);
const struct storage_info *sfc_nor_flash_info(void);
int get_nor_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size);

#endif /* _SFC_NOR_H_ */
