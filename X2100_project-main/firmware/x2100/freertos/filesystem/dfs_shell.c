#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_private.h>
#include <dfs_shell.h>

static int check_str_with_special_ch(const char *str)
{
    char *p = (char *)str;
    unsigned char str_len = strlen(str);
    while ( p != (str + str_len))
    {
        if ( *p == ' ' || *p == '\'' || *p == '\"') {
            return 1;
        } else {
            p++;
        }
    }
    return 0;
}

void ls(const char *pathname)
{
    struct dfs_fd fd;
    struct dirent dirent;
    struct stat stat;
    int length;
    char *fullpath, *path;
    int info_flag = 0;

    fullpath = NULL;
    if (pathname == NULL)
    {
#ifdef DFS_USING_WORKDIR
        /* open current working directory */
        path = strdup(working_directory);
#else
        path = strdup("/");
#endif
        if (path == NULL)
            return ; /* out of memory */
    }
    else
    {
        path = (char *)pathname;
    }


    /* list directory */
    if (dfs_file_open(&fd, path, O_DIRECTORY) == 0)
    {
        //elm_printf("Directory %s:\n", path);
        do
        {
            memset(&dirent, 0, sizeof(struct dirent));
            length = dfs_file_getdents(&fd, &dirent, sizeof(struct dirent));
            if (length > 0)
            {
                memset(&stat, 0, sizeof(struct stat));

                /* build full path for each file */
                fullpath = dfs_normalize_path(path, dirent.d_name);
                if (fullpath == NULL)
                    break;

                if (!info_flag) {
                    info_flag = 1;
                    elm_printf("%-15s%s\n", "Type/Size", "Name");
                }

                if (dfs_file_stat(fullpath, &stat) == 0)
                {
                    if (S_ISDIR(stat.st_mode))
                    {
                        elm_printf("%-15s", "<DIR>");
                    }
                    else
                    {
                        elm_printf("%-15lu", stat.st_size);
                    }
                    if (check_str_with_special_ch(dirent.d_name)) {
                        elm_printf("'%s'\n", dirent.d_name);
                    } else {
                        elm_printf("%s\n", dirent.d_name);
                    }
                }
                else
                    elm_printf("BAD file: %s\n", dirent.d_name);
                free(fullpath);
            }
        }
        while (length > 0);

        dfs_file_close(&fd);
    }
    else
    {
        /* list file */
        if (dfs_file_stat(path, &stat) == 0) {

            if (!info_flag) {
                info_flag = 1;
                elm_printf("%-15s%s\n", "Type/Size", "Name");
            }

            if (S_ISDIR(stat.st_mode)) {
                elm_printf("%-15s", "<DIR>");
            } else {
                elm_printf("%-15lu", stat.st_size);
            }
            if (check_str_with_special_ch(path)) {
                elm_printf("'%s'\n", path);
            } else {
                elm_printf("%s\n", path);
            }
        } else {
            elm_printf("%s :No such file or directory\n", path);
        }
    }

    if (pathname == NULL)
        free(path);
}

void rm(const char *filename)
{
    if (dfs_file_unlink(filename) < 0)
    {
        elm_printf("Delete %s failed\n", filename);
    }
}


void cat(const char *filename)
{
    struct dfs_fd fd;
    uint32_t length;
    char buffer[81];

    if (dfs_file_open(&fd, filename, O_RDONLY) < 0)
    {
        elm_printf("Open %s failed\n", filename);

        return;
    }

    do
    {
        memset(buffer, 0, sizeof(buffer));
        length = dfs_file_read(&fd, buffer, sizeof(buffer) - 1);
        if (length > 0)
        {
            elm_printf("%s", buffer);
        }
    }
    while (length > 0);

    elm_printf("\n");
    dfs_file_close(&fd);
}


#define BUF_SZ  4096
static void copyfile(const char *src, const char *dst)
{
    struct dfs_fd dst_fd;
    struct dfs_fd src_fd;
    uint8_t *block_ptr;
    int32_t read_bytes;

    block_ptr = malloc(BUF_SZ);
    if (block_ptr == NULL)
    {
        elm_printf("out of memory\n");

        return;
    }

    if (dfs_file_open(&src_fd, src, O_RDONLY) < 0)
    {
        free(block_ptr);
        elm_printf("Read %s failed\n", src);

        return;
    }
    if (dfs_file_open(&dst_fd, dst, O_WRONLY | O_CREAT) < 0)
    {
        free(block_ptr);
        dfs_file_close(&src_fd);

        elm_printf("Write %s failed\n", dst);

        return;
    }

    do
    {
        read_bytes = dfs_file_read(&src_fd, block_ptr, BUF_SZ);
        if (read_bytes > 0)
        {
            int length;

            length = dfs_file_write(&dst_fd, block_ptr, read_bytes);
            if (length != read_bytes)
            {
                /* write failed. */
                elm_printf("Write file data failed, errno=%d\n", length);
                break;
            }
        }
    }
    while (read_bytes > 0);

    dfs_file_close(&src_fd);
    dfs_file_close(&dst_fd);
    free(block_ptr);
}

