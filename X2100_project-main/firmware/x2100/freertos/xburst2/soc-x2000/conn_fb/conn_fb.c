#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/conn.h>
#include <stdio.h>
#include <os.h>
#include <driver/camera.h>
#include <common.h>
#include <driver/fb.h>

#define RTOS_CPU_ID 1
#define NOTIFY_WRITE_TIMEOUT_MS 1000
#define NOTIFY_READ_TIMEOUT_MS  5000

enum conn_cmd {
    CMD_conn_fb_conn_detect,
    CMD_conn_fb_get_info,
    CMD_conn_fb_enable,
    CMD_conn_fb_pan_display,
    CMD_conn_fb_set_cfg,
    CMD_conn_fb_enable_config,
    CMD_conn_fb_disable_config,
    CMD_conn_fb_conn_inited,
    CMD_conn_fb_read_reg,
};

struct conn_fb_notify {
    enum conn_cmd cmd;
    void *data;
};

struct conn_fb_data {
    char device_name[20];
    struct fb_info info;
    struct fb_handle *fb;
    struct conn_node *conn;
    struct conn_fb_notify *notify;
};

static struct conn_fb_data conn_fb_dev[5];

static int conn_fb_send_cmd(struct conn_fb_data *drv, enum conn_cmd cmd, void *data)
{
    struct conn_fb_notify notify;
    int len = sizeof(notify);

    notify.cmd = cmd;
    notify.data = data;

    int ret = conn_write(drv->conn, &notify, len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != len)
        return -1;

    return 0;
}

static int soc_conn_fb_get_info(struct conn_fb_data *drv)
{
    fb_get_info(drv->fb, &drv->info);

    int ret = conn_fb_send_cmd(drv, CMD_conn_fb_get_info, (void *)&drv->info);
    if (ret < 0)
        printf("CONN_FB: %s. send notify: ret=%d.(CPU%d)\n", __func__, ret, RTOS_CPU_ID);

    return ret;
}

static int soc_conn_fb_set_cfg(struct conn_fb_data *drv, struct lcdc_layer *cfg)
{
    fb_set_config(drv->fb, cfg);

    int ret = conn_fb_send_cmd(drv, CMD_conn_fb_set_cfg, NULL);
    if (ret < 0)
        printf("CONN_FB: %s. send notify: ret=%d.(CPU%d)\n", __func__, ret, RTOS_CPU_ID);

    return ret;
}

static int soc_conn_fb_enable(struct conn_fb_data *drv)
{
    fb_enable(drv->fb);

    int ret = conn_fb_send_cmd(drv, CMD_conn_fb_enable, NULL);
    if (ret < 0)
        printf("CONN_FB: %s. send notify: ret=%d.(CPU%d)\n", __func__, ret, RTOS_CPU_ID);

    return ret;
}

static int soc_conn_fb_pan_display(struct conn_fb_data *drv, int *frame_index)
{
    fb_pan_display(drv->fb, *frame_index);

    int ret = conn_fb_send_cmd(drv, CMD_conn_fb_pan_display, NULL);
    if (ret < 0)
        printf("CONN_FB: %s. send notify: ret=%d.(CPU%d)\n", __func__, ret, RTOS_CPU_ID);

    return ret;
}

static int soc_conn_fb_enable_config(struct conn_fb_data *drv)
{
    fb_enable_config(drv->fb);

    int ret = conn_fb_send_cmd(drv, CMD_conn_fb_enable_config, NULL);
    if (ret < 0)
        printf("CONN_FB: %s. send notify: ret=%d.(CPU%d)\n", __func__, ret, RTOS_CPU_ID);

    return ret;
}

static int soc_conn_fb_disable_config(struct conn_fb_data *drv)
{
    fb_disable_config(drv->fb);

    int ret = conn_fb_send_cmd(drv, CMD_conn_fb_disable_config, NULL);
    if (ret < 0)
        printf("CONN_FB: %s. send notify: ret=%d.(CPU%d)\n", __func__, ret, RTOS_CPU_ID);

    return ret;
}

static void conn_fb_notify_process(void *data)
{
    int ret;
    struct conn_fb_notify notify;
    int len = sizeof(notify);
    int index = (int)data;

    struct conn_fb_data *drv = &conn_fb_dev[index];

    while (1) {
        ret = conn_read(drv->conn, &notify, len, NOTIFY_READ_TIMEOUT_MS);
        if (ret != len) {
            thread_yield();
            continue;
        }

        switch (notify.cmd)  {
            case CMD_conn_fb_get_info:
                soc_conn_fb_get_info(drv);
                break;
            case CMD_conn_fb_set_cfg:
                soc_conn_fb_set_cfg(drv, notify.data);
                break;
            case CMD_conn_fb_enable:
                soc_conn_fb_enable(drv);
                break;
            case CMD_conn_fb_pan_display:
                soc_conn_fb_pan_display(drv, notify.data);
                break;
            case CMD_conn_fb_enable_config:
                soc_conn_fb_enable_config(drv);
                break;
            case CMD_conn_fb_disable_config:
                soc_conn_fb_disable_config(drv);
                break;
            case CMD_conn_fb_conn_detect:
            case CMD_conn_fb_read_reg:
                break;
            default:
                break;
        }
    }
}

static void conn_fb_drv_init(int index)
{
    struct conn_fb_data *drv = &conn_fb_dev[index];

    drv->conn = conn_request(drv->device_name, sizeof(struct conn_fb_notify));
    if (!drv->conn)
        panic("CONN_FB: conn_fb request conn(%s) err. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);

    thread_create("conn_fb recv notify thread", 4096, conn_fb_notify_process, (void*)index);
}


static void conn_fb_init_thread(void *data)
{
    int index = 0;
    struct list_head *pos;
    struct list_head *list = fb_get_device_list();

    list_for_each(pos, list) {
        struct fb_handle *data = list_entry(pos, struct fb_handle, node);
        if (data) {
            sprintf(conn_fb_dev[index].device_name, "conn_%s", data->name);
            conn_fb_drv_init(index);
            struct fb_handle *fb = fb_open(data->name);
            if(!fb)
                printf("drv->fb = NULL\n");

            conn_fb_dev[index].fb = fb;
        }
        index++;
    }

    return;
}


void soc_conn_fb_init(void)
{
    thread_create("conn fb init thread", 4096, conn_fb_init_thread, NULL);
}


