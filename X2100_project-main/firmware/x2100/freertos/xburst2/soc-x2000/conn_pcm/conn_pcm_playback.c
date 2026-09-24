#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/conn.h>

#include <driver/pcm.h>
#include <driver/gpio.h>
#include <driver/pcm_adapter.h>

#define RTOS_CPU_ID 1
#define NOTIFY_WRITE_TIMEOUT_MS 10
#define NOTIFY_READ_TIMEOUT_MS  1000
#define CONN_PCM_PLAYBACK_DEV_MAX_COUNT 1

struct pcm_data_info {
    void *data;
    int size;
};

struct pcm_param_info {
    int channel;
    pcm_data_fmt data_fmt;
    pcm_sample_rate sample_rate;
    int volume;
    int frame_byte;
    int per_frame_size;
    int count;
};

struct pcm_info {
    struct pcm_data_info pcm_data;
    struct pcm_param_info pcm_param;
};

enum conn_cmd {
    CMD_conn_pcm_playback_alloc_mem,
    CMD_conn_pcm_playback_free_mem,
    CMD_conn_pcm_playback_enable,
    CMD_conn_pcm_playback_disable,
    CMD_conn_pcm_playback_write_frame,
    CMD_conn_pcm_playback_set_param,
    CMD_conn_pcm_playback_detect,
};

struct conn_pcm_playback_notify {
    enum conn_cmd cmd;
    void *data;
};

struct conn_pcm_playback_data {
    int index;
    int is_enable;
    char *device_name;
    char *playback_name;
    char *icodec_playback_name;

    struct pcm_params playback_params;
    struct pcm_adapter *adapter;
    int volume;

    struct conn_node *conn;
    struct conn_pcm_playback_notify *notify;
};

static struct conn_pcm_playback_data conn_pcm_playback_dev[CONN_PCM_PLAYBACK_DEV_MAX_COUNT] = {
    {
        .index                  = 0,
        .is_enable              = 1,
        .device_name            = "conn_pcm_icodec_playback",
        .playback_name            = "aic0-playback",
        .icodec_playback_name            = "icodec-playback",

        .playback_params = {
            .channels = 1,
            .pcm_data_fmt = pcm_fmt_S16LE,
            .pcm_sample_rate = pcm_rate_16000,
            .pcm_interface = pcm_interface_i2s,
            .i2s_frame_mode = i2s_LR_mode,
            .i2s_bclk_direction = i2s_bclk_codec_master,
            .i2s_frame_direction = i2s_frame_codec_master,
        },
    },
};

static int conn_pcm_playback_send_cmd(struct conn_pcm_playback_data *drv, enum conn_cmd cmd, void *data)
{
    struct conn_pcm_playback_notify notify;
    int len = sizeof(notify);

    notify.cmd = cmd;
    notify.data = data;

    // printf("CONN_PCM_PLAYBACK[%d]: %s. send notify.(CPU%d)\n", drv->index, __func__, RTOS_CPU_ID);
    int ret = conn_write(drv->conn, &notify, len, NOTIFY_WRITE_TIMEOUT_MS);
    if (ret != len)
        return -1;

    return 0;
}

static int soc_conn_pcm_playback_enable(struct conn_pcm_playback_data *drv)
{
    struct pcm_device *dai = pcm_get(drv->playback_name);
    struct pcm_device *codec = pcm_get(drv->icodec_playback_name);

    pcm_private_ctrl(dai, "sysclk-set-rate", 256*16000);
    pcm_private_ctrl(dai, "sysclk-set-output", 1);

/* 配置适配器，这里填写音频的相关参数 */
    struct pcm_adapter_param adapter_param;
    adapter_param.channels = drv->playback_params.channels;
    adapter_param.data_fmt = drv->playback_params.pcm_data_fmt;
    adapter_param.sample_rate = drv->playback_params.pcm_sample_rate;
    /* 创建适配器 */
    drv->adapter = pcm_adapter_create(dai, &adapter_param);

    struct pcm_params icodec_playback_params = {
        .channels = 1,      //  icodec channels must set 1
        .pcm_data_fmt = drv->playback_params.pcm_data_fmt,
        .pcm_sample_rate = drv->playback_params.pcm_sample_rate,
        .pcm_interface = drv->playback_params.pcm_interface,
        .i2s_frame_mode = drv->playback_params.i2s_frame_mode,
        .i2s_bclk_direction = drv->playback_params.i2s_bclk_direction,
        .i2s_frame_direction = drv->playback_params.i2s_frame_direction,
    };

    pcm_enable(codec, &icodec_playback_params);
    pcm_enable(dai, &drv->playback_params);

    pcm_start(codec);
    pcm_start(dai);

    if (drv->volume != 0)
        pcm_set_volume(codec, drv->volume);
    else
        pcm_set_volume(codec, 50);

    int ret = conn_pcm_playback_send_cmd(drv, CMD_conn_pcm_playback_enable, NULL);
    if (ret < 0){
        printf("CONN_PCM_PLAYBACK[%d]: %s. send notify: %d.(CPU%d)\n", drv->index, __func__, ret, RTOS_CPU_ID);
        return -1;
    }

    return 0;
}

