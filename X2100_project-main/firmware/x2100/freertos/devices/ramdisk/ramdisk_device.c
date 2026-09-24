/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */
#include <lds_symbol.h>
#include <common.h>
#include "ramdisk_device.h"
#include "ramdisk_partition.h"

#ifdef CONFIG_X2000_CONN_FS
int soc_conn_fs_inited(void);
#endif

static struct ramdisk_block *ramdisk_device_alloc(uint32_t size);
static int ramdisk_device_free(struct ramdisk_block *rdev);

static struct ramdisk_block *ramdisk_dev;

int ramdisk_devices_init(void)
{
#ifdef CONFIG_RAMDISK_DEVICE
    ramdisk_dev = ramdisk_device_alloc(RAMDISK_DEVICE_SIZE);

#ifdef CONFIG_DFS_ELMFAT
    if (ramdisk_dev)
        ramdisk_device_partitions_init(ramdisk_dev);
#endif

#endif

    return 0;
}

/*
 * 相关deinit 函数未测试
 */
int ramdisk_devices_deinit(void)
{
#ifdef CONFIG_RAMDISK_DEVICE
#ifdef CONFIG_DFS_ELMFAT
    if (ramdisk_dev)
        ramdisk_device_partitions_deinit(ramdisk_dev);
#endif
    if (ramdisk_dev)
        ramdisk_device_free(ramdisk_dev);
#endif

    return 0;
}

/*
 * 获取loader程序传递的ramdisk容量大小
 * 返回值: ramdisk的容量大小 单位:字节
 */
static uint32_t ramdisk_device_get_mapped_fs_size(void)
{
#ifdef CONFIG_RAMDISK_DEVICE_MAPPED_PRE_LOAD
    int usable_size = (uint32_t)&_user_heap_end - (uint32_t)&_user_heap_start;
    if (usable_size < _mapped_rtosdata_size) {
        printf("mapped filesystem size(0x%x) is bigger then heap usable size(0x%x)\n", _mapped_rtosdata_size, usable_size);
        printf("ramdisk use malloc default size\n");
        return 0;
    }

    return _mapped_rtosdata_size;
#else
    return 0;
#endif
}


static struct ramdisk_block *ramdisk_device_alloc(uint32_t size)
{
    struct ramdisk_block *rdev = malloc(sizeof(struct ramdisk_block));
    void *ramdisk_device_addr_base;
    uint32_t ramdisk_device_size;

    if (rdev == NULL) {
        printf("ramdisk device alloc struct failed\n");
        goto ram_struct_failed;
    }
    memset(rdev, 0x00, sizeof(struct ramdisk_block));

    if (ramdisk_device_get_mapped_fs_size()) {
        /* 使用loader(SPL/uboot)预加载内存空间 */
        ramdisk_device_size = ramdisk_device_get_mapped_fs_size();
        ramdisk_device_addr_base = (void *)((uint32_t)&_user_heap_end - ramdisk_device_size);
        printf("use loader(SPL) prepare filesytem. ramdisk start address=%p size=%d\n", ramdisk_device_addr_base, ramdisk_device_size);
    } else {
        /* malloc申请空间 */
        ramdisk_device_size = size;
        ramdisk_device_addr_base = malloc(ramdisk_device_size);
        if (ramdisk_device_addr_base == NULL) {
            printf("ramdisk device alloc workspace size(%d) failed.\n", ramdisk_device_size);
            goto ram_size_failed;
        }

        memset(ramdisk_device_addr_base, 0x00, ramdisk_device_size);
    }

    sprintf(rdev->name, "%s", "ramdisk_dev");
    rdev->start_addr = ramdisk_device_addr_base;
    rdev->end_addr   = rdev->start_addr + ramdisk_device_size;
    rdev->size       = ramdisk_device_size;

    return rdev;

ram_size_failed:
    free(rdev);
ram_struct_failed:
    rdev = NULL;
    return NULL;
}

static int ramdisk_device_free(struct ramdisk_block *rdev)
{
    if (rdev == NULL)
        return 0;

    if (ramdisk_device_get_mapped_fs_size()) {
        /* 使用loader(SPL/uboot)预加载内存空间 */
    } else {
        /* malloc申请空间 */
        if (rdev->start_addr)
            free(rdev->start_addr);
    }

    free(rdev);
    rdev = NULL;

    return 0;
}

struct ramdisk_block *ramdisk_devices_info(void)
{
    return ramdisk_dev;
}


uint32_t ramdisk_device_block_read(uint32_t address, uint32_t length, void *buffer)
{
    struct ramdisk_block *rdev = ramdisk_devices_info();
    if (!rdev)
        return -ENODEV;

    if (length == 0)
        return 0;

    if (address + length > (uint32_t)(rdev->end_addr)) {
        printf("%s address=0x%x length=0x%x out of memory(0x%x ~ 0x%x), \n",
                __func__, address, length, (uint32_t)rdev->start_addr, (uint32_t)rdev->end_addr);
        return -EINVAL;
    }

#ifdef CONFIG_X2000_CONN_FS
    if (soc_conn_fs_inited()) {

    } else {
        /* conn_fs 未准备好时,使用ramdisk中的数据 */
        memcpy(buffer, (void *)address, length);
    }
#else
     memcpy(buffer, (void *)address, length);
#endif

    return 0;
}

uint32_t ramdisk_device_block_write(uint32_t address, uint32_t length, void *buffer)
{
    struct ramdisk_block *rdev = ramdisk_devices_info();
    if (!rdev)
        return -ENODEV;

    if (length == 0)
        return 0;

    if (address + length > (uint32_t)(rdev->end_addr)) {
        printf("%s address=0x%x length=0x%x out of memory(0x%x ~ 0x%x), \n",
                __func__, address, length, (uint32_t)rdev->start_addr, (uint32_t)rdev->end_addr);
        return -EINVAL;
    }

#ifdef CONFIG_X2000_CONN_FS
    if (soc_conn_fs_inited()) {

    } else {
        /* conn_fs 未准备好时,使用ramdisk中的数据 */
        memcpy((void *)address, buffer, length);
    }

#else
    memcpy((void *)address, buffer, length);
#endif
    return 0;
}

int ramdisk_device_block_erase(uint32_t address, uint32_t length)
{
    struct ramdisk_block *rdev = ramdisk_devices_info();
    if (!rdev)
        return -ENODEV;

    if (length == 0)
        return 0;

    if (address + length > (uint32_t)(rdev->end_addr)) {
        printf("%s address=0x%x length=0x%x out of memory(0x%x ~ 0x%x), \n",
                __func__, address, length, (uint32_t)rdev->start_addr, (uint32_t)rdev->end_addr);
        return -EINVAL;
    }

#ifdef CONFIG_X2000_CONN_FS
    if (soc_conn_fs_inited()) {

    } else {
        memset((void *)address, 0x00, length);
    }

#else
    memset((void *)address, 0x00, length);
#endif
    return 0;
}

#include <kernel_symbol.h>


EXPORT_SYMBOL(ramdisk_devices_info);
EXPORT_SYMBOL(ramdisk_device_block_read);
EXPORT_SYMBOL(ramdisk_device_block_write);
EXPORT_SYMBOL(ramdisk_device_block_erase);

