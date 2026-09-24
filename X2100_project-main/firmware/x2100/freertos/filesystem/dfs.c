#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include "dfs_private.h"

extern int elm_init(void);
extern int dfs_uffs_init(void);

#ifdef CONFIG_DFS_DEBUGFS
extern int dfs_debugfs_init(void);
#endif

#ifdef CONFIG_DFS_YAFFS
extern int dfs_yaffs_init(void);
#endif

/* Global variables */
const struct dfs_filesystem_ops *filesystem_operation_table[DFS_FILESYSTEM_TYPES_MAX];
struct dfs_filesystem filesystem_table[DFS_FILESYSTEMS_MAX];

/* device filesystem lock */
static struct mutex fslock;

#ifdef DFS_USING_WORKDIR
char working_directory[DFS_PATH_MAX] = {"/"};
#endif

static struct dfs_fdtable _fdtab;
static int  fd_alloc(struct dfs_fdtable *fdt, int startfd);

/*
 * this function will initialize device file system.
 */
int dfs_init(void)
{
    static bool init_ok = FALSE;

    if (init_ok)
    {
        printf("dfs already init.\n");
        return 0;
    }

    /* clear filesystem operations table */
    memset((void *)filesystem_operation_table, 0, sizeof(filesystem_operation_table));
    /* clear filesystem table */
    memset(filesystem_table, 0, sizeof(filesystem_table));
    /* clean fd table */
    memset(&_fdtab, 0, sizeof(_fdtab));

    /* create device filesystem lock */
    mutex_init(&fslock);

#ifdef DFS_USING_WORKDIR
    /* set current working directory */
    memset(working_directory, 0, sizeof(working_directory));
    working_directory[0] = '/';
#endif

#ifdef CONFIG_DFS_ELMFAT
    elm_init();
#endif

#ifdef CONFIG_DFS_DEBUGFS
    dfs_debugfs_init();
#endif

#ifdef CONFIG_DFS_UFFS
    dfs_uffs_init();
#endif

#ifdef CONFIG_DFS_YAFFS
    dfs_yaffs_init();
#endif

    init_ok = TRUE;

    return 0;
}


/*
 * this function will lock device file system.
 *
 * @note please don't invoke it on ISR.
 */
void dfs_lock(void)
{
    mutex_lock(&fslock);
}

/*
 * this function will lock device file system.
 *
 * @note please don't invoke it on ISR.
 */
void dfs_unlock(void)
{
    mutex_unlock(&fslock);
}

static int fd_alloc(struct dfs_fdtable *fdt, int startfd)
{
    int idx;

    /* find an empty fd entry */
    for (idx = startfd; idx < (int)fdt->maxfd; idx++)
    {
        if (fdt->fds[idx] == NULL)
            break;
        if (fdt->fds[idx]->ref_count == 0)
            break;
    }

    /* allocate a larger FD container */
    if (idx == fdt->maxfd && fdt->maxfd < DFS_FD_MAX)
    {
        int cnt, index;
        struct dfs_fd **fds;

        /* increase the number of FD with 4 step length */
        cnt = fdt->maxfd + 4;
        cnt = cnt > DFS_FD_MAX ? DFS_FD_MAX : cnt;

        fds = realloc(fdt->fds, cnt * sizeof(struct dfs_fd *));
        if (fds == NULL) goto __exit; /* return fdt->maxfd */

        /* clean the new allocated fds */
        for (index = fdt->maxfd; index < cnt; index ++)
        {
            fds[index] = NULL;
        }

        fdt->fds   = fds;
        fdt->maxfd = cnt;
    }

    /* allocate  'struct dfs_fd' */
    if (idx < (int)fdt->maxfd && fdt->fds[idx] == NULL)
    {
        fdt->fds[idx] = calloc(1, sizeof(struct dfs_fd));
        if (fdt->fds[idx] == NULL)
            idx = fdt->maxfd;
    }

__exit:
    return idx;
}

/*
 * @ingroup Fd
 * This function will allocate a file descriptor.
 *
 * @return -1 on failed or the allocated file descriptor.
 */
int fd_new(void)
{
    struct dfs_fd *d;
    int idx;
    struct dfs_fdtable *fdt;

    fdt = dfs_fdtable_get();
    /* lock filesystem */
    dfs_lock();

    /* find an empty fd entry */
    idx = fd_alloc(fdt, 0);

    /* can't find an empty fd entry */
    if (idx == fdt->maxfd)
    {
        idx = -(1 + DFS_FD_OFFSET);
        printf("DFS fd new is failed! Could not found an empty fd entry.\n");
        goto __result;
    }

    d = fdt->fds[idx];
    d->ref_count = 1;
    d->magic = DFS_FD_MAGIC;

__result:
    dfs_unlock();
    return idx + DFS_FD_OFFSET;
}

/*
 * @ingroup Fd
 *
 * This function will return a file descriptor structure according to file
 * descriptor.
 *
 * @return NULL on on this file descriptor or the file descriptor structure
 * pointer.
 */
