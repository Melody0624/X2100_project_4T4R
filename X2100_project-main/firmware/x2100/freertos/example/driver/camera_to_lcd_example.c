#include <stdio.h>
#include <os.h>
#include <driver/backlight.h>
#include <driver/fb.h>
#include <driver/camera.h>
#include <common.h>
#include <bayer16_to_rgb.h>
#include <yuv422_to_rgb.h>

//初始化lcd、backlight
struct fb_handle * lcd_init(struct fb_info * fb_info) {

    struct backlight *lcd_pwm;
    struct fb_handle *fb;

    lcd_pwm = backlight_open("backlight_gpio0");
    if (lcd_pwm == NULL)
        printf("backlight_open fail.\n");
    else
        backlight_set_brightness(lcd_pwm, lcd_pwm->max_brightness);

    fb = fb_open("fb0");
    if (fb == NULL) {
        printf("open fb0 error!\n");
        return NULL;
    }

    fb_enable(fb);
    fb_get_info(fb, fb_info);

    return fb;
}

void test_camera_to_lcd(void)
{
    struct camera_device *camera;
    struct camera_info *cam_info;
    struct fb_info fb_info;
    struct fb_handle *fb;

    int ret;

    fb = lcd_init(&fb_info);

    camera = camera_detect(0);
    if (!camera) {
        printf("camera not found\n");
        return;
    }

    cam_info = camera_get_info(camera);
    assert(cam_info);
    printf("camera found %s (%dx%d)\n", cam_info->name, cam_info->width, cam_info->height);

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

        // if (cam_info->data_fmt == CAMERA_PIX_FMT_NV12 ||
        //     cam_info->data_fmt == CAMERA_PIX_FMT_NV21) {
        //     printf("camera: frame:%p uv_buffer: %p\n", buf, buf + cam_info->uv_data_offset);
        // } else {
        //     printf("uv_buffer: %p\n", buf);
        // }

        switch (cam_info->data_fmt)
        {
        case CAMERA_PIX_FMT_SBGGR16:
        case CAMERA_PIX_FMT_SGBRG16:
        case CAMERA_PIX_FMT_SGRBG16:
        case CAMERA_PIX_FMT_SRGGB16:
            bayer16_to_rgb(buf, fb_info.fb_mem, fb_info.xres, fb_info.yres, fb_info.fb_fmt,
                    fb_info.bytes_per_line, cam_info->width, cam_info->height, cam_info->data_fmt);
            break;
        case CAMERA_PIX_FMT_YUYV:
            yuv422_to_rgb(buf, fb_info.fb_mem, fb_info.xres, fb_info.yres, fb_info.fb_fmt,
                fb_info.bytes_per_line, cam_info->width, cam_info->height);
            break;

        default:
            printf("not support data_fmt.\n");
            break;
        }

        fb_pan_display(fb, 0);
        camera_put_frame(camera, buf);
    }
    camera_power_off(camera);
}