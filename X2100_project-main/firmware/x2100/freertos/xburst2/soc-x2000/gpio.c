#include <common.h>
#include <soc/base.h>
#include <soc/gpio.h>
#include <spinlock.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <cpu/cache.h>
#include <__ffs.h>
#include <driver/adc.h>
#include <soc/cpm.h>

#define PE_PORT     4
#define PE_TYPED    (BIT(22) | BIT(23) | BIT(24) | BIT(25) | BIT(26) | BIT(27))

#define CPM_EXCLK_DS                    (0xE0)
#define DVP_VOLTAGE_BIT                 30
#define SD_VOLTAGE_BIT                  31

#define SOC_GPIO_VOLTAGE_3_3V           0
#define SOC_GPIO_VOLTAGE_1_8V           1

#define ADC_VREF_VOLTAGE                1800

#define GPIO_FUNC_FLAG      0x0100
#define GPIO_FUNC_OFFSET    0
#define GPIO_FUNC_MASK      0x01ff

#define GPIO_PULL_FLAG      0x8000
#define GPIO_PULL_OFFSET    13
#define GPIO_PULL_MASK      0xe000

static const unsigned long gpio_irqbase[] = {
    [GPIO_PORT_A] = IRQ_GPIO_START + 0 * 32,
    [GPIO_PORT_B] = IRQ_GPIO_START + 1 * 32,
    [GPIO_PORT_C] = IRQ_GPIO_START + 2 * 32,
    [GPIO_PORT_D] = IRQ_GPIO_START + 3 * 32,
    [GPIO_PORT_E] = IRQ_GPIO_START + 4 * 32,
};

static const unsigned long gpiobase[] = {
    [0] = (unsigned long)CKSEG1ADDR(GPIO_IOBASE + 0 * GPIO_PORT_OFF),
    [1] = (unsigned long)CKSEG1ADDR(GPIO_IOBASE + 1 * GPIO_PORT_OFF),
    [2] = (unsigned long)CKSEG1ADDR(GPIO_IOBASE + 2 * GPIO_PORT_OFF),
    [3] = (unsigned long)CKSEG1ADDR(GPIO_IOBASE + 3 * GPIO_PORT_OFF),
    [4] = (unsigned long)CKSEG1ADDR(GPIO_IOBASE + 4 * GPIO_PORT_OFF),

    [SHADOW] = (unsigned long)CKSEG1ADDR(GPIO_IOBASE + GPIO_SHADOW_OFF),
};

#define GPIO_ADDR(port, reg) ((volatile unsigned long *)(gpiobase[port] + reg))

static inline void gpio_write(int port, unsigned int reg, int val)
{
    *GPIO_ADDR(port, reg) = val;
}

static inline unsigned int gpio_read(int port, unsigned int reg)
{
    return *GPIO_ADDR(port, reg);
}

static inline int hal_gpio_get_value(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    return !!(gpio_read(port, PXPIN) & (1 << pin));
}

static inline void hal_gpio_set_value(int gpio, int value)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    gpio_write(port, value ? PXPAT0S : PXPAT0C, 1 << pin);
}

static inline void hal_gpio_unmask_irq(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    gpio_write(port, PXFLGC, 1 << pin);
    gpio_write(port, PXMSKC, 1 << pin);
}

static inline void hal_gpio_mask_irq(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    gpio_write(port, PXMSKS, 1 << pin);
}

static inline void hal_gpio_clear_irqflag(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    gpio_write(port, PXFLGC, 1 << pin);
}

static inline int hal_gpio_port_get_irqflag(enum gpio_port port)
{
    return gpio_read(port, PXFLG) & ~gpio_read(port, PXMSK);
}

