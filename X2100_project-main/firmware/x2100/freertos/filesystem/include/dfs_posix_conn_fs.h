#ifndef __DFS_POSIX_CONN_FS_H__
#define __DFS_POSIX_CONN_FS_H__

#include <dfs_file.h>

#ifdef __cplusplus
extern "C" {
#endif

enum conn_fs_posix_cmds {
    conn_fs_posix_cmds_open,
    conn_fs_posix_cmds_close,
    conn_fs_posix_cmds_read,
    conn_fs_posix_cmds_write,
    conn_fs_posix_cmds_lseek,
};

struct conn_fs_posix_cmds_arguments {
    uint32_t cmd_index;     /* 指定命令索引号，Linux端按此转发到posix应用层对应操作 */
    int argc;               /* argument count */
    void **argv;            /* argument vector */
};

struct conn_fs_posix_cmds_result {
    uint32_t cmd_index;     /* 返回Linux应用层和驱动交互的magic Number. 暂无用处 */
    int res_val;            /* 返回值, posix API操作结果 */
    uint32_t result_len;    /* 返回内容的长度, */
    void *result;           /* 返回内存存放buffer, Linux应用层中被mapped_mem取代，Linux驱动中被mem取代. RTOS存放有效数据 */
};


int conn_fs_file_record_ops_posix_lseek(int fd, off_t offset);
int conn_fs_file_record_ops_posix_write(int fd, off_t offset);
int conn_fs_file_record_ops_posix_read(int fd, off_t offset);
int conn_fs_file_record_ops_posix_close(int fd);
int conn_fs_file_record_ops_posix_open(const char *file, int flags, mode_t mode, int fd);

off_t conn_fs_ops_posix_lseek(int fd, off_t offset, int whence);
int conn_fs_ops_posix_write(int fd, const void *buf, size_t len);
int conn_fs_ops_posix_read(int fd, const void *buf, size_t len);
int conn_fs_ops_posix_close(int fd);
int conn_fs_ops_posix_open(const char *file, int flags, mode_t mode);

int conn_fs_ops_inited(void);

#ifdef __cplusplus
}
#endif

#endif
