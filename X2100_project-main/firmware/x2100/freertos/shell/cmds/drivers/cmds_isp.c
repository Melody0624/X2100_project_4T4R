
#include <shell.h>
#include <driver/camera_isp.h>
#include <common.h>
#include <printf.h>
#include <dump_mem.h>

static camera_hd_t *camera_hd;
static struct camera_info *info;
static struct frame_image_format output_fmt;

static void cmd_isp_dump(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    if (!camera_hd) {
        printf("camera not found\n");
        return;
    }

    isp_skip_frames(camera_hd, 0xffffffff);

    char *buf = isp_wait_frame(camera_hd);

    printf_disable_time_stamp();

    printf("frm buffer %p\n", buf);
    if (buf) {
        dump_mem32_c_style(buf, info->frame_size, 8);

        isp_put_frame(camera_hd, buf);
    }

    printf_enable_time_stamp();
}


static void isp_stream_start(camera_hd_t *camera_hd)
{
    struct camera_info *sensor_info;
    char *device_name;
    int ret;

    sensor_info = isp_get_sensor_info(camera_hd);
    assert(sensor_info != NULL);

    device_name = (char *)(camera_hd->ptr);

    output_fmt.pixel_format  = CAMERA_PIX_FMT_NV12;
    output_fmt.frame_nums    = 2;
    output_fmt.scaler.enable = 0;
    output_fmt.crop.enable   = 0;
    output_fmt.width         = sensor_info->width;
    output_fmt.height        = sensor_info->height;

    ret = isp_set_format(camera_hd, &output_fmt);
    if (ret < 0) {
        printf("%s set format failed\n", device_name);
        goto isp_set_fmt_error;
    }

    ret = isp_request_buffer(camera_hd, &output_fmt);
    if (ret < 0) {
        printf("%s requset buffer failed\n", device_name);
        goto isp_request_buf_error;
    }

    info = isp_get_info(camera_hd);

    char fmt_a = (char)(info->data_fmt >> 0);
    char fmt_b = (char)(info->data_fmt >> 8);
    char fmt_c = (char)(info->data_fmt >> 16);
    char fmt_d = (char)(info->data_fmt >> 24);
    printf("channel         = %s\n", device_name);
    printf("sensor_name     = %s\n", info->name);
    printf("width           = %d\n", info->width);
    printf("height          = %d\n", info->height);
    printf("fps             = %d\n", info->fps);
    printf("data_fmt        = %c%c%c%c\n", fmt_a, fmt_b, fmt_c, fmt_d);
    printf("line_length     = %d\n", info->line_length);
    printf("frame_size      = %d\n", info->frame_size);
    printf("frame_align_size= %d\n", info->frame_align_size);

    ret = isp_power_on(camera_hd);
    if (ret) {
        printf("%s power on failed\n", device_name);
        goto isp_power_on_error;
    }

    ret = isp_stream_on(camera_hd);
    if (ret) {
        printf("%s stream on failed\n", device_name);
        goto isp_stream_on_error;
    }

    return ;

isp_stream_on_error:
    isp_power_off(camera_hd);
isp_power_on_error:
    isp_free_buffer(camera_hd);
isp_request_buf_error:
isp_set_fmt_error:
    return;
}

static void isp_stream_stop(camera_hd_t *camera_hd)
{
    isp_stream_off(camera_hd);

    isp_power_off(camera_hd);

    isp_free_buffer(camera_hd);

    isp_release(camera_hd);

    camera_hd = NULL;
}

static void cmd_isp_start(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    if (argc < 3
            || !(argv[1][0] == '0' || argv[1][0] == '1')
            || !(argv[2][0] == '0' || argv[2][0] == '1' || argv[2][0] == '2')) {
        printf("usage  : isp_start <index> <channel>\n");
        printf("usage  : isp_start [0 ~ 1] [0 ~ 2]\n");
        printf("example: isp_start 0 0\n");
        return;
    }

    int index = argv[1][0] - '0';
    int channel = argv[2][0] - '0';

    if (!camera_hd) {
        camera_hd = isp_detect(index, channel);
        if (!camera_hd) {
            printf("mscaler%d-ch%d camera not found\n", index, channel);
            return;
        }
    }

    isp_stream_start(camera_hd);
}

static void cmd_isp_stop(struct cmd_arg *cmd_arg, int argc, char **argv)
{
    if (camera_hd)
        isp_stream_stop(camera_hd);
}

void cmd_isp_init(void)
{
    shell_cmd_register(cmd_isp_dump, "isp_dump", NULL, NULL);

    shell_cmd_register(cmd_isp_start, "isp_start", NULL, NULL);

    shell_cmd_register(cmd_isp_stop, "isp_stop", NULL, NULL);
}
