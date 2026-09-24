#ifndef _GPIO_PIN_H_
#define _GPIO_PIN_H_

#include <driver/gpio.h>

/**
 * gpio引脚的数据结构
 */
struct gpio_pin {
    ///< pin 对应的gpio
    int gpio;

    ///< pin 使能时的电平
    int enable_level;
};

/**
 * @brief 判断引脚是否定义/有效
 * @param pin 引脚
 * @return 1:引脚定义 0:引脚未定义
 */
static inline int gpio_pin_is_valid(struct gpio_pin pin)
{
    return gpio_is_valid(pin.gpio);
}

/**
 * @brief 申请引脚
 * @param pin 引脚
 * @param name 引脚的名字
 * @return 0:引脚申请成功 其它:引脚申请失败
 * @note 引脚未定义/无效时,返回值为 0
 */
int gpio_pin_request(struct gpio_pin pin, const char *name);

/**
 * @brief 释放引脚
 * @param pin 引脚
 */
void gpio_pin_release(struct gpio_pin pin);

/**
 * @brief 使能引脚,使引脚输出有效电平
 * @param pin 引脚
 */
void gpio_pin_output_enable(struct gpio_pin pin);

/**
 * @brief 失能引脚,使引脚输出无效电平
 * @param pin 引脚
 */
void gpio_pin_output_disable(struct gpio_pin pin);

/**
 * @brief 将引脚设置为输入
 * @param pin 引脚
 */
void gpio_pin_as_input(struct gpio_pin pin);

/**
 * @brief 判断引脚是否为有效电平
 * @param pin 引脚
 * @return 1:引脚为有效电平 0:引脚为无效电平
 * @note 引脚未定义/无效时,返回值为 0
 */
int gpio_pin_is_enable(struct gpio_pin pin);

#endif /* _GPIO_PIN_H_ */
