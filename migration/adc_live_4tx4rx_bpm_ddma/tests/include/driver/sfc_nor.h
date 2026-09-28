#ifndef TEST_SFC_NOR_H
#define TEST_SFC_NOR_H
#include <stdint.h>
int sfc_nor_flash_read(uint32_t offset, uint32_t length, uint8_t *buffer);
int sfc_nor_flash_write(uint32_t offset, uint32_t length, const uint8_t *buffer);
#endif
