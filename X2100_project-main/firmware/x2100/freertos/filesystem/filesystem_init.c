#ifdef CONFIG_DFS

#include <stdlib.h>
#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_posix.h>
#include "filesystem_init.h"

#include "filesystem_partition.c"

extern int dfs_init(void);
extern int mount_fs(const char *dir_name, const char *device_name, const char *fs_type, enum format_filesystem_type format);
extern int ramdisk_device_init_partition(char *device_name, void *start_address, uint32_t size);


int file_system_create_device_mount_point(const char *dir_name)
{
    DIR *dir;
    int ret;
    int try_count = 0;

    while (1) {
        dir = opendir(dir_name);
        if (dir == NULL) {
            goto mkdir_and_try_again;
        }

        closedir(dir);

        return 0;

mkdir_and_try_again:
        if (try_count++ == 1) {
            printf("open_dir %s errorno: %d\n", dir_name, fs_get_errno());
            return -1;
        }

        ret = mkdir(dir_name, 0666);
        if (ret) {
            printf("create dir %s error: %d errono: %d\n", dir_name, ret, fs_get_errno());
            return -1;
        }
    }

    return 0;
}

int file_system_destroy_device_mount_point(const char *dir_name)
{
    rmdir(dir_name);

    return 0;
}

static int file_system_mount_partition_resource(const char *name, const char *fs_type)
{
    assert(name);

    const char *part_name = name;

    char mount_dir[128];
    memset(mount_dir, 0x00, sizeof(mount_dir));
    sprintf(mount_dir, "/%s", part_name);

    int ret = file_system_create_device_mount_point(mount_dir);
    if (ret < 0) {
        printf("create RAMDISK mount point(%s) failed\n", mount_dir);
        return ret;
    }

    ret = mount_fs(mount_dir, part_name, fs_type, FORMAT_FILESYSTEM_TYPE_FAT);
    if (ret < 0) {
        printf("mount file system [%s] on dir [%s]failed\n", part_name, mount_dir);
        file_system_destroy_device_mount_point(mount_dir);
        return ret;
    }

    return 0;
}

static inline void file_system_mount_fixed_partitions(const char *fs_type)
{
    int i;
    struct auto_mount_partition *parts = NULL;
    int parts_num = 0;

    if (strcmp(fs_type, "elm") == 0) {
        parts = parts_elm;
        parts_num = sizeof(parts_elm) / sizeof(parts_elm[0]);
    } else if (strcmp(fs_type, "uffs") == 0) {
        parts = parts_uffs;
        parts_num = sizeof(parts_uffs) / sizeof(parts_uffs[0]);
    } else if (strcmp(fs_type, "yaffs") == 0) {
        parts = parts_yaffs;
        parts_num = sizeof(parts_yaffs) / sizeof(parts_yaffs[0]);
    } else {
        /* 未知的文件系统类型，直接返回 */
        return;
    }

    for (i = 0; i < parts_num; i++) {
        if (!parts[i].part_name)
            continue;

        printf("[FS_INIT] Trying to mount partition: %s with fs_type: %s\n", parts[i].part_name, fs_type);

        /* 查找是否存在名称匹配设备*/
        device_t dev = device_find(parts[i].part_name);
        if (!dev) {
            printf("[FS_INIT] Error: Device '%s' NOT FOUND in device manager!\n", parts[i].part_name);
            continue;
        }

        printf("[FS_INIT] Device '%s' found. Creating mount point...\n", parts[i].part_name);

        /* 创建挂载点,挂载设备分区 */
        file_system_mount_partition_resource(parts[i].part_name, fs_type);
    }
}

