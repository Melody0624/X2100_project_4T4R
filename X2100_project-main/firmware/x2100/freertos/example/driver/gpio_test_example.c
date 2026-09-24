#include <common.h>
#include <driver/gpio.h>

static inline int m_gpio_request(int gpio, const char *name)
{
    if (gpio < 0)
        return 0;

    return gpio_request(gpio, name);
}

static inline void m_gpio_direction_output(int gpio, int value)
{
    if (gpio >= 0)
        gpio_direction_output(gpio, value);
}

static inline void m_gpio_direction_input(int gpio)
{
    if (gpio >= 0)
        gpio_direction_input(gpio);
}

void test_gpio_output(void)                    
{
    m_gpio_request(GPIO_PE(20),"tp");           //申请GPIO_PE(20)
    m_gpio_direction_output(GPIO_PE(20),1);     //设置为输出高电平
}

void test_gpio_input(void)
{
    m_gpio_request(GPIO_PB(02),"test");        //申请GPIO_PB(02)
    m_gpio_direction_input(GPIO_PB(02));       //设置为输入
}