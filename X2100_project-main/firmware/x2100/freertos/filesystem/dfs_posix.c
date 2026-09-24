#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>
#include "dfs_private.h"
#include <kernel_symbol.h>
#include <driver/console.h>

/* Standard IO device handles. */
#define STDIN       0
#define STDOUT      1
#define STDERR      2

/* Standard IO device name defines. */
const char __stdin_name[]  = "STDIN";
const char __stdout_name[] = "STDOUT";
const char __stderr_name[] = "STDERR";

/*
 * this function is a POSIX compliant version, which will open a file and
 * return a file descriptor according specified flags.
 *
 * @param file the path name of file.
 * @param flags the file open flags.
 *
 * @return the non-negative integer on successful open, others for failed.
 */
int open(const char *file, int flags, ...)
{
    /* 简化操作:一个POSIX API操作中conn_fs状态不更新 */
    int conn_fs_inited = conn_fs_ops_inited();

    if (conn_fs_inited)
        return conn_fs_ops_posix_open(file, flags, 0644);

    int fd, result;
    struct dfs_fd *d;

    /* Register standard Input Output devices. */
    if (strcmp(file, __stdin_name) == 0)
        return (STDIN);
    if (strcmp(file, __stdout_name) == 0)
        return (STDOUT);
    if (strcmp(file, __stderr_name) == 0)
        return (STDERR);

    /* allocate a fd */
    fd = fd_new();
    if (fd < 0)
    {
        fs_set_errno(-ENOMEM);

        return -1;
    }
    d = fd_get(fd);

    result = dfs_file_open(d, file, flags);
    if (result < 0)
    {
        /* release the ref-count of fd */
        fd_put(d);
        fd_put(d);

        fs_set_errno(result);

        return -1;
    }

    /* release the ref-count of fd */
    fd_put(d);

    /* 记录当前文件信息 */
    if (!conn_fs_inited)
        conn_fs_file_record_ops_posix_open(file, flags, 0644, fd);

    return fd;
}
EXPORT_SYMBOL(open);

/*
 * this function is a POSIX compliant version, which will close the open
 * file descriptor.
 *
 * @param fd the file descriptor.
 *
 * @return 0 on successful, -1 on failed.
 */
int close(int fd)
{
    /* 简化操作:一个POSIX API操作中conn_fs状态不更新 */
    int conn_fs_inited = conn_fs_ops_inited();

    if (conn_fs_inited)
        return conn_fs_ops_posix_close(fd);

    int result;
    struct dfs_fd *d;

    d = fd_get(fd);
    if (d == NULL)
    {
        fs_set_errno(-EBADF);

        return -1;
    }

    result = dfs_file_close(d);
    fd_put(d);

    if (result < 0)
    {
        /* release the ref-count of fd */
        fd_put(d);

        fs_set_errno(result);

        return -1;
    }

    fd_put(d);

    /* 删除文件记录信息 */
    if (!conn_fs_inited)
        conn_fs_file_record_ops_posix_close(fd);

    return 0;
}
EXPORT_SYMBOL(close);

/*
 * this function is a POSIX compliant version, which will read specified data
 * buffer length for an open file descriptor.
 *
 * @param fd the file descriptor.
 * @param buf the buffer to save the read data.
 * @param len the maximal length of data buffer
 *
 * @return the actual read data buffer length. If the returned value is 0, it
 * may be reach the end of file, please check errno.
 */
#if defined(_USING_NEWLIB) && defined(_EXFUN)
_READ_WRITE_RETURN_TYPE _EXFUN(read, (int fd, void *buf, size_t len))
#else
int read(int fd, void *buf, size_t len)
#endif
{
    /* 简化操作:一个POSIX API操作中conn_fs状态不更新 */
    int conn_fs_inited = conn_fs_ops_inited();

    if (conn_fs_inited)
        return conn_fs_ops_posix_read(fd, buf, len);

    int result;
    struct dfs_fd *d;

    if (fd == STDIN)
    {
        char *p = buf;
        char c = console_get_char();
        *p = c;
        return 1;
    }

    if ((fd == STDOUT) || (fd == STDERR))
        return -1;

    /* get the fd */
    d = fd_get(fd);
    if (d == NULL)
    {
        fs_set_errno(-EBADF);

        return -1;
    }

    result = dfs_file_read(d, buf, len);
    if (result < 0)
    {
        fd_put(d);
        fs_set_errno(result);

        return -1;
    }

    off_t offset = d->pos;

    /* release the ref-count of fd */
    fd_put(d);

    /* 记录当前文件偏移 */
    if (!conn_fs_inited)
        conn_fs_file_record_ops_posix_read(fd, offset);

    return result;
}
EXPORT_SYMBOL(read);

