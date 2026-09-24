#include <common.h>
#include <os.h>

#include <stdio.h>
#include <os.h>
#include <driver/camera.h>
#include <common.h>
#include <driver/cache.h>
#include <soc/lcdc_layer.h>
#include <driver/fb.h>

#include <driver/backlight.h>


void bayer16_to_nv12(void *_dst, void *_src, int xres, int yres)
{
    int len = xres * yres;
    unsigned char *src = _src + 1;
    unsigned char *dst = _dst;

    int i = len / 16;
    while (i--) {
        unsigned char s0 = src[0*2];
        unsigned char s1 = src[1*2];
        unsigned char s2 = src[2*2];
        unsigned char s3 = src[3*2];
        unsigned char s4 = src[4*2];
        unsigned char s5 = src[5*2];
        unsigned char s6 = src[6*2];
        unsigned char s7 = src[7*2];
        unsigned char s8 = src[8*2];
        unsigned char s9 = src[9*2];
        unsigned char s10 = src[10*2];
        unsigned char s11 = src[11*2];
        unsigned char s12 = src[12*2];
        unsigned char s13 = src[13*2];
        unsigned char s14 = src[14*2];
        unsigned char s15 = src[15*2];

        dst[0] = s0;
        dst[1] = s1;
        dst[2] = s2;
        dst[3] = s3;
        dst[4] = s4;
        dst[5] = s5;
        dst[6] = s6;
        dst[7] = s7;
        dst[8] = s8;
        dst[9] = s9;
        dst[10] = s10;
        dst[11] = s11;
        dst[12] = s12;
        dst[13] = s13;
        dst[14] = s14;
        dst[15] = s15;

        src += 32;
        dst += 16;
    }

    memset(dst, 0x80, len / 4);
}

struct camera_device *camera;
struct camera_info *camera_info;
static volatile int format_width;
static int camera_width;
static int camera_height;
static int is_bayer16;

static int camera_is_on = 0;

static void *old_frame;
static int lcd_width;
static int lcd_height;
static struct fb_info fb_info;
static int frame_index;

static int power_on_m_camera(void)
{
    int ret;

    if (camera_is_on)
        return 0;

    ret = camera_power_on(camera);
    if (ret < 0) {
        printf("camera failed to power on\n");
        return ret;
    }

    ret = camera_stream_on(camera);
    if (ret < 0) {
        printf("camera failed to stream on\n");
        camera_power_off(camera);
        return ret;
    }

    camera_is_on = 1;

    return 0;
}

static void power_off_m_camera(void)
{
    if (!camera_is_on)
        return;

    camera_stream_off(camera);
    camera_power_off(camera);
    camera_is_on = 0;
}

static void camera_update_thread(void *data)
{
    int ret;
    void *mem;

    while (1) {
        while (1) {
            if (power_on_m_camera())
                break;

            mem = camera_wait_frame(camera);
            if (mem == NULL) {
                camera_frame_error_type err = camera_get_frame_error(camera);
                printf("failed to get frame: %d\n", err);
                if (err == camera_error_dma_error) {
                    camera_stream_off(camera);
                    camera_stream_on(camera);
                } else {
                    camera_power_off(camera);
                    camera_power_on(camera);
                }
                continue;
            }

            bayer16_to_nv12(mem, mem, camera_width, camera_height);

            void *y_mem = mem;
            void *uv_mem = mem + camera_width * camera_height;
            int width = camera_width;
            int height = camera_height;

            if (camera_width > lcd_width) {
                width = lcd_width;
                y_mem += (camera_width - lcd_width) / 2;
            }

            if (camera_height > lcd_height) {
                height = lcd_height;
                uv_mem += (camera_height - lcd_height) / 4;
            }

            struct lcdc_layer layer_cfg = {
                .fb_fmt = fb_fmt_NV12,
                .xres = width,
                .yres = height,
                .xpos = 0,
                .ypos = 0,

                .layer_order = lcdc_layer_bottom,
                .layer_enable = 1,

                .y = {
                    .mem = y_mem,
                    .stride = camera_width,
                },

                .uv = {
                    .mem = uv_mem,
                    .stride = camera_width / 2,
                },

                .alpha = {
                    .enable = 0,
                    .value = 0xff,
                },
            };

            lcdc_layer_server_update(frame_index, 1, &layer_cfg, 0);

            if (old_frame)
                camera_put_frame(camera, old_frame);

            old_frame = mem;
        }
    }
}

#ifdef CONFIG_BACKLIGHT
static struct backlight *m_backlight;
static void backlight_thread(void *data)
{
    msleep(500);
    int level = backlight_get_maxbrightness(m_backlight);
    printf("backlight level: %d\n", level);
    backlight_set_brightness(m_backlight, level);
}
#endif

int camera_to_lcdc_layer_test(int index)
{
    camera = camera_detect(index);
    if (!camera) {
        printf("camera not found\n");
        return 0;
    }

    camera_info = camera_get_info(camera);
    assert(camera_info);

    printf("camera found %s (%dx%d) %d\n",
         camera_info->name, camera_info->width, camera_info->height, camera_info->frame_align_size);

    camera_width = camera_info->width;
    camera_height = camera_info->height;

    switch (camera_info->data_fmt) {
    case CAMERA_PIX_FMT_SBGGR16:
    case CAMERA_PIX_FMT_SGBRG16:
    case CAMERA_PIX_FMT_SGRBG16:
    case CAMERA_PIX_FMT_SRGGB16:
        is_bayer16 = 1;
        break;
    case CAMERA_PIX_FMT_YUYV:
    case CAMERA_PIX_FMT_YYUV:
    case CAMERA_PIX_FMT_YVYU:
    case CAMERA_PIX_FMT_UYVY:
    case CAMERA_PIX_FMT_VYUY:
        is_bayer16 = 0;
        break;
    default:
        panic("this format currently not support: 0x%x\n", camera_info->data_fmt);
    }

    fb_get_info(&fb_info);
    lcd_width = fb_info.xres;
    lcd_height = fb_info.yres;

    assert(fb_info.frame_count >= 2);

#ifdef CONFIG_BACKLIGHT
    m_backlight = backlight_open("lcd_backlight");
    if (m_backlight) {
        backlight_set_brightness(m_backlight, 0);
        thread_create("set-brightness", 4 * 1024, backlight_thread, NULL);
    }
    else
        printf("failed to get backlight device: lcd_backlight\n");
#else
    printf("CONFIG_BACKLIGHT if you forget it\n");
#endif

    lcdc_layer_server_enable_fb(1);

    thread_create("usb gadget uvc thread", 8192, camera_update_thread, NULL);

    return 0;
}