extern int mkdir(const char *path, mode_t mode);
static void copydir(const char *src, const char *dst)
{
    struct dirent dirent;
    struct stat stat;
    int length;
    struct dfs_fd cpfd;
    if (dfs_file_open(&cpfd, src, O_DIRECTORY) < 0)
    {
        elm_printf("open %s failed\n", src);
        return ;
    }

    do
    {
        memset(&dirent, 0, sizeof(struct dirent));

        length = dfs_file_getdents(&cpfd, &dirent, sizeof(struct dirent));
        if (length > 0)
        {
            char *src_entry_full = NULL;
            char *dst_entry_full = NULL;

            if (strcmp(dirent.d_name, "..") == 0 || strcmp(dirent.d_name, ".") == 0)
                continue;

            /* build full path for each file */
            if ((src_entry_full = dfs_normalize_path(src, dirent.d_name)) == NULL)
            {
                elm_printf("out of memory!\n");
                break;
            }
            if ((dst_entry_full = dfs_normalize_path(dst, dirent.d_name)) == NULL)
            {
                elm_printf("out of memory!\n");
                free(src_entry_full);
                break;
            }

            memset(&stat, 0, sizeof(struct stat));
            if (dfs_file_stat(src_entry_full, &stat) != 0)
            {
                elm_printf("open file: %s failed\n", dirent.d_name);
                continue;
            }

            if (S_ISDIR(stat.st_mode))
            {
                mkdir(dst_entry_full, 0);
                copydir(src_entry_full, dst_entry_full);
            }
            else
            {
                copyfile(src_entry_full, dst_entry_full);
            }
            free(src_entry_full);
            free(dst_entry_full);
        }
    }
    while (length > 0);

    dfs_file_close(&cpfd);
}

static const char *_get_path_lastname(const char *path)
{
    char *ptr;
    if ((ptr = strrchr(path, '/')) == NULL)
        return path;

    /* skip the '/' then return */
    return ++ptr;
}
void copy(const char *src, const char *dst)
{
#define FLAG_SRC_TYPE      0x03
#define FLAG_SRC_IS_DIR    0x01
#define FLAG_SRC_IS_FILE   0x02
#define FLAG_SRC_NON_EXSIT 0x00

#define FLAG_DST_TYPE      0x0C
#define FLAG_DST_IS_DIR    0x04
#define FLAG_DST_IS_FILE   0x08
#define FLAG_DST_NON_EXSIT 0x00

    struct stat stat;
    uint32_t flag = 0;

    /* check the staus of src and dst */
    if (dfs_file_stat(src, &stat) < 0)
    {
        elm_printf("copy failed, bad %s\n", src);
        return;
    }
    if (S_ISDIR(stat.st_mode))
        flag |= FLAG_SRC_IS_DIR;
    else
        flag |= FLAG_SRC_IS_FILE;

    if (dfs_file_stat(dst, &stat) < 0)
    {
        flag |= FLAG_DST_NON_EXSIT;
    }
    else
    {
        if (S_ISDIR(stat.st_mode))
            flag |= FLAG_DST_IS_DIR;
        else
            flag |= FLAG_DST_IS_FILE;
    }

    //2. check status
    if ((flag & FLAG_SRC_IS_DIR) && (flag & FLAG_DST_IS_FILE))
    {
        elm_printf("cp faild, cp dir to file is not permitted!\n");
        return ;
    }

    //3. do copy
    if (flag & FLAG_SRC_IS_FILE)
    {
        if (flag & FLAG_DST_IS_DIR)
        {
            char *fdst;
            fdst = dfs_normalize_path(dst, _get_path_lastname(src));
            if (fdst == NULL)
            {
                elm_printf("out of memory\n");
                return;
            }
            copyfile(src, fdst);
            free(fdst);
        }
        else
        {
            copyfile(src, dst);
        }
    }
    else //flag & FLAG_SRC_IS_DIR
    {
        if (flag & FLAG_DST_IS_DIR)
        {
            char *fdst;
            fdst = dfs_normalize_path(dst, _get_path_lastname(src));
            if (fdst == NULL)
            {
                elm_printf("out of memory\n");
                return;
            }
            mkdir(fdst, 0);
            copydir(src, fdst);
            free(fdst);
        }
        else if ((flag & FLAG_DST_TYPE) == FLAG_DST_NON_EXSIT)
        {
            mkdir(dst, 0);
            copydir(src, dst);
        }
        else
        {
            copydir(src, dst);
        }
    }
}


/**************************************/
void mkfs(const char *fs_name, const char *device_name)
{
    dfs_mkfs(fs_name, device_name, 0);
}


int df(const char *path)
{
    int result;
    int minor = 0;
    long long cap;

    struct statfs buffer;

    int unit_index = 0;
    char *unit_str[] = {"KB", "MB", "GB"};

    long long total_cap;
    int total_minor = 0;
    int total_unit_index = 0;

    result = dfs_statfs(path ? path : NULL, &buffer);
    if (result != 0)
    {
        elm_printf("dfs_statfs failed.\n");
        return -1;
    }

    total_cap = ((long long)buffer.f_bsize) * ((long long)buffer.f_blocks) / 1024LL;
    for (total_unit_index = 0; total_unit_index < 2; total_unit_index ++)
    {
        if (total_cap < 1024) break;

        total_minor = (total_cap % 1024) * 10 / 1024; /* only one decimal point */
        total_cap = total_cap / 1024;
    }

    cap = ((long long)buffer.f_bsize) * ((long long)buffer.f_bfree) / 1024LL;
    for (unit_index = 0; unit_index < 2; unit_index ++)
    {
        if (cap < 1024) break;

        minor = (cap % 1024) * 10 / 1024; /* only one decimal point */
        cap = cap / 1024;
    }

    elm_printf("MonutOn: \"%s\"\n", path);
    elm_printf("Disk-free: %d.%d %s, Disk-total: %d.%d %s  [ %d block, %d bytes per block ]\n",
               (unsigned long)cap, minor, unit_str[unit_index],
               (unsigned long)total_cap, total_minor, unit_str[total_unit_index],
               buffer.f_bfree, buffer.f_bsize);
    return 0;
}
