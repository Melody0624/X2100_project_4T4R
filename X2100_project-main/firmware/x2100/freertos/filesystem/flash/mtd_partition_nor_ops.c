#include <dfs_device.h>
#include <mtd_driver_nor.h>

//#define MTD_DEBUG

#ifdef MTD_DEBUG
#define MTD_DBG(...)     printf("[MTD]"), printf(__VA_ARGS__)
#else
#define MTD_DBG(...)
#endif

/* Device interface */
static int mtd_part_blk_init(device_t dev)
{
    return 0;
}

static int mtd_part_blk_open(device_t dev, uint16_t oflag)
{
    return 0;
}

static int mtd_part_blk_close(device_t dev)
{
    return 0;
}

static int mtd_part_blk_control(device_t dev, uint8_t cmd, void *args)
{
    struct mtd_nor_partition *mtd_part;

    assert(dev != NULL);

    mtd_part = (struct mtd_nor_partition *)dev;

    switch (cmd) {
    case DEVICE_CTRL_BLK_GETGEOME:
    {
        struct device_blk_geometry *geometry;

        geometry = (struct device_blk_geometry *)args;
        if (geometry == NULL)
            return EINVAL;

        geometry->bytes_per_sector  = mtd_part->sector_size;
        geometry->sector_count      = mtd_part->size / mtd_part->sector_size;
        geometry->block_size        = mtd_part->sector_size;

        break;
    }

    default:
        break;
    }

    return 0;
}

static uint32_t mtd_part_blk_read(device_t dev, uint64_t pos, void* buffer, uint32_t size)
{
    struct mtd_nor_partition *mtd_part;
    struct mtd_nor_device *mtd_nor;
    uint32_t read_count = 0;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;

    assert(dev != NULL);
    assert(size != 0);

    mtd_part = (struct mtd_nor_partition *)dev;
    mtd_nor = (struct mtd_nor_device *)mtd_part->user_data;
    block_size = mtd_part->sector_size;

    MTD_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__, mtd_part->name, pos, size);

    if (!(mtd_part->mask_flags & PART_FLAG_RDONLY)) {
        MTD_DBG("ERROR: this device is unreadable,mask_flags = %04x\n", mtd_part->mask_flags);
        return 0;
    }

    while (read_count < size) {
        /* It'a BLOCK device */
        if (((pos + 1) * block_size) > (mtd_part->offset + mtd_part->size)) {
            MTD_DBG("ERROR: read overrun!\n");
            break;
        }
        mtd_nor_read(mtd_nor, pos * block_size + mtd_part->offset, ptr, block_size);

        pos++;
        ptr += block_size;
        read_count++;
    }

    return read_count;
}

static uint32_t mtd_part_blk_write(device_t dev, uint64_t pos, const void* buffer, uint32_t size)
{
    struct mtd_nor_partition *mtd_part;
    struct mtd_nor_device *mtd_nor;
    uint32_t write_count = 0;
    uint8_t *ptr = (uint8_t *)buffer;
    int block_size;

    assert(dev != NULL);
    assert(size != 0);

    mtd_part = (struct mtd_nor_partition *)dev;
    mtd_nor = (struct mtd_nor_device *)mtd_part->user_data;
    block_size = mtd_part->sector_size;

    MTD_DBG("%s name = %s,position = %08llx,size = %08x\n",__func__,mtd_part->name, pos, size);
    if (!(mtd_part->mask_flags & PART_FLAG_WRONLY)) {
        MTD_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", mtd_part->mask_flags);
        /* read only partition, ignore this data */
        return size;
    }

    while (write_count < size) {
        /* It'a BLOCK device */
        if ((pos + 1) * block_size > (mtd_part->offset + mtd_part->size)) {
            MTD_DBG("ERROR: write overrun!\n");
            break;
        }
        mtd_nor_erase_block(mtd_nor, (pos * block_size + mtd_part->offset), block_size);
        mtd_nor_write(mtd_nor, (pos * block_size + mtd_part->offset), ptr, block_size);

        pos++;
        ptr += block_size;
        write_count++;
    }

    return write_count;
}

static uint32_t mtd_part_mtd_read(struct mtd_nor_device *dev, uint64_t offset, uint8_t *buffer, uint32_t length)
{
    struct mtd_nor_partition *mtd_part;
    struct mtd_nor_device *mtd_nor;

    assert(dev != NULL);

    mtd_part = (struct mtd_nor_partition *)dev;
    mtd_nor = (struct mtd_nor_device *)mtd_part->user_data;

    MTD_DBG("%s offset = %08llx,size = %08x\n", __func__, offset, length);
    if (!(mtd_part->mask_flags & PART_FLAG_RDONLY)) {
        MTD_DBG("ERROR: this device is unreadable,mask_flags = %04x\n", mtd_part->mask_flags);
        return 0;
    }

    if (mtd_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if((offset + length) > mtd_part->size) {
            MTD_DBG("ERROR: read size > partition size, pos=%lld, size=%d, partition_size=%d\n", offset, length, mtd_part->size);
            return 0;
        }

        mtd_nor_read(mtd_nor, (mtd_part->offset + offset), buffer, length);

        return length;
    }

    MTD_DBG("ERROR: unknown device type..\n");
    return 0;
}

