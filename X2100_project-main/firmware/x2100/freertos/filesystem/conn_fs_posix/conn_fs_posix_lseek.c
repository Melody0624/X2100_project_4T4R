#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>

#ifdef CONFIG_CONN_FS

int soc_conn_fs_ops_posix_ioctl(uint32_t length, void *cmds, void *res);


#ifdef CONN_FS_CMDS_TAG
#undef CONN_FS_CMDS_TAG
#endif

#define CONN_FS_CMDS_TAG                "conn_fs_cmds_lseek:"

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
static void conn_fs_posix_rtos_result(struct conn_fs_posix_cmds_result *res);

static void conn_fs_posix_rtos_args_lseek(struct conn_fs_posix_cmds_arguments *args)
{
    int *argv0 = args->argv[0];
    off_t *argv1 = args->argv[1];
    int *argv2 = args->argv[2];

    int fd = *argv0;
    off_t offset = *argv1;
    int whence = *argv2;

    printf(CONN_FS_CMDS_TAG "args address       : %p\n", args);
    printf(CONN_FS_CMDS_TAG "args->cmd_index    : %d\n", args->cmd_index);
    printf(CONN_FS_CMDS_TAG "args->argc         : %d\n", args->argc);
    printf(CONN_FS_CMDS_TAG "args->argv address : %p\n", args->argv);
    printf(CONN_FS_CMDS_TAG "argv[0]:fd         : (0x%p)(0x%p)%d\n", argv0, &fd, fd);
    printf(CONN_FS_CMDS_TAG "argv[1]:offset     : (0x%p)(0x%p)%ld\n", argv1, &offset, offset);
    printf(CONN_FS_CMDS_TAG "argv[2]:whence     : (0x%p)(0x%p)%d\n", argv2, &whence, whence);
}
#endif

/*
 * 该函数兼容Linux POSIX版本
 * 功能: 将打开的文件描述的偏移指针定位到指定位置
 *
 * @参数: fd      文件描述符号句柄.
 * @参数: offset  偏移位置.
 * @参数: whence  偏移方向.
 *
 * @返回值: 0  : 文件当前的读写偏移位置
 *         -1 : 失败
 */
off_t conn_fs_ops_posix_lseek(int fd, off_t offset, int whence)
{
    struct conn_fs_posix_cmds_arguments args;
    struct conn_fs_posix_cmds_result res;
    int file_fd = fd;

    /* 查找是否有Linux启动前，RTOS已经打开的文件描述符号 */
    struct conn_fs_file_info_record *node = conn_fs_find_file_node(fd);
    if (node)
        file_fd = node->linux_fd;

    int args_length = sizeof(struct conn_fs_posix_cmds_arguments);

    /* Linux端驱动识别是哪种操作类型 */
    args.cmd_index = conn_fs_posix_cmds_lseek;

    /* API的参数个数:(不包括API本身) */
    args.argc = 3;

    if (args.argc) {
        int size = args.argc * sizeof(void *);

        void **cmds_argv = malloc(size);
        if (!cmds_argv) {
            printf(CONN_FS_CMDS_TAG "dfs posix conn fs cmds(close) argv malloc(%d Byte) failed.\n", size);
            return -ENOMEM;
        }

        cmds_argv[0] = &file_fd;
        cmds_argv[1] = &offset;
        cmds_argv[2] = &whence;

        args.argv = cmds_argv;
    }

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
    conn_fs_posix_rtos_args_lseek(&args);

    soc_conn_fs_ops_posix_ioctl(args_length, &args, &res);

    conn_fs_posix_rtos_result(&res);
#else
    soc_conn_fs_ops_posix_ioctl(args_length, &args, &res);
#endif

    return res.res_val;
}

int conn_fs_file_record_ops_posix_lseek(int fd, off_t offset)
{
    struct conn_fs_file_info_record *node = conn_fs_find_file_node(fd);

    if (node)
        node->offset = offset;

    return 0;
}
#else

off_t conn_fs_ops_posix_lseek(int fd, off_t offset, int whence)
{
    return 0;
}

int conn_fs_file_record_ops_posix_lseek(int fd, off_t offset)
{
    return 0;
}
#endif
