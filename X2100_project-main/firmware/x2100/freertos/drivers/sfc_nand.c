#include <common.h>
#include <os.h>
#include <driver/sfc_nand.h>
#include <assert.h>

int soc_sfc_nand_flash_init(void);
int soc_sfc_nand_flash_read(uint32_t from, uint32_t len, uint8_t *buf);
int soc_sfc_nand_flash_write(uint32_t to, uint32_t len, const uint8_t *buf);
int soc_sfc_nand_flash_erase(uint32_t addr, uint32_t len);
int soc_sfc_nand_is_badblock(uint32_t addr);
int soc_sfc_nand_mark_badblock(uint32_t addr);
const struct storage_info *soc_sfc_nand_flash_info(void);
int soc_sfc_nand_flash_read_check_badblock(uint32_t *offset, uint32_t size, uint8_t *buf);
int soc_sfc_nand_flash_write_check_badblock(uint32_t offset, uint32_t size, const uint8_t *buf);
int soc_get_nand_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size);

void sfc_nand_flash_init(void)
{
    if (soc_sfc_nand_flash_init() != 0)
        panic("nand flash init failed\n");
}

int sfc_nand_flash_read(uint32_t from, uint32_t len, uint8_t *buf)
{
    return soc_sfc_nand_flash_read(from, len, buf);
}

int sfc_nand_flash_write(uint32_t to, uint32_t len, const uint8_t *buf)
{
    return soc_sfc_nand_flash_write(to, len, buf);
}

int sfc_nand_flash_erase(uint32_t addr, uint32_t len)
{
    return soc_sfc_nand_flash_erase(addr, len);
}

int sfc_nand_is_badblock(uint32_t addr)
{
    return soc_sfc_nand_is_badblock(addr);
}

int sfc_nand_mark_badblock(uint32_t addr)
{
    return soc_sfc_nand_mark_badblock(addr);
}

const struct storage_info *sfc_nand_flash_info(void)
{
    return soc_sfc_nand_flash_info();
}

int sfc_nand_flash_read_check_badblock(uint32_t *offset, uint32_t size, uint8_t *buf)
{
    return soc_sfc_nand_flash_read_check_badblock(offset, size, buf);
}

int sfc_nand_flash_write_check_badblock(uint32_t offset, uint32_t size, const uint8_t *buf)
{
    return soc_sfc_nand_flash_write_check_badblock(offset, size, buf);
}

int get_nand_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size)
{
    return soc_get_nand_partition_information_by_name(name, offset, size);
}

/*************为文件系统提供的api*********************/
__weak int soc_sfc_nand_flash_write_page(uint32_t page,
                                         const uint8_t *data, uint32_t data_len,
                                         const uint8_t *spare, uint32_t spare_len)
{
    printf("ERROR: this soc does not support nand page write func!\n");
    return -1;
}

__weak int soc_sfc_nand_flash_read_page(uint32_t page,
                                        uint8_t *data, uint32_t data_len,
                                        uint8_t *spare, uint32_t spare_len)
{
    printf("ERROR: this soc does not support nand page read func!\n");
    return -1;
}

__weak int soc_sfc_nand_flash_erase_block(uint32_t block)
{
    printf("ERROR: this soc does not support nand block erase func!\n");
    return -1;
}

__weak struct mtd_nand_partition *soc_sfc_nand_flash_partition_information(void)
{
    printf("ERROR: this soc does not support get nand partition info func!\n");
    return NULL;
}


int sfc_nand_flash_read_page(uint32_t page,
                             uint8_t *data, uint32_t data_len,
                             uint8_t *spare, uint32_t spare_len)
{
    return soc_sfc_nand_flash_read_page(page, data, data_len, spare, spare_len);
}

int sfc_nand_flash_write_page(uint32_t page,
                              const uint8_t *data, uint32_t data_len,
                              const uint8_t *spare, uint32_t spare_len)
{
    return soc_sfc_nand_flash_write_page(page, data, data_len, spare, spare_len);
}

int sfc_nand_flash_erase_block(uint32_t block)
{
    return soc_sfc_nand_flash_erase_block(block);
}

struct mtd_nand_partition *sfc_nand_flash_partition_information(void)
{
    return soc_sfc_nand_flash_partition_information();
}