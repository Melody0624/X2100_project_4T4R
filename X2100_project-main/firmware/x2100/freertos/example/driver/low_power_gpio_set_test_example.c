#include <stdio.h>
#include <common.h>
#include <driver/pm.h>
#include <driver/gpio.h>
#include <driver/irq.h>

#include <os.h>

#define WAKEUP_GPIO GPIO_PB(28)

static void m_gpio_irq_handler(int irq, void *data)
{
    printf("wakeup-gpio irq: %d %d\n", irq, gpio_get_value(irq_to_gpio(irq)));
}

static void gpio_wakeup_irq(void)
{
    request_irq(gpio_to_irq(WAKEUP_GPIO), IRQ_TYPE_EDGE_BOTH, m_gpio_irq_handler, "wakeup-gpio", NULL);
}

static inline void gpio_init(int gpio, enum gpio_function func)
{
    assert(!gpio_request(gpio, "PM"));
    gpio_set_func(gpio , func);
}

void pm_gpio_low_power_set_init(void)
{
    /* x1000 */
    /* 以下gpio无特殊说明均为悬空脚，SFC FLASH相关引脚这里不作处理 */
    gpio_init(GPIO_PA(0), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(1), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(4), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(5), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(6), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(7), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(8), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PA(9), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
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

    gpio_init(GPIO_PB(0), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(1), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(2), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(3), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(4), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(5), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(6), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(7), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(8), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(9), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(10), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(11), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(12), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(13), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(14), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(15), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(16), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(17), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(18), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(19), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(20), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(21), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(22), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(25), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(26), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(27), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));

    gpio_init(GPIO_PC(0), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(1), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(2), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(3), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(4), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(5), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(6), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(7), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(8), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(9), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(10), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(11), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(12), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(13), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(14), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(15), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(16), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(17), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(18), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(19), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(20), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(21), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(22), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(23), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(24), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(25), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(26), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PC(27), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));

    gpio_init(GPIO_PD(2), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(3), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(4), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(5), (GPIO_OUTPUT0 | GPIO_PULL_HIZ));

    /* PB23、PB24是i2c对应引脚,外接上拉电阻，因为没有外接I2C设备所以不使用i2c功能，仅作普通IO处理 */
    gpio_init(GPIO_PB(23), (GPIO_INPUT | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(24), (GPIO_INPUT | GPIO_PULL_HIZ));

    /* PD0、PD1是i2c对应引脚,外接上拉电阻，这里不使用i2c功能仅作普通IO处理 */
    gpio_init(GPIO_PD(0), (GPIO_INPUT | GPIO_PULL_HIZ));
    gpio_init(GPIO_PD(1), (GPIO_INPUT | GPIO_PULL_HIZ));

    /* BOOT sel 0~2、wakeup 引脚，保留原来状态不作其他处理 */
    gpio_init(GPIO_PB(28), (GPIO_INPUT | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(29), (GPIO_INPUT | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(30), (GPIO_INPUT | GPIO_PULL_HIZ));
    gpio_init(GPIO_PB(31), (GPIO_INPUT | GPIO_PULL_HIZ));

}

void pm_gpio_low_power_runtime_set_before_sleep(void)
{
    /* 设置GPIO进入休眠的状态(可提前记录休眠前状态，具体操作参考[FreeRTOS功耗调试说明文档]) */
}

void pm_gpio_low_power_runtime_set_after_sleep(void)
{
    /* 恢复GPIO休眠前的状态,一般休眠前是什么状态就恢复成什么状态 */
}

static void rtc_cb(void *data)
{
    printf("rtc alarm comming !\n");
}

int low_power_test(void)
{
    int ret = 0;

    printf("x1000 low_power_test start...\n");

    /* 初始化GPIO悬空引脚的状态，此操作可以提前 */
    pm_gpio_low_power_set_init();

    wake_lock_wait_unlock_timeout(10);

    os_enter_critical();

    /* 通过所有引用计数不为0的wake lock的数量，判断能不能进入休眠 */
    if (wake_lock_get_locked_count()) {
        printf("Enter sleep timeout, maybe some wake_lock locked!\n");
        wake_locks_show();
        ret = -EBUSY;
        goto exit_critical;
    }

    /* 通过设置GPIO使其在休眠时处于较低功耗 */
    pm_gpio_low_power_runtime_set_before_sleep();

    /* 系统休眠后可以通过按键中断唤醒 */
    gpio_wakeup_irq();

    /* 系统休眠后可以通过rtc时钟唤醒（10秒后唤醒） */
    rtc_set_alarm(10, rtc_cb);

    /* 进入休眠 */
    pm_enter_sleep();

    /* 恢复GPIO休眠前的状态 */
    pm_gpio_low_power_runtime_set_after_sleep();

    /* 从休眠被唤醒 */
    printf("wakeup !\n");

exit_critical:
    os_exit_critical();

    printf("low_power_test end !\n");

    return ret;
}