static void hal_gpio_port_set_func(int port, unsigned int pins, enum gpio_function func)
{
    /* func option */
    if (func & GPIO_FUNC_FLAG) {
        /* No Shadows registers for EDG, set registers directly */
        if (func & 0x10)
            gpio_write(port, PXDEGS, pins);
        else
            gpio_write(port, PXDEGC, pins);

        if (func & 0x8)
            gpio_write(SHADOW, PXINTS, pins);
        else
            gpio_write(SHADOW, PXINTC, pins);

        if (func & 0x4)
            gpio_write(SHADOW, PXMSKS, pins);
        else
            gpio_write(SHADOW, PXMSKC, pins);

        if (func & 0x2)
            gpio_write(SHADOW, PXPAT1S, pins);
        else
            gpio_write(SHADOW, PXPAT1C, pins);

        if (func & 0x1)
            gpio_write(SHADOW, PXPAT0S, pins);
        else
            gpio_write(SHADOW, PXPAT0C, pins);

        /* configure PzGID2LD to specify which port group to load */
        gpio_write(SHADOW, PZGID2LD, port);
    }

    if (func & GPIO_PULL_FLAG) {
        int pull = (func >> GPIO_PULL_OFFSET) & 0x3;

        /* TYPED only support pull-down function and the setting in the pull-up register */
        if (pull && port == PE_PORT && pins & PE_TYPED)
            pull--;

        if (pull == 0) { // no pull
            gpio_write(port, PXPUC, pins);
            gpio_write(port, PXPDC, pins);
        }
        if (pull == 1) { // pull up
            gpio_write(port, PXPDC, pins);
            gpio_write(port, PXPUS, pins);
        }
        if (pull == 2) { // pull down
            gpio_write(port, PXPUC, pins);
            gpio_write(port, PXPDS, pins);
        }
    }
}

static DEFINE_SPINLOCK(lock);

struct gpio_data {
    unsigned int request_map;
    unsigned int irq_map;
    unsigned int ref_count;
    unsigned int irq_both_edge;
};

struct gpio_data gpiodata[5];

const char *gpionames[5 * 32];

void soc_gpio_set_driver_strength(int gpio, int value)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    if(value & BIT(0))
        gpio_write(port, PXDS0S, BIT(pin));
    else
        gpio_write(port, PXDS0C, BIT(pin));

    if(value & BIT(1))
        gpio_write(port, PXDS1S, BIT(pin));
    else
        gpio_write(port, PXDS1C, BIT(pin));

    if(value & BIT(2))
        gpio_write(port, PXDS2S, BIT(pin));
    else
        gpio_write(port, PXDS2C, BIT(pin));

    spin_unlock_irqrestore(&lock, flags);
}

int soc_gpio_get_driver_strength(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;
    int value = -1;

    spin_lock_irqsave(&lock, flags);
    value = (gpio_read(port, PXDS0) & BIT(pin)) ? (1 << 0) : 0;
    value |= (gpio_read(port, PXDS1) & BIT(pin)) ? (1 << 1) : 0;
    value |= (gpio_read(port, PXDS2) & BIT(pin)) ? (1 << 2) : 0;

    spin_unlock_irqrestore(&lock, flags);

    return value;
}

void soc_gpio_set_slew_rate(int gpio, int value)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    if (value)
        gpio_write(port, PXSRS, BIT(pin));
    else
        gpio_write(port, PXSRC, BIT(pin));

    spin_unlock_irqrestore(&lock, flags);
}

int soc_gpio_get_slew_rate(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;
    int value = -1;

    spin_lock_irqsave(&lock, flags);

    value = (gpio_read(port, PXSR) & BIT(pin)) ? (1 << 0) : 0;

    spin_unlock_irqrestore(&lock, flags);

    return value;
}

void soc_gpio_set_schmitt(int gpio, int value)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    if(value & BIT(0))
        gpio_write(port, PXSMTS, BIT(pin));
    else
        gpio_write(port, PXSMTC, BIT(pin));

    spin_unlock_irqrestore(&lock, flags);
}

int soc_gpio_get_schmitt(int gpio)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;
    int value = -1;

    spin_lock_irqsave(&lock, flags);

    value = (gpio_read(port, PXSMT) & BIT(pin)) ? (1 << 0) : 0;

    spin_unlock_irqrestore(&lock, flags);

    return value;
}

void gpio_port_set_func(enum gpio_port port, unsigned int pins, enum gpio_function func)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);
    hal_gpio_port_set_func(port, pins, func);
    spin_unlock_irqrestore(&lock, flags);
}

