/*
 * RTOS 和 Linux 共用存储分区，RTOS中文件系统挂载在ramdisk中,没有存储介质的写入操作，所有修改存储介质相关的操作均在Linux端实现
 *
 * 该驱动实现依赖于RTOS端 ramdisk模拟存储介质
 *
 * RTOS端 擦除/写入等修改存储介质相关操作 通过核间通讯发送给Linux端,由Linux执行相关操作
 * RTOS端 读取/查询等获取存储介质相关操作 通过核间通讯发送给Linux端,由Linux执行相关操作，更新到RTOS端指定的内存空间中
 */

#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/conn.h>
#include <driver/ramdisk.h>
#include <dfs_posix_conn_fs.h>

#ifndef CONFIG_RAMDISK_DEVICE
#error "conn fs driver must select macro CONFIG_RAMDISK_DEVICE"
#endif

#ifndef CONFIG_DFS_ELMFAT
#error "conn fs driver must select macro CONFIG_DFS_ELMFAT"
#endif

#define RTOS_CPU_ID                     1

#define NOTIFY_WRITE_TIMEOUT_MS         10
#define NOTIFY_READ_TIMEOUT_MS          500

uint32_t ramdisk_device_dfs_elm_block_size(void);
struct ramdisk_block *ramdisk_devices_info(void);

enum conn_fs_cmd {
    CMD_conn_fs_get_posix_service_status,
    CMD_conn_fs_ops_posix_ioctl,

    CMD_conn_fs_conn_detect,
};


struct conn_fs_notify {
    enum conn_fs_cmd cmd;
    void *data;
};

struct conn_fs_data {
    char *device_name;
    uint32_t size;
    uint32_t start_addr;
    struct conn_node *conn;
    struct conn_fs_notify *notify;
    int service_running;
};


static const char *conn_cmd_str[] = {
    [CMD_conn_fs_get_posix_service_status] = "conn_fs_get_posix_service_status",
    [CMD_conn_fs_ops_posix_ioctl] = "conn_fs_ops_posix_ioctl",

    [CMD_conn_fs_conn_detect] = "conn_fs_conn_detect",
};

static struct conn_fs_data conn_fs_dev = {
    .device_name            = "conn_fs",
};


static inline void m_p_err(const char *func, int err)
{
    printf("CONN_FS: %s. send notify: %d.(CPU%d)\n", func, err, RTOS_CPU_ID);
}

static int conn_fs_send_cmd(struct conn_fs_data *drv, enum conn_fs_cmd cmd, void *data, void *res)
{
    struct conn_fs_notify notify;
    int notify_len = sizeof(notify);
    int ret;

    notify.cmd = cmd;
    notify.data = data;

    ret = conn_write(drv->conn, &notify, notify_len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != notify_len) {
        printf("CONN_FS: %s. failed to write conn ret: %d.(CPU%d)\n", conn_cmd_str[cmd], ret, RTOS_CPU_ID);
        return -1;
    }

    ret = conn_read(drv->conn, &notify, notify_len, NOTIFY_READ_TIMEOUT_MS);
    if (ret != notify_len) {
        printf("CONN_FS: %s. failed to read conn ret: %d.(CPU%d)\n", conn_cmd_str[cmd], ret, RTOS_CPU_ID);
        return -1;
    }

    if (res && notify.data)
        memcpy(res, notify.data, sizeof(struct conn_fs_posix_cmds_result));

    return 0;
}

static void soc_conn_fs_update_service_status(struct conn_fs_data *drv, int status)
{
    if (drv) {
        drv->service_running = status;
        printf("conn fs service status:%s\n", status ? "connect" : "disconnect");
    }
}


static int soc_conn_fs_get_posix_service_status(struct conn_fs_data *drv)
{
    int status;

    int ret = conn_fs_send_cmd(drv, CMD_conn_fs_get_posix_service_status, (void *)&status, NULL);
    if (ret < 0)
        m_p_err(__func__, ret);

    return status;
}