static uint32_t mtd_part_mtd_write(struct mtd_nor_device *dev, uint64_t offset, const uint8_t *buffer, uint32_t length)
{
    struct mtd_nor_partition *mtd_part;
    struct mtd_nor_device *mtd_nor;

    assert(dev != NULL);

    mtd_part = (struct mtd_nor_partition *)dev;
    mtd_nor = (struct mtd_nor_device *)mtd_part->user_data;

    MTD_DBG("%s offset = %08llx,size = %08x\n",__func__, offset, length);

    if (!(mtd_part->mask_flags & PART_FLAG_WRONLY)) {
        MTD_DBG("ERROR: this device is unwritable,mask_flags = %04x\n", mtd_part->mask_flags);
        /* read only partition, ignore this data */
        return length;
    }

    if (mtd_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > mtd_part->size) {
            MTD_DBG("ERROR: write size > partition size, pos=%lld, size=%d, partition_size=%d\n", offset, length, mtd_part->size);
            return 0;
        }

        /* MTD device skip erase,user do it by himself */
        mtd_nor_write(mtd_nor, (mtd_part->offset + offset), buffer, length);
        return length;
    }

    MTD_DBG("ERROR: unknown device type..\n");
    return 0;
}

static int mtd_part_mtd_erase_block(struct mtd_nor_device* dev, uint64_t offset, uint32_t length)
{
    struct mtd_nor_partition *mtd_part;
    struct mtd_nor_device *mtd_nor;
    int block_size;

    assert(dev != NULL);

    mtd_part = (struct mtd_nor_partition *)dev;
    mtd_nor = (struct mtd_nor_device *)mtd_part->user_data;
    block_size = mtd_part->sector_size;

    MTD_DBG("%s offset = %08llx,size = %08x\n", __func__, offset, length);

    if (mtd_part->mask_flags & PART_TYPE_MTD) {
        /* It'a MTD device */
        if ((offset + length) > mtd_part->size) {
            MTD_DBG("ERROR: erase size > partition size, pos=%lld, size=%d, partition_size=%d\n", offset, length, mtd_part->size);
            return 0;
        }

        if (length % block_size != 0) {
            MTD_DBG("ERROR: erase size must align to BLOCK SIZE\n");
            return 0;
        }

        mtd_nor_erase_block(mtd_nor, (mtd_part->offset + offset), length);

        return length;
    }

    MTD_DBG("ERROR: unknown device type..\n");
    return 0;
}


const static struct mtd_nor_driver_ops mtd_part_mtd_ops =
{
    .read                   = mtd_part_mtd_read,
    .write                  = mtd_part_mtd_write,
    .erase_block            = mtd_part_mtd_erase_block,
};


long mtd_nor_init_partition(const char *mtd_name,struct mtd_nor_partition *parts)
{
    struct mtd_nor_partition *mtd_part;
    struct mtd_nor_device *mtd_nor;

    mtd_nor = (struct mtd_nor_device *)device_find(mtd_name);
    if (mtd_nor == NULL)
        return -EIO;

    if (parts == NULL)
        return -EIO;

    for (mtd_part = parts; mtd_part->name != NULL; mtd_part++) {
        MTD_DBG("part name: %s\n",mtd_part->name);
        /* get partition type */
        if(mtd_part->mask_flags & PART_TYPE_BLK) {
            /* It'a a BLOCK device */

            /* set device interface */
            mtd_part->blk.type      = Device_Class_Block;
            mtd_part->blk.name      = mtd_part->name;
            mtd_part->blk.init      = mtd_part_blk_init;
            mtd_part->blk.open      = mtd_part_blk_open;
            mtd_part->blk.read      = mtd_part_blk_read;
            mtd_part->blk.write     = mtd_part_blk_write;
            mtd_part->blk.close     = mtd_part_blk_close;
            mtd_part->blk.control   = mtd_part_blk_control;
            mtd_part->blk.flag      = DEVICE_FLAG_RDWR | DEVICE_FLAG_STANDALONE;

            mtd_part->user_data     = mtd_nor;

            /* register device */
            device_register(&mtd_part->blk);
        } else if(mtd_part->mask_flags & PART_TYPE_MTD) { /* It's a MTD device */
            MTD_DBG("part name: %s\n",mtd_part->name);

            mtd_part->user_data = mtd_nor;

            /* Init MTD NOR device interface ... */
            mtd_part->mtd.block_size         = mtd_part->sector_size;
            mtd_part->mtd.block_start        = 0;
            mtd_part->mtd.block_end          = (mtd_part->size / mtd_part->sector_size) - 1;
            mtd_part->mtd.ops                = &mtd_part_mtd_ops;

            mtd_devices_nor_register_device(mtd_part->name, &mtd_part->mtd);
        } else {
            printf("unknown device type...\n");
            printf("Device(%s) type must be PART_TYPE_BLK / PART_TYPE_MTD.\n", mtd_part->name);
        }
    }

    return 0;
}
