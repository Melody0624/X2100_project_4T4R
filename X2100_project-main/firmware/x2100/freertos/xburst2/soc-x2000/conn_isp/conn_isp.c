#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/conn.h>
#include <stdio.h>
#include <os.h>
#include <driver/camera_isp.h>
#include <driver/isp_tuning.h>
#include <common.h>

#define RTOS_CPU_ID 1

#define NOTIFY_WRITE_TIMEOUT_MS 10
#define NOTIFY_READ_TIMEOUT_MS  500

enum conn_cmd {
    CMD_conn_isp_get_info,
    CMD_conn_isp_power_on,
    CMD_conn_isp_power_off,
    CMD_conn_isp_stream_on,
    CMD_conn_isp_stream_off,
    CMD_conn_isp_set_format,
    CMD_conn_isp_get_format,
    CMD_conn_isp_request_buffer,
    CMD_conn_isp_free_buffer,
    CMD_conn_isp_get_max_scaler_size,
    CMD_conn_isp_get_line_align_size,

    CMD_conn_isp_wait_frame,
    CMD_conn_isp_get_frame,
    CMD_conn_isp_put_frame,
    CMD_conn_isp_get_frame_count,
    CMD_conn_isp_skip_frames,

    CMD_conn_isp_get_sensor_info,
    CMD_conn_isp_get_sensor_reg,
    CMD_conn_isp_set_sensor_reg,

    CMD_conn_isp_conn_detect,

    CMD_conn_isp_dqbuf,
    CMD_conn_isp_dqbuf_wait,
    CMD_conn_isp_qbuf,
};

#define MSCALER_MAX_CH 3

struct conn_isp_notify {
    enum conn_cmd cmd;
    int id;
    int mscaler_ch;
    void *data;
};

struct conn_isp_data {
    int index;
    int is_enable;
    char *device_name;
    camera_hd_t *mscaler_data[MSCALER_MAX_CH];

    struct mutex notify_mutex;
    struct conn_node *conn;
    struct conn_isp_notify notify;
    thread_waiter_t send_wait;
};

static struct conn_isp_data conn_isp_dev[2] = {
    {
        #ifdef CONFIG_X2000_CONN_ISP0
        .index                  = 0,
        .is_enable              = 1,
        .device_name            = "conn_isp0",
        #endif
    },

    {
        #ifdef CONFIG_X2000_CONN_ISP1
        .index                  = 1,
        .is_enable              = 1,
        .device_name            = "conn_isp1",
        #endif
    },
};

static inline void m_p_err(int index, int ch, const char *func, int err)
{
    printf("CONN_ISP[%d,%d]: %s. send notify: %d.(CPU%d)\n", index, ch, func, err, RTOS_CPU_ID);
}

static int conn_isp_send_cmd(struct conn_isp_data *drv, enum conn_cmd cmd, int id, void *data)
{
    struct conn_isp_notify notify;
    int len = sizeof(notify);

    notify.cmd = cmd;
    notify.data = data;
    notify.id = id;

    int ret = conn_write(drv->conn, &notify, len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != len)
        return -1;

    return 0;
}

