#include <stdio.h>
#include <common.h>
#include <driver/gpio.h>

static inline void gpio_init(int gpio, enum gpio_function func)
{
    assert(!gpio_request(gpio, "PM"));
    gpio_set_func(gpio, func);
}

void pm_gpio_set_init(void)
{
    /* PA */
    gpio_init(GPIO_PA(0),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(1),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(2),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(3),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(4),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(5),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(6),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(7),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(8),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(9),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(10), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(11), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(12), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(13), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(14), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(15), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(16), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(17), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(18), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(19), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(20), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(21), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(22), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(23), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(24), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(25), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(26), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(27), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(28), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(29), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(30), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(31), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));

    /* uart2 */
    // gpio_set_func(GPIO_PB(0), GPIO_INPUT);
    // gpio_set_func(GPIO_PB(1), GPIO_INPUT);


    /* PB */
    gpio_init(GPIO_PB(2),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(3),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(4),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(5),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(6),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(7),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(8),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(9),  (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(10), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(11), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(12), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(13), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(14), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(15), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(16), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(17), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(18), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(19), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(20), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(21), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(22), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(23), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(24), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(25), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(26), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(27), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(28), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(29), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(30), (GPIO_OUTPUT1));
    gpio_init(GPIO_PB(31), (GPIO_OUTPUT1));

    /* PC */
    gpio_init(GPIO_PC(0),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(1),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(2),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(3),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(4),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(5),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(6),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(7),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(8),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(9),  (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(10), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(11), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(12), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(13), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(14), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(15), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(16), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(17), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(18), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(19), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(20), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(21), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(22), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(23), (GPIO_OUTPUT1 | GPIO_PULL_HIZ));

    gpio_init(GPIO_PC(24), (GPIO_OUTPUT0));
    gpio_init(GPIO_PC(25), (GPIO_OUTPUT0));
    gpio_init(GPIO_PC(26), (GPIO_OUTPUT0));

    gpio_init(GPIO_PC(29), (GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(30), (GPIO_PULL_HIZ));

    gpio_init(GPIO_PC(31), (GPIO_OUTPUT1));

    /* PD */
    gpio_init(GPIO_PD(0),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(1),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(2),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(3),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(4),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(5),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(6),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(7),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(8),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(9),  (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(10), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(11), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(12), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(13), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(14), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(15), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(16), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(17), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(18), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(19), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(20), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(21), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(22), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(23), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(24), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(25), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(26), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(27), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(28), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(29), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(30), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(31), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
}