void soc_gpio_set_func(int gpio, enum gpio_function func)
{
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    unsigned long flags;
    char gpio_str[10];

    spin_lock_irqsave(&lock, flags);

    if (func & 0x10) {
        int tmp = func & 0x1f;
        if (GPIO_INT_LO <= tmp && tmp <= GPIO_INT_RE_FE)
            panic("gpio:%s can't set as irq func, please use irq api\n",
                     gpio_to_str(gpio, gpio_str, sizeof(gpio_str)));

        if (gpiodata[port].irq_map & (1 << pin)) {
            if (tmp == GPIO_INPUT)
                func &= ~GPIO_FUNC_MASK;
            else
                panic("gpio:%s can't set as other func when it's irq mode\n",
                     gpio_to_str(gpio, gpio_str, sizeof(gpio_str)));
        }
    }

    hal_gpio_port_set_func(port, 1 << pin, func);

    spin_unlock_irqrestore(&lock, flags);
}

int soc_gpio_get_value(int gpio)
{
    return hal_gpio_get_value(gpio);
}

void soc_gpio_set_value(int gpio, int value)
{
    hal_gpio_set_value(gpio, value);
}

void soc_gpio_direction_output(int gpio, int value)
{
    gpio_set_func(gpio, value ? GPIO_OUTPUT1 : GPIO_OUTPUT0);
}

void soc_gpio_direction_input(int gpio)
{
    gpio_set_func(gpio, GPIO_INPUT);
}

int soc_gpio_request(int gpio, const char *name)
{
    unsigned long flags;
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    char gpio_str[5];

    assert(gpio >= 0 && gpio <= 5 * 32);

    spin_lock_irqsave(&lock, flags);

    if (gpiodata[port].request_map & (1 << pin)) {
        printf("gpio: %s has been requested as: %s\n", gpio_to_str(gpio, gpio_str, sizeof(gpio_str)), gpionames[gpio]);
        spin_unlock_irqrestore(&lock, flags);
        return -EBUSY;
    }

    gpiodata[port].request_map |= 1 << pin;
    gpionames[gpio] = name;

    spin_unlock_irqrestore(&lock, flags);

    return 0;
}

void soc_gpio_release(int gpio)
{
    unsigned long flags;
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    assert(gpio >= 0 && gpio <= 5 * 32);

    spin_lock_irqsave(&lock, flags);

    assert(gpiodata[port].request_map & (1 << pin));

    gpiodata[port].request_map &= ~(1 << pin);
    gpionames[gpio] = NULL;

    spin_unlock_irqrestore(&lock, flags);
}

/*
 * 如下api 调用的时候中断是关闭的,
 * 所以在单核cpu上不必 spin_lock
 */

void soc_gpio_startup_irq(int irq, unsigned int irqflags)
{
    int gpio = irq - IRQ_GPIO_START;
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;
    enum gpio_function func;
    int type = to_irqtype(irqflags);

    gpiodata[port].irq_both_edge &= ~(1 << pin);
    gpiodata[port].irq_map |= 1 << pin;

    switch (type)
    {
    case IRQ_TYPE_LEVEL_LOW:
        func = GPIO_INT_LO;
        break;
    case IRQ_TYPE_LEVEL_HIGH:
        func = GPIO_INT_HI;
        break;
    case IRQ_TYPE_EDGE_FALLING:
        func = GPIO_INT_FE;
        break;
    case IRQ_TYPE_EDGE_RISING:
        func = GPIO_INT_RE;
        break;
    case IRQ_TYPE_EDGE_BOTH:
        func = GPIO_INT_RE_FE;
        break;
    default:
        assert(0);
        return;
    }

    // 默认屏蔽中断,相当于hal_gpio_mask_irq(gpio);
    func |= 4;

    hal_gpio_port_set_func(port, 1 << pin, func);
}

void soc_gpio_shutdown_irq(int irq)
{
    int gpio = irq - IRQ_GPIO_START;
    enum gpio_port port = gpio / 32;
    unsigned int pin = gpio % 32;

    gpiodata[port].irq_map &= ~(1 << pin);
    hal_gpio_port_set_func(port, 1 << pin, GPIO_INPUT);
}