/*
 * this function is a POSIX compliant version, which will write specified data
 * buffer length for an open file descriptor.
 *
 * @param fd the file descriptor
 * @param buf the data buffer to be written.
 * @param len the data buffer length.
 *
 * @return the actual written data buffer length.
 */
#if defined(_USING_NEWLIB) && defined(_EXFUN)
_READ_WRITE_RETURN_TYPE _EXFUN(write, (int fd, const void *buf, size_t len))
#else
int write(int fd, const void *buf, size_t len)
#endif
{
    /* 简化操作:一个POSIX API操作中conn_fs状态不更新 */
    int conn_fs_inited = conn_fs_ops_inited();

    if (conn_fs_inited)
        return conn_fs_ops_posix_write(fd, buf, len);

    int result;
    struct dfs_fd *d;

    if ((fd == STDOUT) || (fd == STDERR))
    {
        const char *p = buf;

        size_t save_len = len;
        while (len) {
            console_put_char(*p);
            p++;
            len--;
        }

        return save_len;
    }

    if (fd == STDIN)
        return -1;

    /* get the fd */
    d = fd_get(fd);
    if (d == NULL)
    {
        fs_set_errno(-EBADF);

        return -1;
    }

    result = dfs_file_write(d, buf, len);
    if (result < 0)
    {
        fd_put(d);
        fs_set_errno(result);

        return -1;
    }

    off_t offset = d->pos;

    /* release the ref-count of fd */
    fd_put(d);

    /* 记录当前文件偏移 */
    if (!conn_fs_inited)
        conn_fs_file_record_ops_posix_write(fd, offset);

    return result;
}
EXPORT_SYMBOL(write);

/*
 * this function is a POSIX compliant version, which will seek the offset for
 * an open file descriptor.
 *
 * @param fd the file descriptor.
 * @param offset the offset to be seeked.
 * @param whence the directory of seek.
 *
 * @return the current read/write position in the file, or -1 on failed.
 */
off_t lseek(int fd, off_t offset, int whence)
{
    /* 简化操作:一个POSIX API操作中conn_fs状态不更新 */
    int conn_fs_inited = conn_fs_ops_inited();

    if (conn_fs_inited)
        return conn_fs_ops_posix_lseek(fd, offset, whence);

    int result;
    struct dfs_fd *d;

    d = fd_get(fd);
    if (d == NULL)
    {
        fs_set_errno(-EBADF);

        return -1;
    }

    switch (whence)
    {
    case SEEK_SET:
        break;

    case SEEK_CUR:
        offset += d->pos;
        break;

    case SEEK_END:
        offset += d->size;
        break;

    default:
        fd_put(d);
        fs_set_errno(-EINVAL);

        return -1;
    }

    if (offset < 0)
    {
        fd_put(d);
        fs_set_errno(-EINVAL);

        return -1;
    }
    result = dfs_file_lseek(d, offset);
    if (result < 0)
    {
        fd_put(d);
        fs_set_errno(result);

        return -1;
    }

    /* release the ref-count of fd */
    fd_put(d);

    /* 记录当前文件偏移 */
    if (!conn_fs_inited)
        conn_fs_file_record_ops_posix_lseek(fd, offset);

    return offset;
}
EXPORT_SYMBOL(lseek);


/*
 * this function is a POSIX compliant version, which will rename old file name
 * to new file name.
 *
 * @param old the old file name.
 * @param new the new file name.
 *
 * @return 0 on successful, -1 on failed.
 *
 * note: the old and new file name must be belong to a same file system.
 */
int rename(const char *old, const char *new)
{
    int result;

    result = dfs_file_rename(old, new);
    if (result < 0)
    {
        fs_set_errno(result);

        return -1;
    }

    return 0;
}
EXPORT_SYMBOL(rename);


/*
 * this function is a POSIX compliant version, which will unlink (remove) a
 * specified path file from file system.
 *
 * @param pathname the specified path name to be unlinked.
 *
 * @return 0 on successful, -1 on failed.
 */
