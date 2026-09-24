#include <os.h>
#include <list.h>
#include <string.h>
#include <common.h>
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>
#include "dfs_device.h"
#include "filesystem_hotplug.h"

/*
 * 创建设备挂载点/挂载对应分区
 */


/* 外部声明 */
extern int mount_fs(const char *dir_name, const char *device_name, const char *fs_type, enum format_filesystem_type format);

extern int file_system_create_device_mount_point(const char *dir_name);
extern int file_system_destroy_device_mount_point(const char *dir_name);

static int file_system_do_mounting(const char *dir, const char *partition_name)
{
    int ret = file_system_create_device_mount_point(dir);
    if (ret < 0) {
        printf("create RAMDISK mount point(%s) failed\n", dir);
        return ret;
    }

    ret = mount_fs(dir, partition_name, "elm", FORMAT_FILESYSTEM_TYPE_NO_FORMAT);
    if (ret < 0) {
        printf("mount file system [%s] failed. partition type not FAT/FAT32/exFAT unspport\n", partition_name);
        file_system_destroy_device_mount_point(dir);
        return ret;
    }

    return 0;
}

static int file_system_do_unmounting(const char *dir, const char *partition_name)
{
    dfs_unmount(dir);

    file_system_destroy_device_mount_point(dir);

    return 0;
}

/*
 * 检查跟节点是否挂载成功
 * return  =1: 挂载/创建成功,可以读写操作
 *         =0: 挂载/创建失败
 */
int file_system_root_path_is_valid(void)
{
    DIR *dir;
    const char *dir_name = "/";

    dir = opendir(dir_name);
    if (dir == NULL) {
        return 0;
    }

    closedir(dir);

    return 1;
}

int file_system_insert_partition(const char *name)
{
    assert(name);

    const char *part_name = name;

    char mount_dir[128];
    memset(mount_dir, 0x00, sizeof(mount_dir));
    sprintf(mount_dir, "/%s", part_name);

    file_system_do_mounting(mount_dir, part_name);

    return 0;
}

int file_system_remove_partition(const char *name)
{
    assert(name);

    const char *part_name = name;

    char mount_dir[128];
    memset(mount_dir, 0x00, sizeof(mount_dir));
    sprintf(mount_dir, "/%s", part_name);

    file_system_do_unmounting(mount_dir, part_name);

    return 0;
}