void soc_gpio_enable_irq(int irq)
{
    int gpio = irq - IRQ_GPIO_START;
    enum gpio_port port = gpio / 32;

    hal_gpio_clear_irqflag(gpio);
    hal_gpio_unmask_irq(gpio);
    if (gpiodata[port].ref_count++ == 0)
        arch_enable_irq_nolock(IRQ_GPIO0-port);
}

void soc_gpio_disable_irq(int irq)
{
    int gpio = irq - IRQ_GPIO_START;
    enum gpio_port port = gpio / 32;

    if (--gpiodata[port].ref_count == 0)
        arch_disable_irq_nolock(IRQ_GPIO0-port);

    hal_gpio_mask_irq(gpio);

    /* 确保 msk 写入
     */
    fast_iob();
}

static void gpio_irq_handler(int irq, void *data)
{
    enum gpio_port port = (enum gpio_port) data;
    unsigned long flag = hal_gpio_port_get_irqflag(port);
    int pin = __ffs(flag);

    // if (!(gpiodata[port].request_map & (1 << pin)))
    //     return;

    if (!flag)
        hang();

    gpio_write(port, PXFLGC, 1 << pin);

    handle_irq(gpio_irqbase[port] + pin);
}

void soc_gpio_irq_init(void)
{
    request_irq_disabled(IRQ_GPIO0, 0, gpio_irq_handler, "GPIO_PA", (void *)GPIO_PORT_A);
    request_irq_disabled(IRQ_GPIO1, 0, gpio_irq_handler, "GPIO_PB", (void *)GPIO_PORT_B);
    request_irq_disabled(IRQ_GPIO2, 0, gpio_irq_handler, "GPIO_PC", (void *)GPIO_PORT_C);
    request_irq_disabled(IRQ_GPIO3, 0, gpio_irq_handler, "GPIO_PD", (void *)GPIO_PORT_D);
    request_irq_disabled(IRQ_GPIO4, 0, gpio_irq_handler, "GPIO_PE", (void *)GPIO_PORT_E);
}

enum gpio_function soc_gpio_get_func(int gpio)
{
    int port = gpio / 32;
    unsigned int pin = gpio % 32;

    unsigned int pull;
    enum gpio_function function;

    assert(gpio >= 0 && gpio <= 5 * 32);

    unsigned int intr = !!(gpio_read(port, PXINT) & (1 << pin));
    unsigned int mask = !!(gpio_read(port, PXMSK) & (1 << pin));
    unsigned int pat1 = !!(gpio_read(port, PXPAT1) & (1 << pin));
    unsigned int pat0 = !!(gpio_read(port, PXPAT0) & (1 << pin));

    unsigned int func = intr << 3 | mask << 2 | pat1 << 1 | pat0;

    unsigned int pu = !!(gpio_read(port, PXPU) & (1 << pin));
    unsigned int pd = !!(gpio_read(port, PXPD) & (1 << pin));

    if (pu)
        pull = 1;
    else if (pd)
        pull = 2;
    else
        pull = 0;

    function = (((func << GPIO_FUNC_OFFSET) | GPIO_FUNC_0)) | \
               (((pull << GPIO_PULL_OFFSET) | GPIO_PULL_HIZ));

    return function;
}

void dvp_gpio_voltage_set(int voltage)
{
    assert(voltage >= 0);

    if (voltage == SOC_GPIO_VOLTAGE_3_3V) {
        cpm_clear_bit(DVP_VOLTAGE_BIT, CPM_EXCLK_DS);
        printf("gpio: VDDIO_CIM(PA00~PA17) = 3.3V\n");
    }
    else if (voltage == SOC_GPIO_VOLTAGE_1_8V) {
        cpm_set_bit(DVP_VOLTAGE_BIT, CPM_EXCLK_DS);
        printf("gpio: VDDIO_CIM(PA00~PA17) = 1.8V\n");
    }
}

