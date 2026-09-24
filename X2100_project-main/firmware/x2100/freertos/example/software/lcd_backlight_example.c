#include <lcd_backlight.h>
#include <lcd_pwm_backlight.h>
#include <common.h>

void backlight_register_test(void)
{
    struct lcd_pwm_backligt *backlight;

    struct pwm_config_data config = {
        .shutdown_mode = PWM_graceful_shutdown,
        .idle_level = PWM_idle_low,
        .accuracy_priority = PWM_accuracy_levels_first,
        .freq = 120000,
        .levels = 100,
        .clk_id = NULL,
    };

/*注册*/
    backlight = lcd_pwm_backlight_register(GPIO_PC(25), "lcd_pwm", &config);

/*注销*/
    // lcd_pwm_backlight_unregiser(backlight);
}

void backlight_use_test(void)
{
    struct lcd_backlight *lcd_pwm;
    int brightness;

/*在init.c已经执行*/
    // lcd_pwm_backlight_init();

#ifdef CONFIG_LCD_PWM_BACKLIGHT0_NAME
    lcd_pwm = lcd_backlight_open(CONFIG_LCD_PWM_BACKLIGHT0_NAME);
#else
    lcd_pwm = lcd_backlight_open("lcd_pwm");
#endif

    if (lcd_pwm != NULL) {
        lcd_backlight_set_brightness(lcd_pwm, 100);

        brightness = lcd_backlight_get_brightness(lcd_pwm);
        printf("brightness = %d\n",brightness);
    }

}