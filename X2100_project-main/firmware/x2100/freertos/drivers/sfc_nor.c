#include <driver/sfc_nor.h>
#include <assert.h>

int soc_sfc_nor_flash_init(void);
int soc_sfc_nor_flash_read(uint32_t from, uint32_t len, uint8_t *buf);
int soc_sfc_nor_flash_write(uint32_t to, uint32_t len, const uint8_t *buf);
int soc_sfc_nor_flash_erase(uint32_t addr, uint32_t len);
const struct storage_info *soc_sfc_nor_flash_info(void);
int soc_get_nor_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size);

void sfc_nor_flash_init(void)
{
    if (soc_sfc_nor_flash_init() != 0)
        panic("nor flash init failed\n");
}

int sfc_nor_flash_read(uint32_t from, uint32_t len, uint8_t *buf)
{
    return soc_sfc_nor_flash_read(from, len, buf);
}

int sfc_nor_flash_write(uint32_t to, uint32_t len, const uint8_t *buf)
{
    return soc_sfc_nor_flash_write(to, len, buf);
}

int sfc_nor_flash_erase(uint32_t addr, uint32_t len)
{
    return soc_sfc_nor_flash_erase(addr, len);
}

const struct storage_info *sfc_nor_flash_info(void)
{
    return soc_sfc_nor_flash_info();
}

int get_nor_partition_information_by_name(char *name, uint32_t *offset, uint32_t *size)
{
    return soc_get_nor_partition_information_by_name(name, offset, size);
}