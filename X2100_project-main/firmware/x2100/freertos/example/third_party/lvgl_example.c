#include "third_party/lvgl/lvgl_ingenic.h"

#include <devices/gt9xx_touch.h>
#include <driver/backlight.h>
#include "third_party/lvgl/lvgl/lvgl.h"
#include "third_party/lvgl/lvgl/examples/lv_examples.h"
#include "third_party/lvgl/lvgl/demos/lv_demos.h"

void backlight_use(void)
{
    struct backlight *lcd_pwm;

#if defined(CONFIG_PWM_BACKLIGHT0_NAME)
    lcd_pwm = backlight_open(CONFIG_PWM_BACKLIGHT0_NAME);
#elif defined(CONFIG_GPIO_BACKLIGHT0_NAME)
    lcd_pwm = backlight_open(CONFIG_GPIO_BACKLIGHT0_NAME);
#else
    lcd_pwm = backlight_open("lcd_pwm");
#endif

    if (lcd_pwm != NULL)
        backlight_set_brightness(lcd_pwm, backlight_get_maxbrightness(lcd_pwm));

}

void lvgl_test(void)
{
    const char *fb_path = "fb0";
    char *device_name = "gt9xx";
    /*该demo以gt9xx触摸屏作为lvgl input*/
    struct goodix_ts_data gt9xx;
    goodix_touch_init(&gt9xx);

    backlight_use();

    lv_init();

    int ret = lvgl_init_fb_display(fb_path);
    assert(!ret);

    ret = lvgl_init_tp_input(device_name);
    assert(!ret);

    lv_demo_widgets();

    lvgl_start(10 * 1000);

}