void sd_gpio_voltage_set(int voltage)
{
    assert(voltage >= 0);

    if (voltage == SOC_GPIO_VOLTAGE_3_3V) {
        cpm_clear_bit(SD_VOLTAGE_BIT, CPM_EXCLK_DS);
        printf("gpio: VDDIO_SD(PE00~PE05) = 3.3V\n");
    }
    else if (voltage == SOC_GPIO_VOLTAGE_1_8V) {
        cpm_set_bit(SD_VOLTAGE_BIT, CPM_EXCLK_DS);
        printf("gpio: VDDIO_SD(PE00~PE05) = 1.8V\n");
    }
}

int gpio_get_voltage(unsigned int adc_ch, int R0, int R1)
{
    unsigned int val;
    int adc_voltage, real_voltage;

    adc_init();

    val = adc_read_data(adc_ch);

    adc_voltage = val * ADC_VREF_VOLTAGE / 4096;

    /* 电路分压, 根据实际电路计算 */
    real_voltage = adc_voltage * (R0 + R1) / R1;

    adc_deinit();

    return real_voltage;
}

void gpio_voltage_init(void)
{
    /* VDDIO_CIM/VDDIO_SD 可以通过手动直接配置；
       也可以通过 ADC 读出实际连接电压进行配置 */

#ifdef CONFIG_SOC_X2000_VDDIO_DVP_BASED_ON_SADC
    /* 基于 SADC 读出 VDDIO_CIM 接入电压, 然后进行配置 */
    unsigned int dvp_adc_ch = CONFIG_SOC_X2000_VDDIO_DVP_ADC_CHANNEL;
    unsigned int dvp_R0 = CONFIG_SOC_X2000_VDDIO_DVP_R0;
    unsigned int dvp_R1 = CONFIG_SOC_X2000_VDDIO_DVP_R1;
    unsigned int dvp_voltage = gpio_get_voltage(dvp_adc_ch, dvp_R0, dvp_R1);

    /* 单位mV */
    if (dvp_voltage >= 1600 && dvp_voltage <= 1980)
        dvp_gpio_voltage_set(SOC_GPIO_VOLTAGE_1_8V);
    else if (dvp_voltage >= 3000 && dvp_voltage <= 3630)
        dvp_gpio_voltage_set(SOC_GPIO_VOLTAGE_3_3V);
    else {
        printf("gpio dvp voltage set failed, voltage: %d\n", dvp_voltage);
        hang();
    }
#elif defined(CONFIG_SOC_X2000_VDDIO_DVP_VOLTAGE_VALUE)
    dvp_gpio_voltage_set(CONFIG_SOC_X2000_VDDIO_DVP_VOLTAGE_VALUE);
#endif /* CONFIG_SOC_GPIO_VDDIO_DVP_AUTO_CONFIG */

#ifdef CONFIG_SOC_X2000_VDDIO_SD_BASED_ON_SADC
    /* 基于 SADC 读出 VDDIO_SD 接入电压, 然后进行配置 */
    unsigned int sd_adc_ch = CONFIG_SOC_X2000_VDDIO_SD_ADC_CHANNEL;
    unsigned int sd_R0 = CONFIG_SOC_X2000_VDDIO_SD_R0;
    unsigned int sd_R1 = CONFIG_SOC_X2000_VDDIO_SD_R1;
    unsigned int sd_voltage = gpio_get_voltage(sd_adc_ch, sd_R0, sd_R1);

    /* 单位mV */
    if (sd_voltage >= 1600 && sd_voltage <= 1980)
        sd_gpio_voltage_set(SOC_GPIO_VOLTAGE_1_8V);
    else if (sd_voltage >= 3000 && sd_voltage <= 3630)
        sd_gpio_voltage_set(SOC_GPIO_VOLTAGE_3_3V);
    else {
        printf("gpio sd voltage set failed, voltage: %d\n", sd_voltage);
        hang();
    }
#elif defined(CONFIG_SOC_X2000_VDDIO_SD_VOLTAGE_VALUE)
    sd_gpio_voltage_set(CONFIG_SOC_X2000_VDDIO_SD_VOLTAGE_VALUE);
#endif /* CONFIG_SOC_GPIO_VDDIO_SD_AUTO_CONFIG */
}