int unlink(const char *pathname)
{
    int result;

    result = dfs_file_unlink(pathname);
    if (result < 0)
    {
        fs_set_errno(result);

        return -1;
    }

    return 0;
}
EXPORT_SYMBOL(unlink);

#ifndef _WIN32 /* we can not implement these functions */
/*
 * this function is a POSIX compliant version, which will get file information.
 *
 * @param file the file name
 * @param buf the data buffer to save stat description.
 *
 * @return 0 on successful, -1 on failed.
 */
int stat(const char *file, struct stat *buf)
{
    int result;

    result = dfs_file_stat(file, buf);
    if (result < 0)
    {
        fs_set_errno(result);

        return -1;
    }

    return result;
}
EXPORT_SYMBOL(stat);

/*
 * this function is a POSIX compliant version, which will get file status.
 *
 * @param fildes the file description
 * @param buf the data buffer to save stat description.
 *
 * @return 0 on successful, -1 on failed.
 */
int fstat(int fildes, struct stat *buf)
{
    struct dfs_fd *d;

    /* get the fd */
    d = fd_get(fildes);
    if (d == NULL)
    {
        fs_set_errno(-EBADF);

        return -1;
    }

    /* it's the root directory */
    buf->st_dev = 0;

    buf->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH |
                   S_IWUSR | S_IWGRP | S_IWOTH;
    if (d->type == FT_DIRECTORY)
    {
        buf->st_mode &= ~S_IFREG;
        buf->st_mode |= S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
    }

    buf->st_size    = d->size;
    buf->st_mtime   = 0;

    fd_put(d);

    return EOK;
}
EXPORT_SYMBOL(fstat);

#endif

/*
 * this function is a POSIX compliant version, which shall request that all data
 * for the open file descriptor named by fildes is to be transferred to the storage
 * device associated with the file described by fildes.
 *
 * @param fildes the file description
 *
 * @return 0 on successful completion. Otherwise, -1 shall be returned and errno
 * set to indicate the error.
 */
int fsync(int fildes)
{
    int ret;
    struct dfs_fd *d;

    /* get the fd */
    d = fd_get(fildes);
    if (d == NULL)
    {
        fs_set_errno(-EBADF);
        return -1;
    }

    ret = dfs_file_flush(d);

    fd_put(d);
    return ret;
}
EXPORT_SYMBOL(fsync);

/*
 * this function is a POSIX compliant version, which shall perform a variety of
 * control functions on devices.
 *
 * @param fildes the file description
 * @param cmd the specified command
 * @param data represents the additional information that is needed by this
 * specific device to perform the requested function.
 *
 * @return 0 on successful completion. Otherwise, -1 shall be returned and errno
 * set to indicate the error.
 */
int fcntl(int fildes, int cmd, ...)
{
    int ret = -1;
    struct dfs_fd *d;

    /* get the fd */
    d = fd_get(fildes);
    if (d)
    {
        void *arg;
        va_list ap;

        va_start(ap, cmd);
        arg = va_arg(ap, void *);
        va_end(ap);

        ret = dfs_file_ioctl(d, cmd, arg);
        fd_put(d);
    }
    else ret = -EBADF;

    if (ret < 0)
    {
        fs_set_errno(ret);
        ret = -1;
    }

    return ret;
}
EXPORT_SYMBOL(fcntl);

/*
 * this function is a POSIX compliant version, which shall perform a variety of
 * control functions on devices.
 *
 * @param fildes the file description
 * @param cmd the specified command
 * @param data represents the additional information that is needed by this
 * specific device to perform the requested function.
 *
 * @return 0 on successful completion. Otherwise, -1 shall be returned and errno
 * set to indicate the error.
 */
int ioctl(int fildes, int cmd, ...)
{
    void *arg;
    va_list ap;

    va_start(ap, cmd);
    arg = va_arg(ap, void *);
    va_end(ap);

    /* we use fcntl for this API. */
    return fcntl(fildes, cmd, arg);
}
EXPORT_SYMBOL(ioctl);


/*
 * this function is a POSIX compliant version, which will return the
 * information about a mounted file system.
 *
 * @param path the path which mounted file system.
 * @param buf the buffer to save the returned information.
 *
 * @return 0 on successful, others on failed.
 */
