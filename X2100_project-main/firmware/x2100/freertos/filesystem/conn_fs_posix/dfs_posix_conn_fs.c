#include <dfs.h>
#include <dfs_posix.h>
#include <dfs_posix_conn_fs.h>

//#define CONFIG_CONN_FS
//#define CONFIG_CONN_FS_CMDS_DEBUG

#include "conn_fs_posix_file_info_record.c"
#include "conn_fs_posix_open.c"
#include "conn_fs_posix_close.c"
#include "conn_fs_posix_read.c"
#include "conn_fs_posix_write.c"
#include "conn_fs_posix_lseek.c"

#ifdef CONFIG_CONN_FS

#ifdef CONN_FS_CMDS_TAG
#undef CONN_FS_CMDS_TAG
#endif

#define CONN_FS_CMDS_TAG                "conn_fs_cmds_result:"

#define CONN_FS_CMDS_RESULT_MAX_LEN     (1 * 1024 * 1024)

int soc_conn_fs_inited(void);

#ifdef CONFIG_CONN_FS_CMDS_DEBUG
static void conn_fs_posix_rtos_result(struct conn_fs_posix_cmds_result *res)
{
    printf(CONN_FS_CMDS_TAG "res address       : %p\n", res);
    printf(CONN_FS_CMDS_TAG "res->index        : 0x%x\n", res->cmd_index);
    printf(CONN_FS_CMDS_TAG "res->res_val      : 0x%x\n", res->res_val);
    printf(CONN_FS_CMDS_TAG "res->result_len   : 0x%x\n", res->result_len);
    printf(CONN_FS_CMDS_TAG "res->result       : 0x%p\n", res->result);

    if (res->result_len > CONN_FS_CMDS_RESULT_MAX_LEN) {
        printf(CONN_FS_CMDS_TAG "res->resule_len(0x%x) may too long, not dump result.\n", res->result_len);
        return ;
    }

    unsigned char *buf = res->result;
    int i = 0;
    for (i = 0; i < res->result_len; i++) {
        if ( i != 0 && i % 16 == 0)
            printf("\n");

        printf("%02x:", buf[i]);
    }
    printf("\n");
}
#endif

static void conn_fs_update_file_info(int status)
{
    static int service_status = 0;

    if (service_status != status && status)
        conn_fs_update_file_node_to_linux();

    service_status = status;
}

int conn_fs_ops_inited(void)
{
    int status = soc_conn_fs_inited();

    conn_fs_update_file_info(status);

    return status;
}

#else

int conn_fs_ops_inited(void)
{
    return 0;
}

#endif