struct dfs_fd *fd_get(int fd)
{
    struct dfs_fd *d;
    struct dfs_fdtable *fdt;

    fdt = dfs_fdtable_get();
    fd = fd - DFS_FD_OFFSET;
    if (fd < 0 || fd >= (int)fdt->maxfd)
        return NULL;

    dfs_lock();
    d = fdt->fds[fd];

    /* check dfs_fd valid or not */
    if ((d == NULL) || (d->magic != DFS_FD_MAGIC))
    {
        dfs_unlock();
        return NULL;
    }

    /* increase the reference count */
    d->ref_count ++;
    dfs_unlock();

    return d;
}

/*
 * @ingroup Fd
 *
 * This function will put the file descriptor.
 */
void fd_put(struct dfs_fd *fd)
{
    assert(fd != NULL);

    dfs_lock();

    fd->ref_count --;

    /* clear this fd entry */
    if (fd->ref_count == 0)
    {
        int index;
        struct dfs_fdtable *fdt;

        fdt = dfs_fdtable_get();
        for (index = 0; index < (int)fdt->maxfd; index ++)
        {
            if (fdt->fds[index] == fd)
            {
                free(fd);
                fdt->fds[index] = 0;
                break;
            }
        }
    }
    dfs_unlock();
}

/*
 * @ingroup Fd
 *
 * This function will return whether this file has been opend.
 *
 * @param pathname the file path name.
 *
 * @return 0 on file has been open successfully, -1 on open failed.
 */
int fd_is_open(const char *pathname)
{
    char *fullpath;
    unsigned int index;
    struct dfs_filesystem *fs;
    struct dfs_fd *fd;
    struct dfs_fdtable *fdt;

    fdt = dfs_fdtable_get();
    fullpath = dfs_normalize_path(NULL, pathname);
    if (fullpath != NULL)
    {
        char *mountpath;
        fs = dfs_filesystem_lookup(fullpath);
        if (fs == NULL)
        {
            /* can't find mounted file system */
            free(fullpath);

            return -1;
        }

        /* get file path name under mounted file system */
        if (fs->path[0] == '/' && fs->path[1] == '\0')
            mountpath = fullpath;
        else
            mountpath = fullpath + strlen(fs->path);

        dfs_lock();

        for (index = 0; index < fdt->maxfd; index++)
        {
            fd = fdt->fds[index];
            if (fd == NULL || fd->fops == NULL || fd->path == NULL) continue;

            if (fd->fs == fs && strcmp(fd->path, mountpath) == 0)
            {
                /* found file in file descriptor table */
                free(fullpath);
                dfs_unlock();

                return 0;
            }
        }
        dfs_unlock();

        free(fullpath);
    }

    return -1;
}

/*
 * this function will return a sub-path name under directory.
 *
 * @param directory the parent directory.
 * @param filename the filename.
 *
 * @return the subdir pointer in filename
 */
const char *dfs_subdir(const char *directory, const char *filename)
{
    const char *dir;

    if (strlen(directory) == strlen(filename)) /* it's a same path */
        return NULL;

    dir = filename + strlen(directory);
    if ((*dir != '/') && (dir != filename))
    {
        dir --;
    }

    return dir;
}


/*
 * this function will normalize a path according to specified parent directory
 * and file name.
 *
 * @param directory the parent path
 * @param filename the file name
 *
 * @return the built full file path (absolute path)
 */