void file_system_init(void)
{
    dfs_init();

#ifdef CONFIG_DFS_ELMFAT
    /* Flash */
    int ret_fat;

    /* 根文件系统挂载 */
#ifdef CONFIG_RAMDISK_DEVICE
    /* 将ramdisk挂载为根目录, 其他分区在该目录下创建相应的目录并挂载 */
    char *root_dir = "/";
    char *root_part = "ramdisk_ingenic_focre_rot0_ponit";
    ret_fat = mount_fs(root_dir, root_part, "elm", FORMAT_FILESYSTEM_TYPE_FAT);
    if (ret_fat < 0) {
        printf("mount file system (%s) failed\n", root_part);
        return;
    } else {
        //printf("mount file system(elm) on Device(%s) OK\n", root_dir);
    }

    file_system_mount_fixed_partitions("elm");
#else

    /* 兼容旧配置 查找挂载默认分区rootfs */
    char *root_dir = "/";
    char *root_part = "rootfs";
    ret_fat = mount_fs(root_dir, root_part, "elm", FORMAT_FILESYSTEM_TYPE_FAT);
    if (ret_fat < 0) {
        printf("mount file system (%s) failed\n", root_part);
        return;
    } else {
        //printf("mount file system(elm) on Device(%s) OK\n", root_dir);
    }
#endif


#ifdef CONFIG_DFS_DEBUGFS
    /* 文件系统类型挂载为: debugfs 非elm， 故在此主动挂载不由monitor 挂载 */

    /* RAM Device */
    int ret_debugfs = -1;
    void *ram_start_address = malloc(CONFIG_RAM_DEVICE_SIZE);
    assert(ram_start_address != NULL);
    ramdisk_device_init_partition(CONFIG_RAM_DEVICE_NAME, ram_start_address, CONFIG_RAM_DEVICE_SIZE);

    ret_debugfs = file_system_create_device_mount_point(CONFIG_RAM_DEVICE_DIR);
    if (ret_debugfs < 0) {
        printf("create RAMDISK mount point(%s) failed\n", CONFIG_RAM_DEVICE_DIR);
        return ;
    }

    ret_debugfs = mount_fs(CONFIG_RAM_DEVICE_DIR, CONFIG_RAM_DEVICE_NAME, "debugfs", FORMAT_FILESYSTEM_TYPE_FAT);
    if (ret_debugfs < 0) {
        printf("mount filesystem \"%s\" failed\n", CONFIG_RAM_DEVICE_DIR);
    } else {
        //printf("mount RAMDISK on \"%s\" OK\n", CONFIG_RAM_DEVICE_DIR);
    }
#endif

#endif

#ifdef CONFIG_DFS_UFFS

#if defined(CONFIG_RAMDISK_DEVICE) && defined (CONFIG_DFS_ELMFAT)

    file_system_mount_fixed_partitions("uffs");
#else
    int ret_uffs;
    /* 兼容旧配置 查找挂载默认分区rootfs */
    char *root_dir = "/";
    char *root_part = "rootfs";
    ret_uffs = mount_fs(root_dir, root_part, "uffs", FORMAT_FILESYSTEM_TYPE_FAT);
    if (ret_uffs < 0) {
        printf("mount file system (%s) failed\n", root_part);
        return;
    } else {
        //printf("mount file system(elm) on Device(%s) OK\n", root_dir);
    }
#endif

#endif

#ifdef CONFIG_DFS_YAFFS
#if defined(CONFIG_RAMDISK_DEVICE) && defined (CONFIG_DFS_ELMFAT)
    file_system_mount_fixed_partitions("yaffs");
#else
    int ret_yaffs;
    /* 兼容旧配置 查找挂载默认分区rootfs */
    char *root_dir = "/";
    char *root_part = "rootfs";
    ret_yaffs = mount_fs(root_dir, root_part, "yaffs", FORMAT_FILESYSTEM_TYPE_FAT);
    if (ret_yaffs < 0) {
        printf("mount file system (%s) failed\n", root_part);
        return;
    } else {
        // printf("mount file system(elm) on Device(%s) OK\n", root_dir);
    }
#endif
#endif
}
#endif
