#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>

#ifdef CONN_FS_CMDS_TAG
#undef CONN_FS_CMDS_TAG
#endif

#define CONN_FS_CMDS_TAG                "conn_fs_update_file_info:"

#ifdef CONFIG_CONN_FS
/*
 * RTOS已在工作Linux未启动完成时，记录已操作文件相关信息，
 */
struct conn_fs_file_info_record {
    struct list_head link;
    int rtos_fd;
    int linux_fd;

    char name[256];
    int flags;
    mode_t mode;
    off_t offset;
};

/* 已打开单未同步到Linux端的文件 */
static struct list_head record_file_info_list = LIST_HEAD_INIT(record_file_info_list);
static DEFINE_MUTEX(file_mutex);


static void conn_fs_add_file_node_to_list(struct conn_fs_file_info_record *node)
{
    list_add_tail(&node->link, &record_file_info_list);
}

static void conn_fs_del_file_node(struct conn_fs_file_info_record *node)
{
    list_del_init(&node->link);
}

static struct conn_fs_file_info_record *conn_fs_alloc_file_node(void)
{
    mutex_lock(&file_mutex);

    struct conn_fs_file_info_record *node = malloc(sizeof(struct conn_fs_file_info_record));
    if (!node) {
        printf(CONN_FS_CMDS_TAG "malloc file node failed\n");
        mutex_unlock(&file_mutex);
        return NULL;
    }

    memset(node, 0x00, sizeof(struct conn_fs_file_info_record));

    node->rtos_fd = -1;
    node->linux_fd = -1;

    conn_fs_add_file_node_to_list(node);

    mutex_unlock(&file_mutex);

    return node;
}

static void conn_fs_free_file_node(struct conn_fs_file_info_record *node)
{
    assert(node);

    mutex_lock(&file_mutex);

    conn_fs_del_file_node(node);

    free(node);

    mutex_unlock(&file_mutex);
}

static struct conn_fs_file_info_record *conn_fs_find_file_node(int fd)
{
    struct conn_fs_file_info_record *node;

    mutex_lock(&file_mutex);

    if (list_empty(&record_file_info_list)) {
        mutex_unlock(&file_mutex);
        return NULL;
    }

    list_for_each_entry(node, &record_file_info_list, link) {
        if (node->rtos_fd == fd) {
            mutex_unlock(&file_mutex);
            return node;
        }
    }

    mutex_unlock(&file_mutex);
    return NULL;
}

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
static void conn_fs_dump_all_file_node_info(void)
{
    struct conn_fs_file_info_record *node;

    mutex_lock(&file_mutex);
    if (list_empty(&record_file_info_list)) {
        printf(CONN_FS_CMDS_TAG "file node list is empty\n");
        mutex_unlock(&file_mutex);
        return ;
    }

    list_for_each_entry(node, &record_file_info_list, link) {
        printf(CONN_FS_CMDS_TAG "rtos fd      : %d\n", node->rtos_fd);
        printf(CONN_FS_CMDS_TAG "linux fd     : %d\n", node->linux_fd);
        printf(CONN_FS_CMDS_TAG "name         : %s\n", node->name);
        printf(CONN_FS_CMDS_TAG "flags        : 0x%x\n", node->flags);
        printf(CONN_FS_CMDS_TAG "mode         : 0x%x\n", node->mode);
        printf(CONN_FS_CMDS_TAG "offset       : %d\n", node->offset);
    }

    mutex_unlock(&file_mutex);
}

#else

static void conn_fs_dump_all_file_node_info(void)
{
    /* Nothing TODO */
}

#endif

static void conn_fs_update_file_node_to_linux(void)
{
    struct conn_fs_file_info_record *node;


    if (list_empty(&record_file_info_list)) {
#ifdef CONFIG_CONN_FS_CMDS_DEBUG
        printf(CONN_FS_CMDS_TAG "file node list is empty, no need update\n");
#endif
        return ;
    }

    /* 将文件状态同步到Linux端 */
    list_for_each_entry(node, &record_file_info_list, link) {
        if (node->rtos_fd < 0)
            printf(CONN_FS_CMDS_TAG "rtos fd(%d) is invalid\n", node->rtos_fd);

        if (node->linux_fd > 0)
            printf(CONN_FS_CMDS_TAG "linux fd(%d) is already opened\n", node->linux_fd);

        node->linux_fd = conn_fs_ops_posix_open(node->name, node->flags, node->mode);

        if (node->linux_fd)
            conn_fs_ops_posix_lseek(node->linux_fd, node->offset, SEEK_SET);
    }

    conn_fs_dump_all_file_node_info();

    return NULL;
}

#endif