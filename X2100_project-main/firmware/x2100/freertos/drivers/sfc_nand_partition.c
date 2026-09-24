#include <malloc.h>
#include <driver/sfc_nand.h>
#include <mtd_driver_nand.h>


static int sfc_devices_partition_init(const char *mtd_name)
{
    struct mtd_nand_partition *flash_parts = sfc_nand_flash_partition_information();
    if (!flash_parts)
        return -1;

    mtd_nand_init_partition(mtd_name, flash_parts);

    return 0;
}

/***********************************************************************
 *
 ***********************************************************************/
/*
 * sfc devices operation
 */


static int sfc_devices_nand_read_page(struct mtd_nand_device* device, uint32_t page,
                                           uint8_t *data, uint32_t data_len,
                                           uint8_t *spare, uint32_t spare_len)
{
    return sfc_nand_flash_read_page(page, data, data_len, spare, spare_len);
}

static int sfc_devices_nand_write_page(struct mtd_nand_device* device, uint32_t page,
                                            const uint8_t *data, uint32_t data_len,
                                            const uint8_t *spare, uint32_t spare_len)
{
     return sfc_nand_flash_write_page(page, data, data_len, spare, spare_len);
}

static int sfc_devices_nand_erase_block(struct mtd_nand_device* device, uint32_t block)
{
    return sfc_nand_flash_erase_block(block);
}

static int sfc_device_nand_mark_badblock(struct mtd_nand_device* device, uint32_t block)
{
    return sfc_nand_mark_badblock(block * device->page_size * device->pages_per_block);
}

static int sfc_device_nand_check_badblock(struct mtd_nand_device* device, uint32_t block)
{
    return sfc_nand_is_badblock(block * device->page_size * device->pages_per_block);
}

const static struct mtd_nand_driver_ops sfc_nand_ops = {
    .read_page      = sfc_devices_nand_read_page,
    .write_page     = sfc_devices_nand_write_page,
    .erase_block    = sfc_devices_nand_erase_block,
    .mark_badblock = sfc_device_nand_mark_badblock,
    .check_block = sfc_device_nand_check_badblock,
};

int sfc_nand_partition_init(struct mtd_nand_device *mtd_device)
{
    mtd_device->ops = &sfc_nand_ops;

    mtd_devices_nand_register_device("mtd_nand", mtd_device);

    return  sfc_devices_partition_init("mtd_nand");
}