int statfs(const char *path, struct statfs *buf)
{
    int result;

    result = dfs_statfs(path, buf);
    if (result < 0)
    {
        fs_set_errno(result);

        return -1;
    }

    return result;
}
EXPORT_SYMBOL(statfs);


/*
 * this function is a POSIX compliant version, which will make a directory
 *
 * @param path the directory path to be made.
 * @param mode
 *
 * @return 0 on successful, others on failed.
 */
int mkdir(const char *path, mode_t mode)
{
    int fd;
    struct dfs_fd *d;
    int result;

    fd = fd_new();
    if (fd == -1)
    {
        fs_set_errno(-ENOMEM);

        return -1;
    }

    d = fd_get(fd);

    result = dfs_file_open(d, path, O_DIRECTORY | O_CREAT);

    if (result < 0)
    {
        fd_put(d);
        fd_put(d);
        fs_set_errno(result);

        return -1;
    }

    dfs_file_close(d);
    fd_put(d);
    fd_put(d);

    return 0;
}
EXPORT_SYMBOL(mkdir);


/*
 * this function is a POSIX compliant version, which will remove a directory.
 *
 * @param pathname the path name to be removed.
 *
 * @return 0 on successful, others on failed.
 */
int rmdir(const char *pathname)
{
    int result;

    result = dfs_file_unlink(pathname);
    if (result < 0)
    {
        fs_set_errno(result);

        return -1;
    }

    return 0;
}
EXPORT_SYMBOL(rmdir);


/*
 * this function is a POSIX compliant version, which will open a directory.
 *
 * @param name the path name to be open.
 *
 * @return the DIR pointer of directory, NULL on open directory failed.
 */
DIR *opendir(const char *name)
{
    struct dfs_fd *d;
    int fd, result;
    DIR *t;

    t = NULL;

    /* allocate a fd */
    fd = fd_new();
    if (fd == -1)
    {
        fs_set_errno(-ENOMEM);

        return NULL;
    }
    d = fd_get(fd);

    result = dfs_file_open(d, name, O_RDONLY | O_DIRECTORY);
    if (result >= 0)
    {
        /* open successfully */
        t = (DIR *) malloc(sizeof(DIR));
        if (t == NULL)
        {
            dfs_file_close(d);
            fd_put(d);
        }
        else
        {
            memset(t, 0, sizeof(DIR));

            t->fd = fd;
        }
        fd_put(d);

        return t;
    }

    /* open failed */
    fd_put(d);
    fd_put(d);
    fs_set_errno(result);

    return NULL;
}
EXPORT_SYMBOL(opendir);


/*
 * this function is a POSIX compliant version, which will return a pointer
 * to a dirent structure representing the next directory entry in the
 * directory stream.
 *
 * @param d the directory stream pointer.
 *
 * @return the next directory entry, NULL on the end of directory or failed.
 */
struct dirent *readdir(DIR *d)
{
    int result;
    struct dfs_fd *fd;

    fd = fd_get(d->fd);
    if (fd == NULL)
    {
        fs_set_errno(-EBADF);
        return NULL;
    }

    if (d->num)
    {
        struct dirent *dirent_ptr;
        dirent_ptr = (struct dirent *)&d->buf[d->cur];
        d->cur += dirent_ptr->d_reclen;
    }

    if (!d->num || d->cur >= d->num)
    {
        /* get a new entry */
        result = dfs_file_getdents(fd,
                                   (struct dirent *)d->buf,
                                   sizeof(d->buf) - 1);
        if (result <= 0)
        {
            fd_put(fd);
            fs_set_errno(result);

            return NULL;
        }

        d->num = result;
        d->cur = 0; /* current entry index */
    }

    fd_put(fd);

    return (struct dirent *)(d->buf + d->cur);
}
EXPORT_SYMBOL(readdir);


/*
 * this function is a POSIX compliant version, which will return current
 * location in directory stream.
 *
 * @param d the directory stream pointer.
 *
 * @return the current location in directory stream.
 */
long telldir(DIR *d)
{
    struct dfs_fd *fd;
    long result;

    fd = fd_get(d->fd);
    if (fd == NULL)
    {
        fs_set_errno(-EBADF);

        return 0;
    }

    result = fd->pos - d->num + d->cur;
    fd_put(fd);

    return result;
}
EXPORT_SYMBOL(telldir);


