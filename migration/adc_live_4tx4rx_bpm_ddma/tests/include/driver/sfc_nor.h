#ifndef TEST_SFC_NOR_H
#define TEST_SFC_NOR_H
#include <stdint.h>
struct storage_info { const char *name; uint32_t id, pagesize; uint64_t chipsize; uint32_t erasesize, addrsize; };
int sfc_nor_flash_read(uint32_t offset, uint32_t length, uint8_t *buffer);
int sfc_nor_flash_write(uint32_t offset, uint32_t length, const uint8_t *buffer);
int sfc_nor_flash_erase(uint32_t offset, uint32_t length);
const struct storage_info *sfc_nor_flash_info(void);
#endif
