#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/conn.h>
#include <stdio.h>
#include <os.h>
#include <driver/camera.h>
#include <common.h>

#define RTOS_CPU_ID 1

#define NOTIFY_WRITE_TIMEOUT_MS 10
#define NOTIFY_READ_TIMEOUT_MS  500

enum conn_cmd {
    CMD_conn_vic_get_info,
    CMD_conn_vic_power_on,
    CMD_conn_vic_power_off,
    CMD_conn_vic_stream_on,
    CMD_conn_vic_stream_off,
    CMD_conn_vic_wait_frame,
    CMD_conn_vic_put_frame,
    CMD_conn_vic_get_frame_count,
    CMD_conn_vic_skip_frames,
    CMD_conn_vic_conn_detect,
};

struct conn_vic_notify {
    enum conn_cmd cmd;
    void *data;
};

struct conn_vic_data {
    int index;
    int is_enable;
    char *device_name;
    struct camera_device *camera;

    struct conn_node *conn;
    struct conn_vic_notify *notify;
};

static struct conn_vic_data conn_vic_dev[2] = {
    {
        #ifdef CONFIG_X2000_CONN_VIC0
        .index                  = 0,
        .is_enable              = 1,
        .device_name            = "conn_vic0",
        #endif
    },

    {
        #ifdef CONFIG_X2000_CONN_VIC1
        .index                  = 1,
        .is_enable              = 1,
        .device_name            = "conn_vic1",
        #endif
    },
};

static inline void m_p_err(int index, const char *func, int err)
{
    printf("CONN_VIC[%d]: %s. send notify: %d.(CPU%d)\n", index, func, err, RTOS_CPU_ID);
}

static int conn_vic_send_cmd(struct conn_vic_data *drv, enum conn_cmd cmd, void *data)
{
    struct conn_vic_notify notify;
    int len = sizeof(notify);

    notify.cmd = cmd;
    notify.data = data;

    int ret = conn_write(drv->conn, &notify, len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != len)
        return -1;

    return 0;
}

static void soc_conn_vic_get_info(struct conn_vic_data *drv)
{
    struct camera_info *info = camera_get_info(drv->camera);
    if (!info) {
        printf("CONN_VIC[%d]: failed to get camera info. (CPU%d)\n", drv->index, RTOS_CPU_ID);
        return;
    }

    int ret = conn_vic_send_cmd(drv, CMD_conn_vic_get_info, (void *)info);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_power_on(struct conn_vic_data *drv)
{
    int ret = camera_power_on(drv->camera);
    if (ret < 0) {
        printf("CONN_VIC[%d]: failed to power on camera. (CPU%d)\n", drv->index, RTOS_CPU_ID);
        return;
    }

    ret = conn_vic_send_cmd(drv, CMD_conn_vic_power_on, NULL);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_power_off(struct conn_vic_data *drv)
{
    camera_power_off(drv->camera);

    int ret = conn_vic_send_cmd(drv, CMD_conn_vic_power_off, NULL);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_stream_on(struct conn_vic_data *drv)
{
    int ret = camera_stream_on(drv->camera);
    if (ret < 0) {
        printf("CONN_VIC[%d]: failed to stream on camera.(CPU%d)\n", drv->index, RTOS_CPU_ID);
        return;
    }

    ret = conn_vic_send_cmd(drv, CMD_conn_vic_stream_on, NULL);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_stream_off(struct conn_vic_data *drv)
{
    camera_stream_off(drv->camera);

    int ret = conn_vic_send_cmd(drv, CMD_conn_vic_stream_off, NULL);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_wait_frame(struct conn_vic_data *drv)
{
    void *buf = camera_wait_frame(drv->camera);

    int ret = conn_vic_send_cmd(drv, CMD_conn_vic_wait_frame, buf);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_put_frame(struct conn_vic_data *drv, void *mem)
{
    camera_put_frame(drv->camera, mem);

    int ret = conn_vic_send_cmd(drv, CMD_conn_vic_put_frame, NULL);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_get_frame_count(struct conn_vic_data *drv)
{
    int ret = camera_get_available_frame_count(drv->camera);

    ret = conn_vic_send_cmd(drv, CMD_conn_vic_get_frame_count, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void soc_conn_vic_skip_frames(struct conn_vic_data *drv, unsigned int frames)
{
    camera_skip_frames(drv->camera, frames);

    int ret = conn_vic_send_cmd(drv, CMD_conn_vic_skip_frames, NULL);
    if (ret < 0)
        m_p_err(drv->index, __func__, ret);
}

static void conn_vic_notify_process(void *data)
{
    int ret;
    struct conn_vic_notify notify;
    int len = sizeof(notify);
    int index = (int)data;

    struct conn_vic_data *drv = &conn_vic_dev[index];

    while (1) {
        ret = conn_read(drv->conn, &notify, len, NOTIFY_READ_TIMEOUT_MS);
        if (ret != len) {
            thread_yield();
            continue;
        }

        switch (notify.cmd) {
            case CMD_conn_vic_get_info:
                soc_conn_vic_get_info(drv);
                break;
            case CMD_conn_vic_power_on:
                soc_conn_vic_power_on(drv);
                break;
            case CMD_conn_vic_power_off:
                soc_conn_vic_power_off(drv);
                break;
            case CMD_conn_vic_stream_on:
                soc_conn_vic_stream_on(drv);
                break;
            case CMD_conn_vic_stream_off:
                soc_conn_vic_stream_off(drv);
                break;
            case CMD_conn_vic_wait_frame:
                soc_conn_vic_wait_frame(drv);
                break;
            case CMD_conn_vic_put_frame:
                soc_conn_vic_put_frame(drv, notify.data);
                break;
            case CMD_conn_vic_get_frame_count:
                soc_conn_vic_get_frame_count(drv);
                break;
            case CMD_conn_vic_skip_frames:
                soc_conn_vic_skip_frames(drv, (unsigned int)notify.data);
                break;
            case CMD_conn_vic_conn_detect:
                break;
            default:
                break;

        }
    }
}

static void camera_detect_process(void *data)
{
    int index = (int)data;
    struct conn_vic_data *drv = &conn_vic_dev[index];

    drv->camera = camera_detect(index);
    if (!drv->camera)
        panic("CONN_VIC: camera(%d) not found. (CPU%d)\n", index, RTOS_CPU_ID);
}

static void conn_vic_drv_init(int index)
{
    struct conn_vic_data *drv = &conn_vic_dev[index];

    drv->conn = conn_request(drv->device_name, sizeof(struct conn_vic_notify));
    if (!drv->conn)
        panic("CONN_VIC: conn_vic request conn(%s) err. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);

    thread_create("camera_detect thread", 4096, camera_detect_process, (void *)index);

    thread_create("conn_vic recv notify thread", 4096, conn_vic_notify_process, (void *)index);
}

static void conn_vic_init_thread(void *data)
{
    if (conn_vic_dev[0].is_enable)
        conn_vic_drv_init(0);

    if (conn_vic_dev[1].is_enable)
        conn_vic_drv_init(1);
}

void soc_conn_vic_init(void)
{
    thread_create("conn vic init thread", 4096, conn_vic_init_thread, NULL);
}