/*
 * this function is a POSIX compliant version, which will set position of
 * next directory structure in the directory stream.
 *
 * @param d the directory stream.
 * @param offset the offset in directory stream.
 */
void seekdir(DIR *d, off_t offset)
{
    struct dfs_fd *fd;

    fd = fd_get(d->fd);
    if (fd == NULL)
    {
        fs_set_errno(-EBADF);

        return ;
    }

    /* seek to the offset position of directory */
    if (dfs_file_lseek(fd, offset) >= 0)
        d->num = d->cur = 0;
    fd_put(fd);
}
EXPORT_SYMBOL(seekdir);


/*
 * this function is a POSIX compliant version, which will reset directory
 * stream.
 *
 * @param d the directory stream.
 */
void rewinddir(DIR *d)
{
    struct dfs_fd *fd;

    fd = fd_get(d->fd);
    if (fd == NULL)
    {
        fs_set_errno(-EBADF);

        return ;
    }

    /* seek to the beginning of directory */
    if (dfs_file_lseek(fd, 0) >= 0)
        d->num = d->cur = 0;
    fd_put(fd);
}
EXPORT_SYMBOL(rewinddir);


/*
 * this function is a POSIX compliant version, which will close a directory
 * stream.
 *
 * @param d the directory stream.
 *
 * @return 0 on successful, -1 on failed.
 */
int closedir(DIR *d)
{
    int result;
    struct dfs_fd *fd;

    fd = fd_get(d->fd);
    if (fd == NULL)
    {
        fs_set_errno(-EBADF);

        return -1;
    }

    result = dfs_file_close(fd);
    fd_put(fd);

    fd_put(fd);
    free(d);

    if (result < 0)
    {
        fs_set_errno(result);

        return -1;
    }
    else
        return 0;
}
EXPORT_SYMBOL(closedir);


#ifdef DFS_USING_WORKDIR
/*
 * this function is a POSIX compliant version, which will change working
 * directory.
 *
 * @param path the path name to be changed to.
 *
 * @return 0 on successful, -1 on failed.
 */
int chdir(const char *path)
{
    char *fullpath;
    DIR *d;

    if (path == NULL)
    {
        dfs_lock();
        elm_printf("%s\n", working_directory);
        dfs_unlock();

        return 0;
    }

    if (strlen(path) > DFS_PATH_MAX)
    {
        fs_set_errno(-ENOTDIR);

        return -1;
    }

    fullpath = dfs_normalize_path(NULL, path);
    if (fullpath == NULL)
    {
        fs_set_errno(-ENOTDIR);

        return -1; /* build path failed */
    }

    d = opendir(fullpath);
    if (d == NULL)
    {
        /* this is a not exist directory */
        free(fullpath);

        return -1;
    }

    /* close directory stream */
    closedir(d);

    dfs_lock();

    /* copy full path to working directory */
    strncpy(working_directory, fullpath, DFS_PATH_MAX);
    /* release normalize directory path name */
    free(fullpath);

    dfs_unlock();

    return 0;
}
EXPORT_SYMBOL(chdir);

#endif

/*
 * this function is a POSIX compliant version, which shall check the file named
 * by the pathname pointed to by the path argument for accessibility according
 * to the bit pattern contained in amode.
 *
 * @param path the specified file/dir path.
 * @param amode the value is either the bitwise-inclusive OR of the access
 * permissions to be checked (R_OK, W_OK, X_OK) or the existence test (F_OK).
 */
int access(const char *path, int amode)
{
    struct stat sb;
    if (stat(path, &sb) < 0)
        return -1; /* already sets errno */

    /* ignore R_OK,W_OK,X_OK condition */
    return 0;
}
EXPORT_SYMBOL(access);

/*
 * this function is a POSIX compliant version, which will return current
 * working directory.
 *
 * @param buf the returned current directory.
 * @param size the buffer size.
 *
 * @return the returned current directory.
 */
char *getcwd(char *buf, size_t size)
{
#ifdef DFS_USING_WORKDIR
    dfs_lock();
    strncpy(buf, working_directory, size);
    dfs_unlock();
#else
    printf(NO_WORKING_DIR);
#endif

    return buf;
}
EXPORT_SYMBOL(getcwd);

int isatty(int fd)
{
    if((STDIN <= fd) && (fd <= STDERR))
        return 1;

    errno = EINVAL;
    return 0;
}
EXPORT_SYMBOL(isatty);
