#include <stdio.h>
#include <os.h>
#include <driver/camera.h>
#include <common.h>

void test_camera(int index)
{
    struct camera_device *camera;
    struct camera_info *info;
    int ret;

    camera = camera_detect(index);
    if (!camera) {
        printf("camera not found\n");
        return;
    }

    info = camera_get_info(camera);
    assert(info);

    printf("camera found %s (%dx%d)\n", info->name, info->width, info->height);

    ret = camera_power_on(camera);
    if (ret < 0) {
        printf("camera failed to power on\n");
        return;
    }

    ret = camera_stream_on(camera);
    if (ret < 0) {
        printf("camera failed to stream on\n");
        camera_power_off(camera);
        return;
    }

    int retry_count = 0;

    while (1) {
        void *buf = camera_wait_frame(camera);
        if (buf == NULL) {
            camera_frame_error_type err = camera_get_frame_error(camera);
            printf("camera failed to get frame:%d\n", err);
            if (retry_count++ == 1) {
                printf("camera reset failed\n");
                camera_power_off(camera);
                return;
            }

            if (err == camera_error_dma_error) {
                camera_stream_off(camera);
                camera_stream_on(camera);
            } else {
                camera_power_off(camera);
                camera_power_on(camera);
                camera_stream_on(camera);
            }

            continue;
        }

        retry_count = 0;

        if (camera_fmt_is_NV12(info->data_fmt)) {
            printf("camera: frame:%p uv_buffer: %p\n", buf, buf + info->uv_data_offset);
        } else {
            printf("uv_buffer: %p\n", buf);
        }

        camera_put_frame(camera, buf);
    }
}
