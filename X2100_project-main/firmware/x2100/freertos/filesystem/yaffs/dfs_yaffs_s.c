#include <pthread.h>
#include <assert.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <mtd_driver_nand.h>

#include "yaffs/yaffs_guts.h"
#include "yaffs/direct/yaffsfs.h"
#include "yaffs/yaffs_packedtags2.h"
#include "yaffs/direct/yaffs_flashif.h"

/* make sure the following struct var had been initilased to 0! */


static int dfs_yfile_open(struct dfs_fd *file)
{
    struct dfs_filesystem *fs;
    struct yaffs_obj *obj;
    int fd;
    int oflag;
    int result;

    fs = (struct dfs_filesystem *)file->data;
    obj = (struct yaffs_obj *)fs->data;

    oflag = file->flags;
    if (oflag & O_DIRECTORY)
    {
        yaffs_DIR * dir;
        if (oflag & O_CREAT)
        {
            result = yaffs_mkdir_reldir(obj, file->path, 0x777);
            if (result < 0)
                return yaffsfs_GetLastError();
        }
        /* open dir */
        dir = yaffs_opendir_reldir(obj, file->path);
        if (dir == NULL)
            return yaffsfs_GetLastError();
        /* save this pointer,will used by dfs_yaffs_getdents*/
        file->data = dir;
        return 0;
    }

    /* regular file operations */
    fd = yaffs_open_reldir(obj, file->path, oflag | O_CREAT, (S_IREAD | S_IWRITE));
    if (fd < 0)
        return yaffsfs_GetLastError();

    file->data = (void *)fd;
    file->pos = yaffs_lseek(fd, 0, SEEK_CUR);
    file->size = yaffs_lseek(fd, 0, SEEK_END);
    yaffs_lseek(fd, file->pos, SEEK_SET);

    if (oflag & O_APPEND)
    {
        file->pos = file->size;
        file->size = yaffs_lseek(fd, 0, SEEK_END);
    }

    return 0;
}

static int dfs_yfile_close(struct dfs_fd *file)
{
    int oflag;
    int fd;

    oflag = file->flags;
    if (oflag & O_DIRECTORY) /* operations about dir */
    {
        if (yaffs_closedir((yaffs_DIR *)(file->data)) < 0)
            return yaffsfs_GetLastError();
        return 0;
    }

    /* regular file operations */
    fd = (int)(file->data);

    if (yaffs_close(fd) == 0)
        return 0;

    /* release memory */
    return yaffsfs_GetLastError();
}

static int dfs_yfile_ioctl(struct dfs_fd *file, int cmd, void *args)
{
    return -ENOSYS;
}

static int dfs_yfile_read(struct dfs_fd *file, void *buf, size_t len)
{
    int fd;
    int char_read;


    fd = (int)(file->data);
    char_read = yaffs_read(fd, buf, len);
    if (char_read < 0)
        return yaffsfs_GetLastError();

    /* update position */
    file->pos = yaffs_lseek(fd, 0, SEEK_CUR);

    return char_read;
}

static int dfs_yfile_write(struct dfs_fd *file, const void *buf, size_t len)
{
    int fd;
    int char_write;

    fd = (int)(file->data);

    char_write = yaffs_write(fd, buf, len);
    if (char_write < 0)
        return yaffsfs_GetLastError();

    /* update position */
    file->pos = yaffs_lseek(fd, 0, SEEK_CUR);

    return char_write;
}

static int dfs_yfile_flush(struct dfs_fd *file)
{
    int fd;
    int result;

    fd = (int)(file->data);

    result = yaffs_flush(fd);
    if (result < 0)
        return yaffsfs_GetLastError();

    return 0;
}

int yaffs_seekdir(yaffs_DIR *dir, long offset)
{
    int i = 0;

    while(i < offset)
    {
        if (yaffs_readdir(dir) == NULL)
            return -1;
        i++;
    }
    return 0;
}