char *dfs_normalize_path(const char *directory, const char *filename)
{
    char *fullpath;
    char *dst0, *dst, *src;
    char *current_path = NULL;
    /* check parameters */
    assert(filename != NULL);

#ifdef DFS_USING_WORKDIR
    if (directory == NULL) { /* shall use working directory */
        directory = &working_directory[0];

    } else if (directory[0] != '/') {
        /* directory is relative path */
        current_path = malloc(strlen(working_directory) + strlen(directory) + 2);
        snprintf(current_path, strlen(working_directory) + strlen(directory) + 2,
                "%s/%s", working_directory, directory);
        directory = current_path;

    } else {
        /* directory is absolute path */
        /* nothing todo */
    }
#else
    if ((directory == NULL) && (filename[0] != '/'))
    {
        printf(NO_WORKING_DIR);

        return NULL;
    }
#endif

    if (filename[0] != '/') /* it's a absolute path, use it directly */
    {
        fullpath = malloc(strlen(directory) + strlen(filename) + 2);

        if (fullpath == NULL)
            return NULL;

        /* join path and file name */
        snprintf(fullpath, strlen(directory) + strlen(filename) + 2,
                    "%s/%s", directory, filename);
    }
    else
    {
        fullpath = strdup(filename); /* copy string */

        if (fullpath == NULL)
            return NULL;
    }

    src = fullpath;
    dst = fullpath;

    dst0 = dst;
    while (1)
    {
        char c = *src;

        if (c == '.')
        {
            if (!src[1]) src ++; /* '.' and ends */
            else if (src[1] == '/')
            {
                /* './' case */
                src += 2;

                while ((*src == '/') && (*src != '\0'))
                    src ++;
                continue;
            }
            else if (src[1] == '.')
            {
                if (!src[2])
                {
                    /* '..' and ends case */
                    src += 2;
                    goto up_one;
                }
                else if (src[2] == '/')
                {
                    /* '../' case */
                    src += 3;

                    while ((*src == '/') && (*src != '\0'))
                        src ++;
                    goto up_one;
                }
            }
        }

        /* copy up the next '/' and erase all '/' */
        while ((c = *src++) != '\0' && c != '/')
            *dst ++ = c;

        if (c == '/')
        {
            *dst ++ = '/';
            while (c == '/')
                c = *src++;

            src --;
        }
        else if (!c)
            break;

        continue;

up_one:
        dst --;
        if (dst < dst0)
        {
            free(fullpath);
            return NULL;
        }
        while (dst0 < dst && dst[-1] != '/')
            dst --;
    }

    *dst = '\0';

    /* remove '/' in the end of path if exist */
    dst --;
    if ((dst != fullpath) && (*dst == '/'))
        *dst = '\0';

    /* final check fullpath is not empty, for the special path of lwext "/.." */
    if ('\0' == fullpath[0])
    {
        fullpath[0] = '/';
        fullpath[1] = '\0';
    }

    if (current_path) {
        free(current_path);
    }

    return fullpath;
}


/*
 * This function will get the file descriptor table of current process.
 */
struct dfs_fdtable *dfs_fdtable_get(void)
{
    struct dfs_fdtable *fdt;
    fdt = &_fdtab;

    return fdt;
}

#ifdef _USING_SHELL

int list_fd(void)
{
    int index;
    struct dfs_fdtable *fd_table;

    fd_table = dfs_fdtable_get();
    if (!fd_table) return -1;

    os_enter_critical();

    printf("fd type    ref magic  path\n");
    printf("-- ------  --- ----- ------\n");
    for (index = 0; index < (int)fd_table->maxfd; index ++)
    {
        struct dfs_fd *fd = fd_table->fds[index];

        if (fd && fd->fops)
        {
            printf("%2d ", index);
            if (fd->type == FT_DIRECTORY)    printf("%-7.7s ", "dir");
            else if (fd->type == FT_REGULAR) printf("%-7.7s ", "file");
            else if (fd->type == FT_SOCKET)  printf("%-7.7s ", "socket");
            else if (fd->type == FT_USER)    printf("%-7.7s ", "user");
            else printf("%-8.8s ", "unknown");
            printf("%3d ", fd->ref_count);
            printf("%04x  ", fd->magic);
            if (fd->path)
            {
                printf("%s\n", fd->path);
            }
            else
            {
                printf("\n");
            }
        }
    }
    os_exit_critical();

    return 0;
}

#endif

#include <dfs_posix.h>

int mount_fs(const char *dir_name, const char *device_name, const char *fs_type, enum format_filesystem_type format)
{
    int ret;
    DIR *dir = NULL;
    int try_count = 0;

    if (!device_name || !fs_type)
        return -ENODEV;

    while (1) {
        /*
         * mount fs to test fs exist
         */
        ret = dfs_mount(device_name, dir_name, fs_type, 0, 0);
        if (ret) {
            if (fs_get_errno() == 0 || fs_get_errno() == -ENOENT) {
                goto mkfs_and_try_again;
            } else {
                //printf("filesystem:mount Device(%s) Type(%s) to DIR(%s) failed. ret %d errno %d\n", device_name, fs_type, dir_name, ret, fs_get_errno());
                return ret;
            }
        }
        /*
         * open root dir to ensure fs is ok
         */
        dir = opendir(dir_name);
        if (dir == NULL) {
            ret = fs_get_errno();
            dfs_unmount(dir_name);
            goto mkfs_and_try_again;
        }
        /*
         * now fs is ok
         */
        closedir(dir);

        return 0;

mkfs_and_try_again:
        if (try_count++ == 1) {
            printf("filesystem:retry mount Device(%s) Type(%s) to DIR(%s) failed. ret %d errno %d\n", device_name, fs_type, dir_name, ret, fs_get_errno());
            return ret;
        }

        /* mount失败，不格式化分区 */
        if (FORMAT_FILESYSTEM_TYPE_NO_FORMAT == format) {
            printf("filesystem:mount Device(%s) Type(%s) to DIR(%s) failed. ret %d errno %d\n", device_name, fs_type, dir_name, ret, fs_get_errno());
            return ret;
        }

        /*
         * mkfs: if fs is not exist or have error, mkfs and test again
         */
        ret = dfs_mkfs(fs_type, device_name, 0);
        if (ret)
            return ret;
    }

    return 0;
}
