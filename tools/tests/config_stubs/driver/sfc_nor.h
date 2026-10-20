#ifndef TEST_SFC_NOR_H
#define TEST_SFC_NOR_H
#include <stdint.h>
struct storage_info { uint32_t erasesize; };
int sfc_nor_flash_read(uint32_t,uint32_t,uint8_t*);
int sfc_nor_flash_write(uint32_t,uint32_t,const uint8_t*);
int sfc_nor_flash_erase(uint32_t,uint32_t);
const struct storage_info *sfc_nor_flash_info(void);
int get_nor_partition_information_by_name(char*,uint32_t*,uint32_t*);
#endif
