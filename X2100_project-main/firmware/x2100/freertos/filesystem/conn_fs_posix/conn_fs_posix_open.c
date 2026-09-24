#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>

#ifdef CONFIG_CONN_FS
int soc_conn_fs_ops_posix_ioctl(uint32_t length, void *cmds, void *res);


#ifdef CONN_FS_CMDS_TAG
#undef CONN_FS_CMDS_TAG
#endif

#define CONN_FS_CMDS_TAG                "conn_fs_cmds_open:"

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
static void conn_fs_posix_rtos_result(struct conn_fs_posix_cmds_result *res);

static void conn_fs_posix_rtos_args_open(struct conn_fs_posix_cmds_arguments *args)
{
    uint32_t *argv0 = args->argv[0];
    uint32_t *argv1 = args->argv[1];
    uint32_t *argv2 = args->argv[2];

    char *file_name = (char *)(*argv0);
    uint32_t flags = *argv1;
    uint32_t mode = *argv2;

    printf(CONN_FS_CMDS_TAG "args address        : %p\n", args);
    printf(CONN_FS_CMDS_TAG "args->cmd_index     : %d\n", args->cmd_index);
    printf(CONN_FS_CMDS_TAG "args->argc          : %d\n", args->argc);
    printf(CONN_FS_CMDS_TAG "args->argv address  : %p\n", args->argv);
    printf(CONN_FS_CMDS_TAG "argv[0]:file_name   : (0x%p)(0x%p)%s\n", argv0, file_name, file_name);
    printf(CONN_FS_CMDS_TAG "argv[1]:flags       : (0x%p)(0x%p)0x%x\n", argv1, &flags, flags);
    printf(CONN_FS_CMDS_TAG "argv[2]:mode        : (0x%p)(0x%p)0x%x\n", argv2, &mode, mode);
}
#endif

/*
 * 该函数兼容Linux POSIX版本
 * 功能: 打开一个文件并根据指定的标志返回一个文件描述符
 *
 * @参数: file  文件命令.
 * @参数: flags 文件打开标志
 * @参数: mode  文件属性
 *
 * @返回值: 非负数: 成功
 *         其他值: 失败
 */
int conn_fs_ops_posix_open(const char *file, int flags, mode_t mode)
{
    struct conn_fs_posix_cmds_arguments args;
    struct conn_fs_posix_cmds_result res;

    int args_length = sizeof(struct conn_fs_posix_cmds_arguments);

    /* Linux端驱动识别是哪种操作类型 */
    args.cmd_index = conn_fs_posix_cmds_open;

    /* API的参数个数:(不包括API本身) */
    args.argc = 3;

    if (args.argc) {
        int size = args.argc * sizeof(void *);

        void **cmds_argv = malloc(size);
        if (!cmds_argv) {
            printf(CONN_FS_CMDS_TAG "dfs posix conn fs cmds(open) argv malloc(%d Byte) failed.\n", size);
            return -ENOMEM;
        }

        cmds_argv[0] = &file;
        cmds_argv[1] = &flags;
        cmds_argv[2] = &mode;

        args.argv = cmds_argv;
    }

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
    conn_fs_posix_rtos_args_open(&args);

    soc_conn_fs_ops_posix_ioctl(args_length, &args, &res);

    conn_fs_posix_rtos_result(&res);

#else
    soc_conn_fs_ops_posix_ioctl(args_length, &args, &res);
#endif

    return res.res_val;
}

int conn_fs_file_record_ops_posix_open(const char *file, int flags, mode_t mode, int fd)
{
    struct conn_fs_file_info_record *node = conn_fs_alloc_file_node();

    strcpy(node->name, file);
    node->flags = flags;
    node->mode = mode;
    node->offset = 0;
    node->rtos_fd = fd;

    return 0;
}

#else
int conn_fs_ops_posix_open(const char *file, int flags, mode_t mode)
{
    return 0;
}

int conn_fs_file_record_ops_posix_open(const char *file, int flags, mode_t mode, int fd)
{
    return 0;
}
#endif
