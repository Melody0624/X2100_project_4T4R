#ifndef _PWM_BACKLIGHT_H
#define _PWM_BACKLIGHT_H
#include <driver/pwm.h>
#include <driver/gpio.h>
#include <driver/gpio_pin.h>

struct pwm_backlight;

void pwm_backlight_init(void);
struct pwm_backlight *pwm_backlight_register(int pwm_gpio, struct gpio_pin pwm_en_gpio, const char *name, struct pwm_config_data *data);
void pwm_backlight_unregiser(struct pwm_backlight *backlight);

void pwm_backlight_export_cfg(void);
#endif