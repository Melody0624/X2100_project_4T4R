/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2011-12-05     Bernard      the first version
 * 2011-04-02     prife        add mark_badblock and check_block
 */

/*
 * COPYRIGHT (C) 2012, Shanghai Real Thread
 */

#ifndef __MTD_NAND_H__
#define __MTD_NAND_H__

#include <dfs_device.h>
#include <stdio.h>

struct mtd_nand_device;

struct mtd_nand_driver_ops;
#define MTD_NAND_DEVICE(device)  ((struct mtd_nand_device*)(device))

#define MTD_EOK          0   /* NO error */
#define MTD_EECC         1   /* ECC error */
#define MTD_EBUSY        2   /* hardware busy */
#define MTD_EIO          3   /* generic IO issue */
#define MTD_ENOMEM       4   /* out of memory */
#define MTD_ESRC         5   /* source issue */
#define MTD_EECC_CORRECT 6   /* ECC error but correct */

#define PART_FLAG_RDONLY                0x0001
#define PART_FLAG_WRONLY                0x0002
#define PART_FLAG_RDWR                  0x0003

#define PART_TYPE_BLK                   0x0010
#define PART_TYPE_MTD                   0x0020



struct mtd_nand_device
{
    struct device parent;

    char name[64];

    uint16_t page_size;          /* The Page size in the flash */
    uint16_t oob_size;           /* Out of bank size */
    uint16_t oob_free;           /* the free area in oob that flash driver not use */
    uint16_t plane_num;          /* the number of plane in the NAND Flash */

    uint32_t pages_per_block;    /* The number of page a block */
    uint16_t block_total;

    uint32_t block_start;        /* The start of available block*/
    uint32_t block_end;          /* The end of available block */

    uint32_t ecc_max;

    /* operations interface */
    const struct mtd_nand_driver_ops* ops;
};


struct mtd_nand_partition {
    union {
        struct mtd_nand_device mtd;
        struct device blk;
    };

    const char *name;
    uint32_t offset;            /* offset within the master MTD space */
    uint32_t size;              /* partition size */
    uint32_t sector_size;       /* sector size: unit Byte. 同一个存储介质不同分区烧录不同镜像sector size可以不相同 */
    uint32_t mask_flags;        /* master MTD flags to mask out for this partition */
    void *user_data;            /* hold parent device */
};



struct mtd_nand_driver_ops
{
    int (*read_id) (struct mtd_nand_device* device);

    int (*read_page)(struct mtd_nand_device* device,
                          uint32_t page,
                          uint8_t* data, uint32_t data_len,
                          uint8_t * spare, uint32_t spare_len);

    int (*write_page)(struct mtd_nand_device * device,
                           uint32_t page,
                           const uint8_t * data, uint32_t data_len,
                           const uint8_t * spare, uint32_t spare_len);

    int (*move_page) (struct mtd_nand_device *device, uint32_t src_page, uint32_t dst_page);

    int (*erase_block)(struct mtd_nand_device* device, uint32_t block);
    int (*check_block)(struct mtd_nand_device* device, uint32_t block);
    int (*mark_badblock)(struct mtd_nand_device* device, uint32_t block);
};

long mtd_devices_nand_register_device(const char* name, struct mtd_nand_device* device);

long mtd_nand_init_partition(const char *mtd_name,struct mtd_nand_partition *parts);

static inline uint32_t mtd_nand_read_id(struct mtd_nand_device* device)
{
    return device->ops->read_id(device);
}

static inline int mtd_nand_read_page(
    struct mtd_nand_device* device,
    int page,
    uint8_t* data, uint32_t data_len,
    uint8_t * spare, uint32_t spare_len)
{
    return device->ops->read_page(device, page, data, data_len, spare, spare_len);
}

static inline int mtd_nand_write_page(
    struct mtd_nand_device* device,
    int page,
    const uint8_t* data, uint32_t data_len,
    const uint8_t * spare, uint32_t spare_len)
{
    return device->ops->write_page(device, page, data, data_len, spare, spare_len);
}

static inline int mtd_nand_move_page(struct mtd_nand_device* device,
                                         int src_page,int dst_page)
{
    return device->ops->move_page(device, src_page, dst_page);
}

static inline int mtd_nand_erase_block(struct mtd_nand_device* device, uint32_t block)
{
    return device->ops->erase_block(device, block);
}

static inline int mtd_nand_check_block(struct mtd_nand_device* device, uint32_t block)
{
    return device->ops->check_block(device, block);
}

static inline int mtd_nand_mark_badblock(struct mtd_nand_device* device, uint32_t block)
{
    return device->ops->mark_badblock(device, block);
}

#endif /* MTD_NAND_H_ */