static int dfs_yfile_lseek(struct dfs_fd *file, off_t offset)
{
    int fd;
    int result = -1;

    if (file->type == FT_DIRECTORY) {
        yaffs_rewinddir((yaffs_DIR *)(file->data));
        result = yaffs_seekdir((yaffs_DIR *)(file->data), offset/sizeof(struct dirent));
        if (result >= 0) {
            file->pos = offset;
            return offset;
        }
    } else if (file->type == FT_REGULAR) {
        fd = (int)(file->data);
        /* set offset as current offset */
        result = yaffs_lseek(fd, offset, SEEK_SET);
        if (result < 0)
            return yaffsfs_GetLastError();
        return result;
    }
    return result;
}

static int dfs_yfile_getdents(struct dfs_fd *file, struct dirent *dirp, uint32_t count)
{
    uint32_t index;
    struct dirent* d;
    yaffs_DIR* dir;
    struct yaffs_dirent * yaffs_d;

    dir = (yaffs_DIR*)(file->data);
    assert(dir != NULL);

    /* make integer count, usually count is 1 */
    count = (count / sizeof(struct dirent)) * sizeof(struct dirent);
    if (count == 0)
        return -EINVAL;

    index = 0;
    /* usually, the while loop should only be looped only once! */
    while (1)
    {
        d = dirp + index;

        yaffs_d = yaffs_readdir(dir);
        if (yaffs_d == NULL)
        {
            if (yaffsfs_GetLastError() == EBADF)
                return -EBADF;

            return -1; /* a general error */
        }

        /* write the rest feilds of struct dirent* dirp  */
        d->d_namlen = strlen(yaffs_d->d_name);
        d->d_reclen = (uint16_t)sizeof(struct dirent);
        strncpy(d->d_name, yaffs_d->d_name, strlen(yaffs_d->d_name) + 1);

        index++;
        if (index * sizeof(struct dirent) >= count)
            break;
    }

    if (index == 0)
        return yaffsfs_GetLastError();

    return index * sizeof(struct dirent);
}


static int dfs_yaffs_mount(struct dfs_filesystem *fs, unsigned long rwflag, const void *data)
{
    int ret;
    yaffs_start_up(fs);
    ret = yaffs_mount(fs->path);
    return ret;
}

static int dfs_yaffs_unmount(struct dfs_filesystem *fs)
{
    int ret = 0;
    if (yaffs_unmount(fs->path) < 0)
        return yaffsfs_GetLastError();

    return ret;
}

static int dfs_yaffs_mkfs(device_t dev_id, int cluster_size)
{
    int ret = 0;
    if (dev_id->type != Device_Class_MTD)
        return -1;

    /**
     * umount_flag: yaffs_format()将在格式化前卸载设备
     * force_umount_flag:允许在设备繁忙时（如有文件打开）强制卸载
     * remount_flag:在格式化完成后会重新挂载设备（如果之前是挂载状态）
     */
    ret = yaffs_format("/", 1, 1, 1);
    return ret;
}

static int dfs_yaffs_statfs(struct dfs_filesystem *fs, struct statfs *buf)
{
    struct mtd_nand_device *mtd = (struct mtd_nand_device *)MTD_NAND_DEVICE(fs->dev_id);
    assert(mtd != NULL);
    struct yaffs_dev *dev;

    dev = yaffs_getdev(fs->path);

    buf->f_bsize = mtd->page_size;
    buf->f_blocks = mtd->block_end -mtd->block_start;
    buf->f_bfree = yaffs_freespace_reldev(dev) / mtd->page_size;

    return 0;
}

static int dfs_yaffs_unlink(struct dfs_filesystem *fs, const char *path)
{
    int result;
    struct yaffs_stat s;

    /* judge file type, dir is to be delete by yaffs_rmdir, others by yaffs_unlink */
    if (yaffs_lstat_reldir(fs->data, path, &s) < 0)
    {
        return yaffsfs_GetLastError();
    }

    switch (s.st_mode & S_IFMT)
    {
    case S_IFREG:
        result = yaffs_unlink_reldir(fs->data, path);
        break;
    case S_IFDIR:
        result = yaffs_rmdir_reldir(fs->data, path);
        break;
    default:
        /* unknown file type */
        return -1;
    }
    if (result < 0)
        return yaffsfs_GetLastError();

    return 0;
}

