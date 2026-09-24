#ifndef _SFC_NAND_H_
#define _SFC_NAND_H_
#include <stdint.h>
#include <driver/storage_info.h>

void sfc_nand_flash_init(void);
int sfc_nand_flash_read(uint32_t from, uint32_t len, uint8_t *buf);
int sfc_nand_flash_write(uint32_t to, uint32_t len, const uint8_t *buf);
int sfc_nand_flash_erase(uint32_t addr, uint32_t len);
const struct storage_info *sfc_nand_flash_info(void);
int sfc_nand_is_badblock(uint32_t addr);
int sfc_nand_mark_badblock(uint32_t addr);

int sfc_nand_flash_read_check_badblock(uint32_t *offset, uint32_t size, uint8_t *buf);
int sfc_nand_flash_write_check_badblock(uint32_t offset, uint32_t size, const uint8_t *buf);
int get_nand_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size);


/*为文件系统提供的 api， 以block 或者 page 为单位*/
int sfc_nand_flash_read_page(uint32_t page,
                             uint8_t *data, uint32_t data_len,
                             uint8_t *spare, uint32_t spare_len);

int sfc_nand_flash_write_page(uint32_t page,
                              const uint8_t *data, uint32_t data_len,
                              const uint8_t *spare, uint32_t spare_len);

int sfc_nand_flash_erase_block(uint32_t block);

struct mtd_nand_partition *sfc_nand_flash_partition_information(void);

#endif /* _SFC_NAND_H_ */
