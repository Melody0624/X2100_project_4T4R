#include <driver/mscaler.h>
#include <common.h>
#include <os.h>
#include <inttypes.h>
#include <driver/fb.h>
#include <os.h>
#include <driver/backlight.h>
#include <devices/pwm_backlight.h>
#include <devices/gpio_backlight.h>
#include <driver/cache.h>
#include <driver/mscaler.h>
#include <include_bin.h>
INCBIN(image_src, "example/resource/image_320x240.nv12");

#define DST_WIDTH 480
#define DST_HEIGHT 840
#define SRC_WIDTH 320
#define SRC_HEIGHT 240

int64_t get_timeus(void)
{
    return systick_get_time_us();
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

int mscaler_test(void)
{
    int ret = 0;
    struct fb_info info;
    struct mscaler_param ms_param;
    int src_size;
    int src_size_y;
    int src_size_uv;
    int dst_size;

#ifdef CONFIG_BACKLIGHT
    #ifdef CONFIG_PWM_BACKLIGHT0_NAME
    m_backlight = backlight_open(CONFIG_PWM_BACKLIGHT0_NAME);
    #elif CONFIG_GPIO_BACKLIGHT0_NAME
    m_backlight = backlight_open(CONFIG_GPIO_BACKLIGHT0_NAME);
    #else
    m_backlight = backlight_open("lcd_pwm");
    #endif
    if (m_backlight) {
        backlight_set_brightness(m_backlight, 0);
        thread_create("set-brightness", 4 * 1024, backlight_thread, NULL);
    }
    else
        printf("failed to get backlight device: lcd_backlight\n");
#else
    printf("CONFIG_BACKLIGHT if you forget it\n");
#endif

    fb_get_info(&info);
    fb_enable();
    if (info.fb_fmt != fb_fmt_RGB888 && info.fb_fmt != fb_fmt_ARGB8888 ) {
       printf("fb_test: this just support rgb888! you should change this demo\n");
       return -1;
    }

    memset(&ms_param, 0, sizeof(struct mscaler_param));

    int ms_align_size = mscaler_align_size();

    ms_param.src_fmt = MSCALER_FORMAT_NV12;

    src_size_y = SRC_WIDTH * SRC_HEIGHT;
    src_size_y = ALIGN(src_size_y, ms_align_size);
    src_size_uv = SRC_WIDTH * SRC_HEIGHT / 2;
    src_size_uv = ALIGN(src_size_uv, ms_align_size);
    src_size = src_size_y + src_size_uv;

    ms_param.src_yaddr = cache_align_malloc(src_size);
    memcpy(ms_param.src_yaddr, image_srcData, SRC_WIDTH * SRC_HEIGHT);
    ms_param.src_uvaddr = ms_param.src_yaddr + src_size_y;
    memcpy(ms_param.src_uvaddr, image_srcData + SRC_WIDTH * SRC_HEIGHT, SRC_WIDTH * SRC_HEIGHT / 2);
    ms_param.src_xres = SRC_WIDTH;
    ms_param.src_yres = SRC_HEIGHT;

    ms_param.dst_fmt = MSCALER_FORMAT_BGRA_8888;
    dst_size = DST_WIDTH * DST_HEIGHT * 4;
    dst_size = ALIGN(dst_size, ms_align_size);
    ms_param.dst_yaddr = cache_align_malloc(dst_size);
    ms_param.dst_xres = DST_WIDTH;
    ms_param.dst_yres = DST_HEIGHT;

    int64_t start_record_timeus = get_timeus();
    for (int i = 0; i < 100; i++) {
        ret = mscaler_convert(&ms_param);
        if (ret<0) {
            printf("error ret %d i %d\n",ret,i);
            break;
        }
    }
    printf("nv12 to rgb 100 times　need %"PRIu64"us\n",get_timeus() - start_record_timeus);
    memcpy(info.fb_mem, ms_param.dst_yaddr, dst_size);
    fb_pan_display(0);
    free(ms_param.src_yaddr);
    free(ms_param.dst_yaddr);
    return ret;
}

