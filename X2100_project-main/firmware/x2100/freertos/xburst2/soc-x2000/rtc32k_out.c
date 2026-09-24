#include <os.h>
#include <spinlock.h>
#include <driver/gpio.h>

#define RTC32K_GPIO                         GPIO_PE(23)
#define RTC32K_FUNCTION                     GPIO_FUNC_0

static DEFINE_SPINLOCK(lock);

static int rtc32k_initialized = 0;
static int refcount = 0;
static int rtc32k_init_on = 0;

int rtc32k_enable(void)
{
    unsigned long flags;
    int gpio = RTC32K_GPIO;
    int func = RTC32K_FUNCTION;

    if (!rtc32k_initialized) {
        printf("rtc32k is not initialized\n");
        return -EINVAL;
    }

    spin_lock_irqsave(&lock, flags);

    if (refcount++ == 0)
        gpio_set_func(gpio, func);

    spin_unlock_irqrestore(&lock, flags);
    return 0;
}

int rtc32k_disable(void)
{
    unsigned long flags;
    int gpio = RTC32K_GPIO;
    int func = GPIO_INPUT;

    if (!rtc32k_initialized) {
        printf("rtc32k is not initialized\n");
        return -EINVAL;
    }

    spin_lock_irqsave(&lock, flags);

    if (--refcount == 0)
        gpio_set_func(gpio, func);

    spin_unlock_irqrestore(&lock, flags);

    return 0;
}


int rtc32k_init(void)
{
    int ret;
    int gpio = RTC32K_GPIO;
    char gpio_str[10];

#ifdef CONFIG_X2000_UTILS_RTC32K_INIT_ON
    rtc32k_init_on = 1;
#endif
    ret = gpio_request(gpio, "rtc32k_out");
    if (ret < 0) {
        printf("rtc32k gpio request failed: %s\n", gpio_to_str(gpio, gpio_str, sizeof(gpio_str)));
        return ret;
    }

    rtc32k_initialized = 1;

    if (rtc32k_init_on) {
        //printf("init enable rtc32k out\n");
        rtc32k_enable();
    }

    return 0;
}

void rtc32k_deinit(void)
{
    int gpio = RTC32K_GPIO;

    if (rtc32k_initialized)
        gpio_release(gpio);

    refcount = 0;
    rtc32k_initialized = 0;
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(rtc32k_enable);
EXPORT_SYMBOL(rtc32k_disable);
EXPORT_SYMBOL(rtc32k_init);
EXPORT_SYMBOL(rtc32k_deinit);