#include <dfs_device.h>
#include <mtd_driver_nand.h>

//#define MTD_DEBUG

#ifdef MTD_DEBUG
#define MTD_DBG(...)     printf("[MTD]"), printf(__VA_ARGS__)
#else
#define MTD_DBG(...)
#endif

#define MTD_MAX_OOB_SIZE   64

static int mtd_part_mtd_read_page(struct mtd_nand_device *dev, uint32_t page,
                                       uint8_t *data, uint32_t data_len,
                                       uint8_t *spare, uint32_t spare_len)
{
    assert(dev != NULL);

    struct mtd_nand_partition *mtd_part = (struct mtd_nand_partition *)dev;
    struct mtd_nand_device *mtd_nand = (struct mtd_nand_device *)mtd_part->user_data;

    int page_index = mtd_part->offset / mtd_nand->page_size + page;

    MTD_DBG("%s page_index = %08x\n", __func__, page_index);

    if (mtd_part->mask_flags & PART_TYPE_MTD)
        return mtd_nand_read_page(mtd_nand, page_index, data, data_len, spare, spare_len);

    MTD_DBG("ERROR: unknown device type..\n");
    return -MTD_EIO;
}

static int mtd_part_mtd_write_page(struct mtd_nand_device *dev, uint32_t page,
                                        const uint8_t *data, uint32_t data_len,
                                        const uint8_t *spare, uint32_t spare_len)
{
    assert(dev != NULL);

    struct mtd_nand_partition *mtd_part = (struct mtd_nand_partition *)dev;
    struct mtd_nand_device *mtd_nand = (struct mtd_nand_device *)mtd_part->user_data;

    if (!(mtd_part->mask_flags & PART_FLAG_WRONLY)) {
        MTD_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", mtd_part->mask_flags);
        /* read only partition, ignore this data */
        return -MTD_EIO;
    }

    int page_index = mtd_part->offset / mtd_nand->page_size + page;


    MTD_DBG("%s page_index = %08x\n", __func__, page_index);

    if (mtd_part->mask_flags & PART_TYPE_MTD)
        return mtd_nand_write_page(mtd_nand, page_index, data, data_len, spare, spare_len);

    MTD_DBG("ERROR: unknown device type..\n");
    return -MTD_EIO;
}

static int mtd_part_mtd_erase_block(struct mtd_nand_device* dev, uint32_t block)
{
    int ret;
    assert(dev != NULL);

    struct mtd_nand_partition *mtd_part = (struct mtd_nand_partition *)dev;
    struct mtd_nand_device *mtd_nand = (struct mtd_nand_device *)mtd_part->user_data;

    int block_size = mtd_nand->page_size * mtd_nand->pages_per_block;

    int block_index = mtd_part->offset / block_size + block;


    if (mtd_part->mask_flags & PART_TYPE_MTD) {
        ret =  mtd_nand_erase_block(mtd_nand, block_index);
        return ret;
    }

    return -MTD_EIO;
}


int mtd_part_mtd_move_page(struct mtd_nand_device *device, uint32_t src_page, uint32_t dst_page)
{
    return -1;
}

int mtd_part_mtd_mark_badblock(struct mtd_nand_device* device, uint32_t block)
{
    int ret;
    struct mtd_nand_partition *mtd_part = (struct mtd_nand_partition *)device;
    struct mtd_nand_device *mtd_nand = (struct mtd_nand_device *)mtd_part->user_data;


    int block_size = mtd_nand->page_size * mtd_nand->pages_per_block;

    int block_index = mtd_part->offset / block_size + block;

    if (mtd_part->mask_flags & PART_TYPE_MTD) {
        ret =  mtd_nand_mark_badblock(mtd_nand, block_index);
        return ret;
    }


    return -MTD_EIO;
}

int mtd_part_mtd_check_block(struct mtd_nand_device* device, uint32_t block)
{
    int ret;
    struct mtd_nand_partition *mtd_part = (struct mtd_nand_partition *)device;
    struct mtd_nand_device *mtd_nand = (struct mtd_nand_device *)mtd_part->user_data;

    int block_size = mtd_nand->page_size * mtd_nand->pages_per_block;

    int block_index = mtd_part->offset / block_size + block;

    if (mtd_part->mask_flags & PART_TYPE_MTD) {
        ret =  mtd_nand_check_block(mtd_nand, block_index);
        return ret;
    }

    return -MTD_EIO;
}

const static struct mtd_nand_driver_ops mtd_part_mtd_ops =
{
    .write_page = mtd_part_mtd_write_page,
    .read_page = mtd_part_mtd_read_page,
    .erase_block = mtd_part_mtd_erase_block,
    .move_page = mtd_part_mtd_move_page,
    .mark_badblock = mtd_part_mtd_mark_badblock,
    .check_block = mtd_part_mtd_check_block,
};


long mtd_nand_init_partition(const char *mtd_name,struct mtd_nand_partition *parts)
{
    struct mtd_nand_partition *mtd_part;
    struct mtd_nand_device *mtd_nand;

    mtd_nand = (struct mtd_nand_device *)device_find(mtd_name);
    if (mtd_nand == NULL)
        return -EIO;

    if (parts == NULL)
        return -EIO;

    for (mtd_part = parts; mtd_part->name != NULL; mtd_part++) {
        MTD_DBG("part name: %s\n",mtd_part->name);

        /* It's a MTD device */
        if (mtd_part->mask_flags & PART_TYPE_MTD) {

            MTD_DBG("part name: %s\n",mtd_part->name);

            mtd_part->user_data = mtd_nand;

            /* Init MTD NOR device interface ... */
            // mtd_part->mtd.block_size         = mtd_part->sector_size;
            mtd_part->mtd.block_start        = 0;
            mtd_part->mtd.block_end          = (mtd_part->size / mtd_part->sector_size) - 1;
            mtd_part->mtd.ops                = &mtd_part_mtd_ops;

            mtd_part->mtd.page_size = mtd_nand->page_size;
            mtd_part->mtd.pages_per_block = mtd_nand->pages_per_block;

            mtd_part->mtd.oob_size = mtd_nand->oob_size > MTD_MAX_OOB_SIZE ? MTD_MAX_OOB_SIZE : mtd_nand->oob_size;
            mtd_part->mtd.oob_free = mtd_part->mtd.oob_size;
            mtd_part->mtd.ecc_max = mtd_nand->ecc_max;
            mtd_part->mtd.plane_num = mtd_nand->plane_num;


            mtd_devices_nand_register_device(mtd_part->name, &mtd_part->mtd);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be  PART_TYPE_MTD.\n", mtd_part->name);
        }
    }

    return 0;
}
