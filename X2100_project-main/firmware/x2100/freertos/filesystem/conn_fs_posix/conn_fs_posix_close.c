#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>

#ifdef CONFIG_CONN_FS

int soc_conn_fs_ops_posix_ioctl(uint32_t length, void *cmds, void *res);

#ifdef CONN_FS_CMDS_TAG
#undef CONN_FS_CMDS_TAG
#endif

#define CONN_FS_CMDS_TAG                "conn_fs_cmds_close:"

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
static void conn_fs_posix_rtos_result(struct conn_fs_posix_cmds_result *res);

static void conn_fs_posix_rtos_args_close(struct conn_fs_posix_cmds_arguments *args)
{
    uint32_t *argv0 = args->argv[0];

    uint32_t fd = *argv0;

    printf(CONN_FS_CMDS_TAG "args address       : %p\n", args);
    printf(CONN_FS_CMDS_TAG "args->cmd_index    : %d\n", args->cmd_index);
    printf(CONN_FS_CMDS_TAG "args->argc         : %d\n", args->argc);
    printf(CONN_FS_CMDS_TAG "args->argv address : %p\n", args->argv);
    printf(CONN_FS_CMDS_TAG "argv[0]:fd         : (0x%p)(0x%p)%d\n", argv0, &fd, fd);
}
#endif

/*
 * 该函数兼容Linux POSIX版本
 * 功能: 关闭一个文件描述符
 *
 * @参数: fd  文件描述符号句柄.
 *
 * @返回值: 0  : 成功
 *         -1 : 失败
 */
int conn_fs_ops_posix_close(int fd)
{
    struct conn_fs_posix_cmds_arguments args;
    struct conn_fs_posix_cmds_result res;
    int file_fd = fd;

    /* 查找是否有Linux启动前，RTOS已经打开的文件描述符号 */
    struct conn_fs_file_info_record *node = conn_fs_find_file_node(fd);
    if (node) {
        file_fd = node->linux_fd;
        conn_fs_free_file_node(node);
    }

    int args_length = sizeof(struct conn_fs_posix_cmds_arguments);

    /* Linux端驱动识别是哪种操作类型 */
    args.cmd_index = conn_fs_posix_cmds_close;

    /* API的参数个数:(不包括API本身) */
    args.argc = 1;

    if (args.argc) {
        int size = args.argc * sizeof(void *);

        void **cmds_argv = malloc(size);
        if (!cmds_argv) {
            printf(CONN_FS_CMDS_TAG "dfs posix conn fs cmds(close) argv malloc(%d Byte) failed.\n", size);
            return -ENOMEM;
        }

        cmds_argv[0] = &file_fd;

        args.argv = cmds_argv;

    }

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
    conn_fs_posix_rtos_args_close(&args);

    soc_conn_fs_ops_posix_ioctl(args_length, &args, &res);

    conn_fs_posix_rtos_result(&res);
#else
    soc_conn_fs_ops_posix_ioctl(args_length, &args, &res);
#endif

    return res.res_val;
}

int conn_fs_file_record_ops_posix_close(int fd)
{
    struct conn_fs_file_info_record *node = conn_fs_find_file_node(fd);

    conn_fs_free_file_node(node);

    return 0;
}

#else

int conn_fs_ops_posix_close(int fd)
{
    return -1;
}

int conn_fs_file_record_ops_posix_close(int fd)
{
    return 0;
}

#endif
