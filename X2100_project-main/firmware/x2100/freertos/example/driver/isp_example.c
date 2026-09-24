#include <stdio.h>
#include <common.h>
#include <os.h>
#include <driver/camera_isp.h>
#include <driver/isp_tuning.h>

static struct frame_image_format output_fmt = {
    .width              = 1920,
    .height             = 1080,
    .pixel_format       = CAMERA_PIX_FMT_NV12,

    .scaler.enable      = 0,
    .scaler.width       = 1920,
    .scaler.height      = 1200,

    .crop.enable        = 0,
    .crop.top           = 0,
    .crop.left          = 0,
    .crop.width         = 1920,
    .crop.height        = 1080,

    .frame_nums         = 2,
};

void test_isp(int index, int channel)
{
    camera_hd_t *camera_hd;
    struct camera_info *sensor_info;
    struct camera_info *info;
    char *device_name;
    int ret;

    camera_hd = isp_detect(index, channel);
    if (!camera_hd) {
        printf("mscaler%d-ch%d not found camera\n",index , channel);
        goto isp_detect_error;
    }
    device_name = (char *)camera_hd->ptr;

    if (!output_fmt.scaler.enable && !output_fmt.crop.enable) {
        /* 输出设置为Sensor分辨率 */
        sensor_info = isp_get_sensor_info(camera_hd);
        if (sensor_info) {
            output_fmt.width = sensor_info->width;
            output_fmt.height = sensor_info->height;
        }
    }

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

    struct frame_image_format fmt;
    isp_get_format(camera_hd, &fmt);

    int count = 0;
    while (1) {
        count++;
        void *buf = isp_wait_frame(camera_hd);
        if (buf) {
            printf("frame count: %d\n", count);
            isp_put_frame(camera_hd, buf);
        }
        sleep(1);
    }

    isp_stream_off(camera_hd);

isp_stream_on_error:
    isp_power_off(camera_hd);
isp_power_on_error:
    isp_free_buffer(camera_hd);
isp_request_buf_error:
isp_set_fmt_error:
    isp_release(camera_hd);
isp_detect_error:
    return ;
}
