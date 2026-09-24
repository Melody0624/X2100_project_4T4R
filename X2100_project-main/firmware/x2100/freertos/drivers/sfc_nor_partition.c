#include <malloc.h>
#include <driver/sfc_nor.h>
#include <mtd_driver_nor.h>

struct mtd_nor_partition *sfc_nor_flash_partition_information(void);

static int sfc_devices_partition_init(const char *mtd_name)
{
    struct mtd_nor_partition *flash_parts = sfc_nor_flash_partition_information();

    mtd_nor_init_partition(mtd_name, flash_parts);

    return 0;
}

/***********************************************************************
 *
 ***********************************************************************/
/*
 * sfc devices operation
 */

/*
 * soc 需要实现
 */
int sfc_nor_flash_read(uint32_t from, uint32_t len, uint8_t *buf);
int sfc_nor_flash_write(uint32_t to, uint32_t len, const uint8_t *buf);
int sfc_nor_flash_erase(uint32_t addr, uint32_t len);

static uint32_t sfc_devices_nor_read(struct mtd_nor_device* device, uint64_t offset, uint8_t* data, uint32_t length)
{
    return sfc_nor_flash_read(offset, length, data);
}

static uint32_t sfc_devices_nor_write(struct mtd_nor_device* device, uint64_t offset, const uint8_t* data, uint32_t length)
{
    return sfc_nor_flash_write(offset, length, data);
}

static int sfc_devices_nor_erase_block(struct mtd_nor_device* device, uint64_t offset, uint32_t length)
{
    return sfc_nor_flash_erase(offset, length);
}

const static struct mtd_nor_driver_ops sfc_nor_ops = {
    .read           = sfc_devices_nor_read,
    .write          = sfc_devices_nor_write,
    .erase_block    = sfc_devices_nor_erase_block,
};

int sfc_nor_partition_init(void)
{
    struct mtd_nor_device *mtd_device = malloc(sizeof(struct mtd_nor_device));
    assert(mtd_device != NULL);

    const struct storage_info *info;

    info = sfc_nor_flash_info();
    mtd_device->ops         = &sfc_nor_ops;
    mtd_device->block_size  = info->erasesize;  /* 该值目前没用到： 读/写/擦除按照sector size对齐操作 */
    mtd_device->block_start = 0;                /* 该值目前没用到 */
    mtd_device->block_end   = info->chipsize / info->erasesize; /* 该值目前没用到 */

    mtd_devices_nor_register_device("mtd_nor", mtd_device);

    sfc_devices_partition_init("mtd_nor");

    return 0;
}
