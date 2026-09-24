#include <common.h>
#include <os.h>

#include "partition.h"

/*
 * storage devices operation
 */

uint64_t storage_devices_blk_read(struct storage_device* device, uint64_t offset, uint8_t* data, uint64_t length)
{
    assert(device && data);

    if (length == 0)
        return 0;

    uint32_t block_size = device->block_size;

    if (offset % block_size) {
        printf("%s: read offset(%lld) not align block size(%d)\n", __func__, offset, block_size);
        return -EINVAL;
    }

    if (length % block_size) {
        printf("%s: read length(%lld) not align block size(%d)\n", __func__, length, block_size);
        return -EINVAL;
    }

    uint32_t blk_offset = offset / block_size;
    uint32_t blk_count = length / block_size;

    if (blk_offset < device->block_start) {
        printf("%s: offset(%lld) less than block start\n", __func__, offset);
        return -EINVAL;
    }

    if ((blk_offset + blk_count - 1) > device->block_end) {
        printf("%s: offset(%lld) read length(%lld) out of block end\n", __func__, offset, length);
        return -EINVAL;
    }

    return usb_host_mass_storage_read(device->dev, data, blk_offset, blk_count);
}

uint64_t storage_devices_blk_write(struct storage_device* device, uint64_t offset, const uint8_t* data, uint64_t length)
{
    assert(device && data);

    if (length == 0)
        return 0;

    uint32_t block_size = device->block_size;

    if (offset % block_size) {
        printf("%s: write offset(%lld) not align block size(%d)\n", __func__, offset, block_size);
        return -EINVAL;
    }

    if (length % block_size) {
        printf("%s: write length(%lld) not align block size(%d)\n", __func__, length, block_size);
        return -EINVAL;
    }

    uint32_t blk_offset = offset / block_size;
    uint32_t blk_count = length / block_size;

    if (blk_offset < device->block_start) {
        printf("%s: offset(%lld) less than block start\n", __func__, offset);
        return -EINVAL;
    }

    if ((blk_offset + blk_count - 1) > device->block_end) {
        printf("%s: offset(%lld) write length(%lld) out of block end\n", __func__, offset, length);
        return -EINVAL;
    }

    return usb_host_mass_storage_write(device->dev, (uint8_t *)data, blk_offset, blk_count);
}

static int storage_devices_blk_erase(struct storage_device* device, uint64_t offset, uint64_t length)
{
    /* TODO */
    return 0;
}

const static struct storage_driver_ops storage_blk_ops = {
    .read           = storage_devices_blk_read,
    .write          = storage_devices_blk_write,
    .erase_block    = storage_devices_blk_erase,
};

void storage_device_free_partition(struct storage_device *storage_device)
{
    struct storage_device_partition *device_part = storage_device->device_parts;

    if (!device_part)
        return;

    while (device_part->name != NULL)
    {
        free(device_part->name);
        device_part->name = NULL;
        device_part++;
    }

    free(storage_device->device_parts);
    storage_device->device_parts = NULL;
}


struct storage_device_partition *storage_device_parse_partition(const char *udisk_name, struct usb_storage_device *dev)
{
    struct storage_device_partition *udisk_parts;

    assert(udisk_name);
    assert(dev);

    udisk_parts = efi_partition(udisk_name, dev);
    if (udisk_parts) {
        printf("  GPT partition\n");
        return udisk_parts;
    }

    udisk_parts = msdos_partition(udisk_name, dev);
    if (udisk_parts) {
        printf("  MBR partition\n");
        return udisk_parts;
    }

    return NULL;
}


/* used when allocating bus numbers */
#define UDISK_MAXID        32
static unsigned int udsikmap [UDISK_MAXID / (8*sizeof (unsigned int))];
static DEFINE_MUTEX(udisk_lock);

int usb_host_mass_storage_link_bind(struct usb_storage_device *dev)
{
    int ret;
    int udisk_id;
    struct storage_device *storage_device;
    struct storage_device_partition *udisk_parts;

    /* check root file system */
    ret = file_system_root_path_is_valid();
    if (!ret) {
        printf("%s: root path invalid, can not mount filesystem\n", __func__);
        return -ENOTDIR;
    }

    /* get udisk id */
    mutex_lock(&udisk_lock);
    udisk_id = find_next_zero_bit(udsikmap, UDISK_MAXID, 0);
    if (udisk_id < UDISK_MAXID) {
        set_bit (udisk_id, udsikmap);
    } else {
        mutex_unlock(&udisk_lock);
        printf("%s: udisk to many\n", __func__);
        return -E2BIG;
    }
    mutex_unlock(&udisk_lock);

    /* malloc and init storage device */
    storage_device = malloc(sizeof(struct storage_device));
    memset(storage_device, 0x00, sizeof(struct storage_device));
    storage_device->dev = (void *)dev;
    storage_device->id = udisk_id;
    snprintf(storage_device->name, sizeof(storage_device->name), "udisk%d", udisk_id);

    storage_device->ops         = &storage_blk_ops;
    storage_device->block_size  = dev->block_size;
    storage_device->block_start = 0;
    storage_device->block_end   = dev->block_count - 1;

    /* 注册块设备 */
    storage_devices_register_device(storage_device->name, storage_device);

    /* 读取分区信息 */
    udisk_parts = storage_device_parse_partition(storage_device->name, dev);
    if (!udisk_parts) {
        printf("%s: storage_device_parse_partition fail\n", __func__);
        ret = -ENOMEM;
        goto err_parse_part;
    }

    /* 初始化块设备的分区信息 */
    ret = storage_device_init_partition_ops(storage_device, udisk_parts);
    if (ret) {
        printf("%s: storage_device_init_partition_ops fail\n", __func__);
        goto err_init_part;
    }

    /* 挂载文件系统 */
    ret = storage_device_hotplug_partition_register(storage_device);
    if (ret) {
        printf("%s: storage_device_hotplug_partition_register fail\n", __func__);
        goto err_hotplug_part;
    }

    dev->user_data = storage_device;

    return 0;

err_hotplug_part:
    storage_device_deinit_partition_ops(storage_device);
err_init_part:
    storage_device_free_partition(storage_device);
err_parse_part:
    storage_devices_unregister_device(storage_device);
    free(storage_device);
    mutex_lock(&udisk_lock);
    clear_bit(udisk_id, udsikmap);
    mutex_unlock(&udisk_lock);
    return ret;
}

void usb_host_mass_storage_unlink(struct usb_storage_device *dev)
{
    int udisk_id;
    struct storage_device *storage_device;

    assert(dev->user_data);
    storage_device = dev->user_data;
    dev->user_data = NULL; 

    storage_device_hotplug_partition_unregister(storage_device);
    storage_device_deinit_partition_ops(storage_device);
    storage_device_free_partition(storage_device);
    storage_devices_unregister_device(storage_device);
    udisk_id = storage_device->id;
    free(storage_device);
    usb_host_mass_storage_unbind(dev);
    mutex_lock(&udisk_lock);
    clear_bit(udisk_id, udsikmap);
    mutex_unlock(&udisk_lock);
}