static int soc_conn_pcm_playback_disable(struct conn_pcm_playback_data *drv)
{
    struct pcm_device *dai = pcm_get(drv->playback_name);
    struct pcm_device *codec = pcm_get(drv->icodec_playback_name);

    pcm_disable(dai);
    pcm_disable(codec);

    /* 释放适配器 */
    pcm_adapter_release(drv->adapter);

    int ret = conn_pcm_playback_send_cmd(drv, CMD_conn_pcm_playback_disable, NULL);
    if (ret < 0){
        printf("CONN_PCM_PLAYBACK[%d]: %s. send notify: %d.(CPU%d)\n", drv->index, __func__, ret, RTOS_CPU_ID);
        return -1;
    }

    return 0;
}

static int soc_conn_pcm_playback_write_frame(struct conn_pcm_playback_data *drv, struct pcm_data_info  *data_info)
{
    struct pcm_adapter *adapter = drv->adapter;

    int frame_size = pcm_data_sample_size(drv->playback_params.pcm_data_fmt) * drv->playback_params.channels;

    pcm_adapter_write_frame(adapter, (void *)data_info->data, data_info->size/frame_size);

    int ret = conn_pcm_playback_send_cmd(drv, CMD_conn_pcm_playback_write_frame, NULL);
    if (ret < 0){
        printf("CONN_PCM_PLAYBACK[%d]: %s. send notify: %d.(CPU%d)\n", drv->index, __func__, ret, RTOS_CPU_ID);
        return -1;
    }

    return 0;
}

static int soc_conn_pcm_playback_set_param(struct conn_pcm_playback_data *drv, struct pcm_param_info *param)
{
    drv->playback_params.channels = param->channel;
    drv->playback_params.pcm_data_fmt = param->data_fmt;
    drv->playback_params.pcm_sample_rate = param->sample_rate;

    if (param->volume != 0)
        drv->volume = param->volume;

    int ret = conn_pcm_playback_send_cmd(drv, CMD_conn_pcm_playback_set_param, NULL);
    if (ret < 0) {
        printf("CONN_PCM_PLAYBACK[%d]: %s. send notify: %d.(CPU%d)\n", drv->index, __func__, ret, RTOS_CPU_ID);
        return -1;
    }

    return 0;
}

static void conn_pcm_playback_notify_process(void *data)
{
    int ret;
    struct conn_pcm_playback_notify notify;
    int len = sizeof(notify);
    int index = (int)data;

    struct conn_pcm_playback_data *drv = &conn_pcm_playback_dev[index];

    while (1) {
        ret = conn_read(drv->conn, &notify, len, NOTIFY_READ_TIMEOUT_MS);
        if (ret != len) {
            thread_yield();
            continue;
        }

        switch (notify.cmd) {
            case CMD_conn_pcm_playback_enable:
                soc_conn_pcm_playback_enable(drv);
                break;

            case CMD_conn_pcm_playback_disable:
                soc_conn_pcm_playback_disable(drv);
                break;

            case CMD_conn_pcm_playback_write_frame:
                soc_conn_pcm_playback_write_frame(drv, notify.data);
                break;

            case CMD_conn_pcm_playback_set_param:
                soc_conn_pcm_playback_set_param(drv, notify.data);
                break;

            case CMD_conn_pcm_playback_detect:
                break;

            default:
                break;
        }
    }
}

static void conn_pcm_playback_drv_init(int index)
{
    struct conn_pcm_playback_data *drv = &conn_pcm_playback_dev[index];

    drv->conn = conn_request(drv->device_name, sizeof(struct conn_pcm_playback_notify));
    if (!drv->conn)
        panic("CONN_PCM_PLAYBACK: conn_pcm_playback request conn(%s) err. (CPU%d)\n", drv->device_name, RTOS_CPU_ID);

    thread_create("conn_pcm_playback recv notify thread", 4096, conn_pcm_playback_notify_process, (void *)index);

}

static void conn_pcm_playback_init_thread(void * data)
{
    if (conn_pcm_playback_dev[0].is_enable)
        conn_pcm_playback_drv_init(0);
}

void soc_conn_pcm_playback_init(void)
{
    thread_create("conn pcm playback init thread", 4096, conn_pcm_playback_init_thread, NULL);
}
