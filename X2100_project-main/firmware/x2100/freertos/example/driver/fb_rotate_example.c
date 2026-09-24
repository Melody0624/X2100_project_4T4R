#include <stdio.h>
#include <driver/fb.h>
#include <os.h>

#include <driver/backlight.h>
#include <devices/pwm_backlight.h>
#include <devices/gpio_backlight.h>
#include <common.h>
#include <driver/gpio_pin.h>
#include <include_bin.h>

#define IMG_WIDTH  320
#define IMG_HEIGHT 240

INCBIN(img, "example/resource/image_320x240.nv12");


void backlight_enable(void)
{
    struct backlight *lcd_gpio;
    lcd_gpio = backlight_open(CONFIG_GPIO_BACKLIGHT0_NAME);
    if (lcd_gpio != NULL)
        backlight_set_brightness(lcd_gpio, 1);
}

void soc_fb_set_rotate(enum lcdc_rotate_angle angle);

void fb_rotate_test(void)
{
    struct fb_info fb_info;
    struct fb_handle *fb;

    backlight_enable();

    fb = fb_open("fb0");

    if (fb == NULL) {
        printf("open fb0 error!\n");
        return;
    }

    fb_enable(fb);
    fb_get_info(fb, &fb_info);

    int rotate_cnt = 1;

    struct lcdc_layer layer_cfg = {
        .fb_fmt = fb_fmt_NV12,
        .xres = IMG_WIDTH,
        .yres = IMG_HEIGHT,
        .xpos = 0,
        .ypos = 0,

        .layer_order = lcdc_layer_0,
        .layer_enable = 1,

        .y = {
            .mem = (void *)imgData,
            .stride = IMG_WIDTH,
        },
        .uv = {
            .mem = (void *)imgData + (IMG_WIDTH * IMG_HEIGHT),
            .stride = IMG_WIDTH,
        },

        .alpha = {
            .enable = 0,
            .value = 0xff,
        },
    };


    fb_enable_config(fb);

    while(1) {
        fb_get_info(fb, &fb_info);
        printf("fb_info = %d, %d\n", fb_info.xres, fb_info.yres);
        if (fb_info.xres >= IMG_WIDTH) {
            layer_cfg.scaling.enable = 0;
        } else {
            layer_cfg.scaling.enable = 1;
            layer_cfg.scaling.xres = fb_info.xres;
            layer_cfg.scaling.yres = fb_info.yres;
        }

        if (!fb_set_config(fb, &layer_cfg))
            fb_pan_display(fb, 0);

        msleep(300);
        soc_fb_set_rotate((rotate_cnt % 4));
        rotate_cnt++;
    }
}