#include <driver/backlight.h>
#include <devices/pwm_backlight.h>
#include <devices/gpio_backlight.h>
#include <common.h>
#include <driver/gpio_pin.h>

void pwm_backlight_register_test(void)
{
    struct pwm_backligt *backlight;

    struct pwm_config_data config = {
        .shutdown_mode = PWM_graceful_shutdown,
        .idle_level = PWM_idle_low,
        .accuracy_priority = PWM_accuracy_levels_first,
        .freq = 120000,
        .levels = 100,
        .clk_id = NULL,
    };

    struct gpio_pin pwm_en = {GPIO_PD(1),1};

/*注册*/
    backlight = pwm_backlight_register(GPIO_PC(25), pwm_en, "lcd_pwm", &config);

/*注销*/
    // lcd_pwm_backlight_unregiser(backlight);
}

void gpio_backlight_regiser_test(void)
{
    struct gpio_backlight *backlight;

    struct gpio_pin gpio = {GPIO_PC(25), 1};

/*注册*/
    backlight = gpio_backlight_register(gpio, "lcd_pwm");

/*注销*/
    //gpio_backlight_unregiser(backlight);

}

void backlight_use_test(void)
{
    struct backlight *lcd_pwm;
    int brightness;

/*在init.c已经执行*/
    // pwm_backlight_init();
    // gpio_backlight_init();

#ifdef CONFIG_PWM_BACKLIGHT0_NAME
    lcd_pwm = backlight_open(CONFIG_PWM_BACKLIGHT0_NAME);
#elif defined(CONFIG_GPIO_BACKLIGHT0_NAME)
    lcd_pwm = backlight_open(CONFIG_GPIO_BACKLIGHT0_NAME);
#else
    lcd_pwm = backlight_open("lcd_pwm");
#endif

    if (lcd_pwm != NULL) {
        backlight_set_brightness(lcd_pwm, backlight_get_maxbrightness(lcd_pwm));

        brightness = backlight_get_brightness(lcd_pwm);
        printf("brightness = %d\n",brightness);

        brightness = backlight_get_maxbrightness(lcd_pwm);
        printf("max brightness = %d\n",brightness);
    }

}