#include <common.h>
#include <os.h>
#include <stdio.h>
#include <errno.h>
#include <driver/cache.h>
#include <driver/mmc_device.h>
#include <block_device_driver.h>
#include <usb/gadget_mass_storage.h>
#include <filesystem/filesystem_hotplug.h>

static const struct gadget_id usb_id = {
    .vendor_id  = 0x0525,
    .product_id = 0xa4a5,
};

static char *partition_name = "mmcblk0p0";
static uint64_t g_part_offset = 0;
static uint64_t g_part_size = 0;
static uint32_t g_block_size = 0;

static int card_storage_read_callback(void *buf, int count, u64 pos);
static int card_storage_write_callback(const void *buf, int count, u64 pos);

static struct fsg_lun_config card_storage_lun_config = {
    .read_callback  = card_storage_read_callback,
    .write_callback = card_storage_write_callback,

    .ro         = 0,
    .removable  = 1,
    .cdrom      = 0,

    .num_sectors = 0,
    .block_size  = 0,
};

static void card_storage_connect_callback(int connect)
{
    printf("fsg: card storage connect=%d\n", connect);

#ifdef CONFIG_DFS_HOTPLUG
    if (!file_system_root_path_is_valid())
        return;

    if (connect) {
        file_system_remove_partition(partition_name);
    } else {
        file_system_insert_partition(partition_name);
    }
#endif
}

static struct fsg_config card_storage_config = {
    .nluns      = 1,
    .luns       = &card_storage_lun_config,
    .connect_cb = card_storage_connect_callback,
};

static int card_storage_parse_partition(void)
{
    struct block_device_partition *blk = (struct block_device_partition *)
                                        device_find(partition_name);
    if (!blk)
        return -ENODEV;

    g_block_size = blk->sector_size;
    g_part_offset = blk->offset;
    g_part_size = blk->size - (blk->size % g_block_size);

    if (g_block_size <= 0 || g_part_size < g_block_size)
        return -EINVAL;

    card_storage_lun_config.block_size = g_block_size;
    card_storage_lun_config.num_sectors = g_part_size / g_block_size;

    return 0;
}

static int card_storage_read_callback(void *buf, int count, u64 pos)
{
    int ret;
    uint64_t addr;

    if (!g_block_size)
        return -ENODEV;

    if ((pos + count) > g_part_size) {
        printf("card_fsg: read pos=%llu, count=%d overflow!\n", pos, count);
        return -EOVERFLOW;
    }

    addr = g_part_offset + pos;

    if ((addr % g_block_size) || (count % g_block_size)) {
        printf("card_fsg: read pos=%llu, count=%d not align!\n", pos, count);
        return -EINVAL;
    }

    ret = mmc_device_block_read(addr, count, buf);
    if (ret < 0)
        return ret;

    return count;
}

static int card_storage_write_callback(const void *buf, int count, u64 pos)
{
    int ret;
    uint64_t addr;

    if (!g_block_size)
        return -ENODEV;

    if ((pos + count) > g_part_size) {
        printf("card_fsg: write pos=%llu, count=%d overflow!\n", pos, count);
        return -EOVERFLOW;
    }

    addr = g_part_offset + pos;

    if ((addr % g_block_size) || (count % g_block_size)) {
        printf("card_fsg: write pos=%llu, count=%d not align!\n", pos, count);
        return -EINVAL;
    }

    ret = mmc_device_block_write(addr, count, buf);
    if (ret < 0)
        return ret;

    return count;
}

int gadget_usb_card_storage_test(void)
{
    int ret;

    ret = card_storage_parse_partition();
    if (ret)
        return ret;

    return gadget_mass_storage_init(&usb_id, &card_storage_config);
}