static int conn_fs_conn_detect(struct conn_node *conn, struct conn_fs_data *drv)
{
    struct conn_fs_notify notify;
    int notify_len = sizeof(notify);
    int ret;

    struct ramdisk_block *rdev = ramdisk_devices_info();
    if (rdev) {
        drv->start_addr = (uint32_t)rdev->start_addr;
        drv->size = rdev->size;
    } else {
        printf("ramdisk devices info is invalid\n");
        printf("CONN_FS: %s. ramdisk devices info is invalid.(CPU%d)\n", conn_cmd_str[CMD_conn_fs_conn_detect], RTOS_CPU_ID);
        return -EINVAL;
    }

    notify.cmd = CMD_conn_fs_conn_detect;
    notify.data = (void *)drv->start_addr;

    ret = conn_write(conn, &notify, notify_len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != notify_len) {
        printf("CONN_FS: %s. failed to write conn ret: %d.(CPU%d)\n", conn_cmd_str[notify.cmd], ret, RTOS_CPU_ID);
        return -1;
    }

    ret = conn_read(conn, &notify, notify_len, NOTIFY_READ_TIMEOUT_MS);
    if (ret != notify_len) {
        printf("CONN_FS: %s. failed to read conn ret: %d.(CPU%d)\n", conn_cmd_str[notify.cmd], ret, RTOS_CPU_ID);
        return -1;
    }

    return 0;
}


static void conn_fs_drv_init(void)
{
    struct conn_fs_data *drv = &conn_fs_dev;
    struct conn_node *conn;
    int count = 180;
    int ret = 0;

    conn = conn_request(drv->device_name, sizeof(struct conn_fs_notify));
    if (!conn)
        panic("CONN_FS: conn_fs request conn(%s) err. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);

    while (count--) {
        /* 等待Linux系统启动完成,创建conn链接 */
        ret = conn_fs_conn_detect(conn, drv);
        if (ret == 0)
            break;

        mdelay(500);
    }

    if (ret < 0) {
        printf("\n");
        printf("CONN_FS: conn_fs(%s) detect timeout. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);
        return ;
    }

    drv->conn = conn;

    ret = soc_conn_fs_get_posix_service_status(drv);
    soc_conn_fs_update_service_status(drv, ret);

    printf("CONN_FS: conn_fs(%s) detect conn successed. service status(%s). (CPU%d)\n",    \
            drv->device_name, ret ? "running":"not ready", RTOS_CPU_ID);
}

static void conn_fs_init_thread(void *data)
{
    conn_fs_drv_init();
}

void soc_conn_fs_init(void)
{
    thread_create("conn fs init thread", 4096, conn_fs_init_thread, NULL);
}

/*****************************************************************************/

/*
 * conn_fs driver 是否连接成功
 * return: =1  连接成功
 *         =0  未创建连接/连接失败
 */
int soc_conn_fs_inited(void)
{
    struct conn_fs_data *drv = &conn_fs_dev;

    /* conn 未建立链接 */
    if (!drv->conn)
        return 0;

    /* 已经查询过Linux 端conn_fs service 状态 */
    if (drv->service_running)
        return 1;

    int status = soc_conn_fs_get_posix_service_status(drv);

    soc_conn_fs_update_service_status(drv, status);

    return status;
}


/*
 * filesystem/dfs_posix.c
 * filesystem/conn_fs_posix/dfs_posix_conn_fs.c
 * RTOS实现文件的读写, 文件兼容posix接口的操作通过该接口发送到Linux端，在Linux端实现兼容posix的具体操作
 * 并接收Linux端兼容posix操作的返回值,已经执行操作的内容
 */
int soc_conn_fs_ops_posix_ioctl(uint32_t length, void *args, void *res)
{
    struct conn_fs_data *drv = &conn_fs_dev;
    int ret;

    ret = conn_fs_send_cmd(drv, CMD_conn_fs_ops_posix_ioctl, args, res);
    if (ret < 0) {
        m_p_err(__func__, ret);
        soc_conn_fs_update_service_status(drv, 0);
    } else
        ret = length;

    return ret;
}