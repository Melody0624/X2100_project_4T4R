#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>


#ifdef CONFIG_CONN_FS
int soc_conn_fs_ops_posix_ioctl(uint32_t length, void *cmds, void *res);

#ifdef CONN_FS_CMDS_TAG
#undef CONN_FS_CMDS_TAG
#endif

#define CONN_FS_CMDS_TAG                "conn_fs_cmds_write:"

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
static void conn_fs_posix_rtos_result(struct conn_fs_posix_cmds_result *res);

static void conn_fs_posix_rtos_args_write(struct conn_fs_posix_cmds_arguments *args)
{
    uint32_t *argv0 = args->argv[0];
    uint32_t *argv1 = args->argv[1];
    uint32_t *argv2 = args->argv[2];

    uint32_t fd = *argv0;
    void *buf = (void *)(*argv1);
    uint32_t len = *argv2;

    printf(CONN_FS_CMDS_TAG "args address       : %p\n", args);
    printf(CONN_FS_CMDS_TAG "args->cmd_index    : %d\n", args->cmd_index);
    printf(CONN_FS_CMDS_TAG "args->argc         : %d\n", args->argc);
    printf(CONN_FS_CMDS_TAG "args->argv address : %p\n", args->argv);
    printf(CONN_FS_CMDS_TAG "argv[0]:fd         : (0x%p)(0x%p)%d\n", argv0, &fd, fd);
    printf(CONN_FS_CMDS_TAG "argv[1]:buf[0]     : (0x%p)(0x%p)0x%x\n", argv1, buf, ((uint32_t *)buf)[0]);
    printf(CONN_FS_CMDS_TAG "argv[2]:len        : (0x%p)(0x%p)0x%x\n", argv2, &len, len);
}
#endif

/*
 * 该函数兼容Linux POSIX版本
 * 功能: 从缓冲区中写入指定长度的数据到已打开的文件描述符中
 *
 * @参数: fd  文件描述符号句柄.
 * @参数: buf 被写入的数据缓冲区
 * @参数: len 最大写入数据长度
 *
 * @返回值: 实际写入数据长度.
 */
int conn_fs_ops_posix_write(int fd, const void *buf, size_t len)
{
    struct conn_fs_posix_cmds_arguments args;
    struct conn_fs_posix_cmds_result res;
    int file_fd = fd;

    /* 查找是否有Linux启动前，RTOS已经打开的文件描述符号 */
    struct conn_fs_file_info_record *node = conn_fs_find_file_node(fd);
    if (node)
        file_fd = node->linux_fd;

    int length = sizeof(struct conn_fs_posix_cmds_arguments);

    /* API的参数个数:(不包括API本身) */
    args.cmd_index = conn_fs_posix_cmds_write;

    args.argc = 3;
    if (args.argc) {
        int size = args.argc * sizeof(void *);

        void **cmds_argv = malloc(size);
        if (!cmds_argv) {
            printf(CONN_FS_CMDS_TAG "dfs posix conn fs cmds(write) argv malloc(%d Byte) failed.\n", size);
            return -ENOMEM;
        }

        cmds_argv[0] = &file_fd;
        cmds_argv[1] = &buf;
        cmds_argv[2] = &len;

        args.argv = cmds_argv;
    }

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
    conn_fs_posix_rtos_args_write(&args);

    soc_conn_fs_ops_posix_ioctl(length, &args, &res);

    /* 如果有接收内容可打印接收到的内容 */
    res.result = (void *)buf;
    conn_fs_posix_rtos_result(&res);
#else
    soc_conn_fs_ops_posix_ioctl(length, &args, &res);
#endif

    return res.res_val;
}

int conn_fs_file_record_ops_posix_write(int fd, off_t offset)
{
    struct conn_fs_file_info_record *node = conn_fs_find_file_node(fd);

    /* 功能和lseek一致 */
    if (node)
        node->offset = offset;

    return 0;
}

#else
int conn_fs_ops_posix_write(int fd, const void *buf, size_t len)
{
    return 0;
}

int conn_fs_file_record_ops_posix_write(int fd, off_t offset)
{
    return 0;
}
#endif