static int dfs_yaffs_stat(struct dfs_filesystem *fs, const char *path, struct stat *st)
{
    int result;
    struct yaffs_stat s;

    result = yaffs_stat_reldir(fs->data, path, &s);
    if (result < 0)
        return yaffsfs_GetLastError();

    /* convert to dfs stat structure */
    st->st_dev = 0;
    st->st_mode = s.st_mode;
    st->st_size = s.st_size;
    st->st_mtime = s.yst_mtime;

    return 0;
}

static int dfs_yaffs_rename(struct dfs_filesystem *fs, const char *oldpath, const char *newpath)
{
    int result;

    result = yaffs_rename_reldir(fs->data, oldpath, newpath);

    if (result < 0)
        return yaffsfs_GetLastError();

    return 0;
}

static const struct dfs_file_ops _fops =
{
    dfs_yfile_open,
    dfs_yfile_close,
    dfs_yfile_ioctl,
    dfs_yfile_read,
    dfs_yfile_write,
    dfs_yfile_flush,
    dfs_yfile_lseek,
    dfs_yfile_getdents,
    NULL, /* poll interface */
};

static const struct dfs_filesystem_ops dfs_yaffs_ops =
{
    "yaffs",
    DFS_FS_FLAG_FULLPATH,
    &_fops,

    dfs_yaffs_mount,
    dfs_yaffs_unmount,
    dfs_yaffs_mkfs,
    dfs_yaffs_statfs,

    dfs_yaffs_unlink,
    dfs_yaffs_stat,
    dfs_yaffs_rename,
};

int yaffs_start_up(struct dfs_filesystem* fs)
{
    yaffsfs_OSInitialisation();                                 // 初始化Lock
    struct yaffs_dev *yaffs_device;
    struct mtd_nand_device *yaffs_mtd_nand;

	yaffs_mtd_nand = MTD_NAND_DEVICE(fs->dev_id);  // 获取flash设备参数

    yaffs_device = malloc(sizeof(*yaffs_device));
    memset(yaffs_device, 0, sizeof(*yaffs_device));

    /* 初始化yaffs设备结构体 */
    yaffs_device->param.name = fs->path;

    /* tags是否存储在数据区, 适用于oob区过小的flash
     * yaffs1必须置0(不支持inband_tags) */
    yaffs_device->param.inband_tags = 1;                                     // 必须置1,不支持oob区的读写
    yaffs_device->param.is_yaffs2 = 1;                                       // yaffs版本
    yaffs_device->param.n_caches = 50;                                       // 缓存块数量,缓存chunk数据
    yaffs_device->param.start_block = yaffs_mtd_nand->block_start;           // 起始块
    yaffs_device->param.end_block = yaffs_mtd_nand->block_end;               // 结尾块
    yaffs_device->param.total_bytes_per_chunk = yaffs_mtd_nand->page_size;   // 页大小
    yaffs_device->param.spare_bytes_per_chunk = yaffs_mtd_nand->oob_size;    // oob区大小
    yaffs_device->param.chunks_per_block = yaffs_mtd_nand->pages_per_block;  // 每块包含几个页
    yaffs_device->param.use_nand_ecc = 1;                                    // 使用nand硬件ecc纠错码
    yaffs_device->param.refresh_period = 1000;                               // 每写refresh_period次,检查一次坏块
    yaffs_device->param.no_tags_ecc = 0;                                     // 是否使用ecc单独校验tags数据
    yaffs_device->param.empty_lost_n_found = 1;                              // 挂载时是否清空目录
    yaffs_device->param.n_reserved_blocks = 5;                               // 保留块
    yaffs_device->param.enable_xattr = 1;                                     // 启用扩展文件属性
    yaffs_device->param.hide_lost_n_found = 1;                               // 是否隐藏lost+found目录
    yaffs_device->param.always_check_erased = 1;                             // 写操作前是否检查块是否被擦除
    yaffs_device->driver_context = yaffs_mtd_nand;                           // 关联mtd驱动

    yaffs_mtd_drv_install(yaffs_device);                       // 获取flash读写操作接口
    yaffs_add_device(yaffs_device);                            // 向yaffs设备列表添加此设备

    return 0;
}




int dfs_yaffs_init(void)
{
    return dfs_register(&dfs_yaffs_ops);
}
// INIT_COMPONENT_EXPORT(dfs_yaffs_init);