static void soc_conn_isp_get_info(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    int data = 0;

    struct camera_info *info = isp_get_info(hd);
    if (!info) {
        printf("CONN_ISP[%d,%d]: failed to get camera info. (CPU%d)\n", drv->index, ch, RTOS_CPU_ID);
        data = -1;
    } else {
        data = (int)info;
    }

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_info, id, (void *)data);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_sensor_info(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    int data = 0;

    struct camera_info *info = isp_get_sensor_info(hd);
    if (!info) {
        printf("CONN_ISP[%d,%d]: failed to get sensor info. (CPU%d)\n", drv->index, ch, RTOS_CPU_ID);
        data = -1;
    } else {
        data = (int)info;
    }

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_sensor_info, id, (void *)data);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_sensor_reg(struct conn_isp_data *drv, int ch, int id, void *data)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    struct sensor_dbg_register *reg = (struct sensor_dbg_register *)data;

    int ret = isp_get_sensor_reg(hd, reg);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to get sensor reg. (CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_sensor_reg, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_set_sensor_reg(struct conn_isp_data *drv, int ch, int id, void *data)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    struct sensor_dbg_register *reg = (struct sensor_dbg_register *)data;

    int ret = isp_set_sensor_reg(hd, reg);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to set sensor reg. (CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_set_sensor_reg, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_power_on(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_power_on(hd);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to power on camera. (CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_power_on, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_power_off(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    isp_power_off(hd);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_power_off, id, NULL);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_stream_on(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_stream_on(hd);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to stream on isp.(CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_stream_on, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_stream_off(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    isp_stream_off(hd);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_stream_off, id, NULL);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_set_format(struct conn_isp_data *drv, int ch, int id, void *data)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_set_format(hd, (struct frame_image_format *)data);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to set isp format.(CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_set_format, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_format(struct conn_isp_data *drv, int ch, int id, void *data)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_get_format(hd, (struct frame_image_format *)data);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to get isp format.(CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_format, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_request_buffer(struct conn_isp_data *drv, int ch, int id, void *data)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    struct frame_image_format *fmt = (struct frame_image_format *)data;

    int ret = isp_request_buffer(hd, fmt);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to request buffer.(CPU%d)\n", drv->index, ch, RTOS_CPU_ID);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_request_buffer, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_free_buffer(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    isp_free_buffer(hd);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_free_buffer, id, NULL);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_max_scaler_size(struct conn_isp_data *drv, int ch, int id, void *data)
{
    int width, height;
    int *size = (int *)data;

    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_get_max_scaler_size(hd, &width, &height);
    if (ret < 0) {
        printf("CONN_ISP[%d,%d]: failed to get max scaler size(CPU%d)\n", drv->index, ch, RTOS_CPU_ID);
    } else {
        size[0] = width;
        size[1] = height;
    }

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_max_scaler_size, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_line_align_size(struct conn_isp_data *drv, int ch, int id, void *data)
{
    int align_size;

    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_get_line_align_size(hd, &align_size);
    if (ret < 0)
        printf("CONN_ISP[%d,%d]: failed to get max scaler size(CPU%d)\n", drv->index, ch, RTOS_CPU_ID);
    else
        *(int *)data = align_size;

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_line_align_size, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_wait_frame(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    void *buf = isp_wait_frame(hd);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_wait_frame, id, buf);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_frame(struct conn_isp_data *drv, int ch, int id)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    void *buf = isp_get_frame(hd);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_frame, id, buf);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_put_frame(struct conn_isp_data *drv, int ch, int id, void *mem)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    isp_put_frame(hd, mem);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_put_frame, id, NULL);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_get_frame_count(struct conn_isp_data *drv, int id, int ch)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret = isp_get_available_frame_count(hd);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_get_frame_count, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_skip_frames(struct conn_isp_data *drv, int ch, int id, unsigned int frames)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    isp_skip_frames(hd, frames);

    int ret = conn_isp_send_cmd(drv, CMD_conn_isp_skip_frames, id, NULL);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_dqbuf(struct conn_isp_data *drv, int ch, int id, struct frame_info *frame)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    int ret;

    ret = isp_dqbuf(hd, frame);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_dqbuf, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}


static void soc_conn_isp_dqbuf_wait(struct conn_isp_data *drv, int ch, int id, struct frame_info *frame)
{
    camera_hd_t *hd = drv->mscaler_data[ch];

    int ret;
    ret = isp_dqbuf_wait(hd, frame);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_dqbuf_wait, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}

static void soc_conn_isp_qbuf(struct conn_isp_data *drv, int ch, int id, struct frame_info *frame)
{
    camera_hd_t *hd = drv->mscaler_data[ch];
    int ret;

    ret = isp_qbuf(hd, frame);

    ret = conn_isp_send_cmd(drv, CMD_conn_isp_qbuf, id, (void *)ret);
    if (ret < 0)
        m_p_err(drv->index, ch, __func__, ret);
}


static void conn_isp_wait_frame_process(void *data)
{
    struct conn_isp_data *drv = data;
    struct conn_isp_notify notify;

    notify = drv->notify;
    mutex_unlock(&drv->notify_mutex);

    soc_conn_isp_wait_frame(drv, notify.mscaler_ch, notify.id);
}

static void conn_isp_dqbuf_wait_process(void *data)
{
    struct conn_isp_data *drv = data;
    struct conn_isp_notify notify;

    notify = drv->notify;
    mutex_unlock(&drv->notify_mutex);

    soc_conn_isp_dqbuf_wait(drv, notify.mscaler_ch, notify.id, notify.data);
}

static void conn_isp_recv_notify_process(void *data)
{
    int ret;
    struct conn_isp_notify notify;
    int len = sizeof(notify);
    int index = (int)data;

    struct conn_isp_data *drv = &conn_isp_dev[index];

    while (1) {
        ret = conn_read(drv->conn, &notify, len, NOTIFY_READ_TIMEOUT_MS);
        if (ret != len) {
            thread_yield();
            continue;
        }

        int ch = notify.mscaler_ch;
        int id = notify.id;

        switch (notify.cmd) {
        case CMD_conn_isp_get_info:
            soc_conn_isp_get_info(drv, ch, id);
            break;
        case CMD_conn_isp_power_on:
            soc_conn_isp_power_on(drv, ch, id);
            break;
        case CMD_conn_isp_power_off:
            soc_conn_isp_power_off(drv, ch, id);
            break;
        case CMD_conn_isp_stream_on:
            soc_conn_isp_stream_on(drv, ch, id);
            break;
        case CMD_conn_isp_stream_off:
            soc_conn_isp_stream_off(drv, ch, id);
            break;
        case CMD_conn_isp_set_format:
            soc_conn_isp_set_format(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_get_format:
            soc_conn_isp_get_format(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_request_buffer:
            soc_conn_isp_request_buffer(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_free_buffer:
            soc_conn_isp_free_buffer(drv, ch, id);
            break;
        case CMD_conn_isp_get_max_scaler_size:
            soc_conn_isp_get_max_scaler_size(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_get_line_align_size:
            soc_conn_isp_get_line_align_size(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_wait_frame:
            mutex_lock(&drv->notify_mutex);
            drv->notify = notify;

            thread_create("conn_isp ioctl thread", 4096, conn_isp_wait_frame_process, (void *)drv);
            break;
        case CMD_conn_isp_get_frame:
            soc_conn_isp_get_frame(drv, ch, id);
            break;
        case CMD_conn_isp_put_frame:
            soc_conn_isp_put_frame(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_get_frame_count:
            soc_conn_isp_get_frame_count(drv, ch, id);
            break;
        case CMD_conn_isp_skip_frames:
            soc_conn_isp_skip_frames(drv, ch, id, (unsigned int)notify.data);
            break;
        case CMD_conn_isp_get_sensor_info:
            soc_conn_isp_get_sensor_info(drv, ch, id);
            break;
        case CMD_conn_isp_get_sensor_reg:
            soc_conn_isp_get_sensor_reg(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_set_sensor_reg:
            soc_conn_isp_set_sensor_reg(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_conn_detect:
            break;
        case CMD_conn_isp_dqbuf:
            soc_conn_isp_dqbuf(drv, ch, id, notify.data);
            break;
        case CMD_conn_isp_dqbuf_wait:
            mutex_lock(&drv->notify_mutex);
            drv->notify = notify;
            thread_create("conn_isp ioctl thread", 4096, conn_isp_dqbuf_wait_process, (void *)drv);
            break;
        case CMD_conn_isp_qbuf:
            soc_conn_isp_qbuf(drv, ch, id, notify.data);
            break;
        default:
            break;
        }
    }
}

static void conn_isp_drv_init(int index)
{
    struct conn_isp_data *drv = &conn_isp_dev[index];

    drv->conn = conn_request(drv->device_name, sizeof(struct conn_isp_notify));
    if (!drv->conn)
        panic("CONN_ISP: conn_isp request conn(%s) err. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);

    int i;
    int is_detect = 0;
    for (i = 0; i < MSCALER_MAX_CH; i++) {
        camera_hd_t *hd = isp_detect(index, i);
        if (!hd)
            continue;

        drv->mscaler_data[i] = hd;
        is_detect = 1;
    }

    if (!is_detect)
        panic("CONN_ISP: camera(%d) not found. (CPU%d)\n", index, RTOS_CPU_ID);

    mutex_init(&drv->notify_mutex);

    thread_create("conn_isp recv notify thread", 4096, conn_isp_recv_notify_process, (void *)index);
}

static void conn_isp_init_thread(void *data)
{

    conn_wait_conn_inited();

    if (conn_isp_dev[0].is_enable)
        conn_isp_drv_init(0);

    if (conn_isp_dev[1].is_enable)
        conn_isp_drv_init(1);
}

void soc_conn_isp_init(void)
{
    thread_create("conn isp init thread", 4096, conn_isp_init_thread, NULL);
}