#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/conn.h>
#include <stdio.h>
#include <common.h>

struct mmc_card *mmc_devices_info(void);
uint32_t mmc_device_block_read(uint64_t address, uint32_t length, void *buffer);
uint32_t mmc_device_block_write(uint64_t address, uint32_t length, void *buffer);
int mmc_device_block_erase(uint64_t address, uint32_t length);

#define RTOS_CPU_ID 1

#define NOTIFY_WRITE_TIMEOUT_MS         10
#define NOTIFY_READ_TIMEOUT_MS          500

enum conn_cmd {
    CMD_conn_blk_get_info,
    CMD_conn_blk_read,
    CMD_conn_blk_write,
    CMD_conn_blk_erase,

    CMD_conn_blk_conn_detect,
};

struct conn_blk_notify {
    enum conn_cmd cmd;
    int offset;
    int length;
    void *data;
};

struct conn_blk_data {
    char *device_name;
    int size;
    int start_addr;
    struct conn_node *conn;
    struct conn_blk_notify *notify;
};

static struct conn_blk_data conn_blk_dev = {
    .device_name            = "conn_blk",
    .start_addr             = CONFIG_X2000_CONN_BLK_START_ADDR,
    .size                   = CONFIG_X2000_CONN_BLK_SIZE,
};

static inline void m_p_err(const char *func, int err)
{
    printf("CONN_BLK: %s. send notify: %d.(CPU%d)\n", func, err, RTOS_CPU_ID);
}

static int conn_blk_send_cmd(struct conn_blk_data *drv, enum conn_cmd cmd, void *data)
{
    struct conn_blk_notify notify;
    int len = sizeof(notify);

    notify.cmd = cmd;
    notify.data = data;

    int ret = conn_write(drv->conn, &notify, len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != len)
        return -1;

    return 0;
}

static int check_blk_rw_size(int start, int offset, int length, int size)
{
    if (start + offset + length > size)
        return -EINVAL;

    return 0;
}

static void soc_conn_blk_get_info(struct conn_blk_data *drv)
{
    struct mmc_card *card_info = mmc_devices_info();

    int ret = conn_blk_send_cmd(drv, CMD_conn_blk_get_info, (void *)card_info);
    if (ret < 0)
        m_p_err(__func__, ret);
}

static void soc_conn_blk_mmc_block_read(struct conn_blk_data *drv,
                                                int offset,
                                                int length,
                                                void *buffer)
{
    int ret;
    int start = drv->start_addr;

    ret = check_blk_rw_size(start, offset, length, drv->size);
    if (ret < 0) {
        printf("CONN_BLK: failed to read mmc block. out of mmc_blk size. (CPU%d)\n", RTOS_CPU_ID);
        return;
    }

    ret = mmc_device_block_read(start + offset, length, buffer);
    if (ret < 0) {
        printf("CONN_BLK: failed to read mmc block. (CPU%d)\n", RTOS_CPU_ID);
        return;
    }

    ret = conn_blk_send_cmd(drv, CMD_conn_blk_read, NULL);
    if (ret < 0)
        m_p_err(__func__, ret);

}

static void soc_conn_blk_mmc_block_write(struct conn_blk_data *drv,
                                                int offset,
                                                int length,
                                                void *buffer)
{
    int start = drv->start_addr;

    int ret = check_blk_rw_size(start, offset, length, drv->size);
    if (ret < 0) {
        printf("CONN_BLK: failed to write mmc block. out of mmc_blk size. (CPU%d)\n", RTOS_CPU_ID);
        return;
    }

    ret = mmc_device_block_write(start + offset, length, buffer);
    if (ret < 0) {
        printf("CONN_BLK: failed to write mmc block. (CPU%d)\n", RTOS_CPU_ID);
        return;
    }

    ret = conn_blk_send_cmd(drv, CMD_conn_blk_write, NULL);
    if (ret < 0)
        m_p_err(__func__, ret);
}

static void soc_conn_blk_mmc_block_erase(struct conn_blk_data *drv,
                                                int offset,
                                                int length)
{
    int start = drv->start_addr;

    int ret = check_blk_rw_size(start, offset, length, drv->size);
    if (ret < 0) {
        printf("CONN_BLK: failed to erase mmc block. out of mmc_blk size. (CPU%d)\n", RTOS_CPU_ID);
        return;
    }

    ret = mmc_device_block_erase(start + offset, length);
    if (ret < 0) {
        printf("CONN_BLK: failed to erase mmc block. (CPU%d)\n", RTOS_CPU_ID);
        return;
    }

    ret = conn_blk_send_cmd(drv, CMD_conn_blk_erase, NULL);
    if (ret < 0)
        m_p_err(__func__, ret);
}

static void conn_blk_conn_detect(struct conn_blk_data *drv)
{
    struct conn_blk_notify notify;
    int len = sizeof(notify);

    notify.cmd = CMD_conn_blk_conn_detect;
    notify.data = (void *)drv->start_addr;
    notify.length = drv->size;

    int ret = conn_write(drv->conn, &notify, len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != len)
        m_p_err(__func__, ret);
}

static void conn_blk_notify_process(void *data)
{
    int ret;
    struct conn_blk_notify notify;
    int len = sizeof(notify);

    struct conn_blk_data *drv = &conn_blk_dev;

    while (1) {
        ret = conn_read(drv->conn, &notify, len, NOTIFY_READ_TIMEOUT_MS);
        if (ret != len) {
            thread_yield();
            continue;
        }

        int offset = notify.offset;
        int length = notify.length;

        // printf("CONN_BLK: offset = %d, length = %d. (CPU%d)\n", offset, length, RTOS_CPU_ID);
        switch (notify.cmd) {
            case CMD_conn_blk_get_info:
                soc_conn_blk_get_info(drv);
                break;
            case CMD_conn_blk_read:
                soc_conn_blk_mmc_block_read(drv, offset, length, notify.data);
                break;
            case CMD_conn_blk_write:
                soc_conn_blk_mmc_block_write(drv, offset, length, notify.data);
                break;
            case CMD_conn_blk_erase:
                soc_conn_blk_mmc_block_erase(drv, offset, length);
                break;
            case CMD_conn_blk_conn_detect:
                conn_blk_conn_detect(drv);
                break;
            default:
                break;
        }
    }
}

static void conn_blk_drv_init(void)
{
    struct conn_blk_data *drv = &conn_blk_dev;

    drv->conn = conn_request(drv->device_name, sizeof(struct conn_blk_notify));
    if (!drv->conn)
        panic("CONN_BLK: conn_blk request conn(%s) err. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);

    thread_create("conn_blk recv notify thread", 4096, conn_blk_notify_process, NULL);
}

static void conn_blk_init_thread(void *data)
{
    conn_blk_drv_init();
}

void soc_conn_blk_init(void)
{
    thread_create("conn blk init thread", 4096, conn_blk_init_thread, NULL);
}