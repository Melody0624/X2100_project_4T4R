#include <common.h>
#include <soc/base.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include <driver/clk.h>
#include <driver/cache.h>
#include <spinlock.h>
#include <os.h>
#include <assert.h>
#include <driver/pwm.h>

#define PWM_CCFG0       0x00 // need spin look
#define PWM_CCFG1       0x04 // need spin look
#define PWM_ENS         0x10
#define PWM_ENC         0x14
#define PWM_EN          0x18
#define PWM_UP          0x20
#define PWM_BUSY        0x24
#define PWM_INITR       0x30 // need spin look
#define PWM_WCFG(n)     (0xb0 + (n)*4)

#define PWM_DES         0x100
#define PWM_DEC         0x104
#define PWM_DE          0x108
#define PWM_DCR0        0x110 // need spin look
#define PWM_DCR1        0x114 // need spin look
#define PWM_DTRIG       0x120
#define PWM_DFER        0x124
#define PWM_DFSM        0x128
#define PWM_DSR         0x130
#define PWM_DSCR        0x134
#define PWM_DINTC       0x138 // need spin look
#define PWM_DAR(n)      (0x140 + (n)*4)
#define PWM_DTLR(n)     (0x190 + (n)*4)
#define PWM_OEN          0x300 // need spin look

#define PWM_CCFG0_div(id)       (id * 4), (id * 4 + 3)
#define PWM_CCFG1_div(id)       ((id - 8) * 4), ((id - 8) * 4 + 3)
#define PWM_WAVEFROM_HIGH   16, 31
#define PWM_WAVWFROM_LOW    0, 15

#define PWM_NUMS 16
#define PWM_DUTY_MAX_COUNT   (0xFFFF)

#define PWM_REG_BASE  KSEG1ADDR(PWM_IOBASE)
#define PWM_ADDR(reg) ((volatile unsigned long *)(PWM_REG_BASE + (reg)))

#define PWM_CONFIG_REG_MODE 1
#define PWM_CONFIG_DMA_MODE 2


static inline void pwm_write_reg(unsigned int reg, unsigned int value)
{
    *PWM_ADDR(reg) = value;
}

static inline unsigned int pwm_read_reg(unsigned int reg)
{
    return *PWM_ADDR(reg);
}

static inline void pwm_set_bit(unsigned int reg, int bit, unsigned int value)
{
    set_bit_field_v(PWM_ADDR(reg), bit, bit, value);
}

static inline unsigned int pwm_get_bit(unsigned int reg, int bit)
{
    return get_bit_field_v(PWM_ADDR(reg), bit, bit);
}

static inline void pwm_set_bits(unsigned int reg, int start, int end, unsigned int value)
{
    set_bit_field_v(PWM_ADDR(reg), start, end, value);
}

static inline unsigned int pwm_get_bits(unsigned int reg, int start, int end)
{
    return get_bit_field_v(PWM_ADDR(reg), start, end);
}

/**********************************************************/


struct jz_pwm_gpio {
    const char *name;
    int id;
    int gpio;
    int func;
};

struct jz_pwm {
    const char *name;
    unsigned int is_config;
    unsigned int is_request;
    unsigned int is_enable;
    unsigned int func_is_clear;
    unsigned int not_really_disable;
    unsigned int not_really_enable;

    unsigned long rate;
    unsigned long real_rate;
    unsigned long half_num;
    unsigned long full_num;
    unsigned int levels;
    unsigned int idle_level;
    unsigned int high_levels;
    unsigned int low_levels;
    struct jz_pwm_gpio *gpio_def;

    unsigned int init_level;
    unsigned int dma_loop;
    void *dma_data;

    spinlock_t lock;
    thread_waiter_t dma_wait;
    volatile unsigned int dma_busy;
};

static DEFINE_SPINLOCK(pwm_lock);
static struct jz_pwm pwmdata[PWM_NUMS];

static struct clk *pwm_gate_clk;
static struct clk *pwm_clk;
static unsigned long clk_src_rate;

struct jz_pwm_gpio pwm_gpio_array[] = {
    { .name = "pwm0", .id = 0, .func = GPIO_FUNC_0, .gpio = GPIO_PC(0), },
    { .name = "pwm1", .id = 1, .func = GPIO_FUNC_0, .gpio = GPIO_PC(1), },
    { .name = "pwm2", .id = 2, .func = GPIO_FUNC_0, .gpio = GPIO_PC(2), },
    { .name = "pwm3", .id = 3, .func = GPIO_FUNC_0, .gpio = GPIO_PC(3), },
    { .name = "pwm4", .id = 4, .func = GPIO_FUNC_0, .gpio = GPIO_PC(4), },
    { .name = "pwm5", .id = 5, .func = GPIO_FUNC_0, .gpio = GPIO_PC(5), },
    { .name = "pwm6", .id = 6, .func = GPIO_FUNC_0, .gpio = GPIO_PC(6), },
    { .name = "pwm7", .id = 7, .func = GPIO_FUNC_0, .gpio = GPIO_PC(7), },
    { .name = "pwm8", .id = 8, .func = GPIO_FUNC_0, .gpio = GPIO_PC(8), },
    { .name = "pwm9", .id = 9, .func = GPIO_FUNC_0, .gpio = GPIO_PC(9), },
    { .name = "pwm10", .id = 10, .func = GPIO_FUNC_0, .gpio = GPIO_PC(10), },
    { .name = "pwm11", .id = 11, .func = GPIO_FUNC_0, .gpio = GPIO_PC(11), },
    { .name = "pwm12", .id = 12, .func = GPIO_FUNC_0, .gpio = GPIO_PC(12), },
    { .name = "pwm13", .id = 13, .func = GPIO_FUNC_0, .gpio = GPIO_PC(13), },
    { .name = "pwm14", .id = 14, .func = GPIO_FUNC_0, .gpio = GPIO_PC(14), },
    { .name = "pwm15", .id = 15, .func = GPIO_FUNC_0, .gpio = GPIO_PC(15), },

    { .name = "pwm0", .id = 0, .func = GPIO_FUNC_2, .gpio = GPIO_PD(30), },
    { .name = "pwm1", .id = 1, .func = GPIO_FUNC_2, .gpio = GPIO_PD(31), },
    { .name = "pwm2", .id = 2, .func = GPIO_FUNC_1, .gpio = GPIO_PE(0), },
    { .name = "pwm3", .id = 3, .func = GPIO_FUNC_1, .gpio = GPIO_PE(1), },
    { .name = "pwm4", .id = 4, .func = GPIO_FUNC_1, .gpio = GPIO_PE(2), },
    { .name = "pwm5", .id = 5, .func = GPIO_FUNC_1, .gpio = GPIO_PE(3), },
    { .name = "pwm6", .id = 6, .func = GPIO_FUNC_1, .gpio = GPIO_PE(4), },
    { .name = "pwm7", .id = 7, .func = GPIO_FUNC_1, .gpio = GPIO_PE(5), },
};

int soc_pwm_dma_disable_loop(int id);

static struct jz_pwm_gpio *pwm_get_gpio_func_def(int gpio)
{
    int i;
    struct jz_pwm_gpio *def;

    for (i = 0; i < ARRAY_SIZE(pwm_gpio_array); i++) {
        def = &pwm_gpio_array[i];
        if (def->gpio == gpio)
            return def;
    }

    return NULL;
}

static void pwm_set_clk_div(int id)
{
    unsigned long div, tmp;
    unsigned long flags;

    tmp = clk_src_rate / pwmdata[id].rate;

    switch (tmp) {
        case 1:div = 0;break;
        case 2:div = 1;break;
        case 4:div = 2;break;
        case 8:div = 3;break;
        case 16:div = 4;break;
        case 32:div = 5;break;
        case 64:div = 6;break;
        case 128:div = 7;break;
        default:panic("PWM: %s, pwm ch %d clk div must be [1 2 4 8 16 32 64 128]!\n", __func__, id);
    }

    spin_lock_irqsave(&pwm_lock, flags);

    if (id < 8)
        pwm_set_bits(PWM_CCFG0, PWM_CCFG0_div(id), div);
    else
        pwm_set_bits(PWM_CCFG1, PWM_CCFG1_div(id), div);

    spin_unlock_irqrestore(&pwm_lock, flags);
}


static void pwm_disable(int id)
{
    unsigned long flags;

    if (!pwmdata[id].is_enable)
        return;

    /* disable pwm */
    spin_lock_irqsave(&pwm_lock, flags);
    pwm_set_bit(PWM_OEN, id, 0);
    spin_unlock_irqrestore(&pwm_lock, flags);
    pwm_write_reg(PWM_ENC, 1 << id);

    pwmdata[id].is_enable = 0;
}

static void pwm_enable(int id)
{
    unsigned long flags;

    if (pwmdata[id].is_enable)
        return;

    gpio_set_func(pwmdata[id].gpio_def->gpio, pwmdata[id].gpio_def->func);

    /* enable pwm */
    if (!pwmdata[id].not_really_enable) {
        pwm_write_reg(PWM_ENS, 1 << id);
        spin_lock_irqsave(&pwm_lock, flags);
        pwm_set_bit(PWM_OEN, id, 1);
        spin_unlock_irqrestore(&pwm_lock, flags);
    }

    pwmdata[id].is_enable = 1;
}

static void set_pwm_mode(int id, int dma_mode)
{
    unsigned long flags;

    spin_lock_irqsave(&pwm_lock, flags);
    pwm_set_bit(PWM_DCR0, id, !!dma_mode);
    spin_unlock_irqrestore(&pwm_lock, flags);
}

static void set_pwm_dma_loop(int id, int dma_loop)
{
    unsigned long flags;

    spin_lock_irqsave(&pwm_lock, flags);
    pwm_set_bit(PWM_DCR1, id, !!dma_loop);
    spin_unlock_irqrestore(&pwm_lock, flags);
}

static void set_pwm_init_level(int id, int level)
{
    unsigned long flags;

    spin_lock_irqsave(&pwm_lock, flags);
    pwm_set_bit(PWM_INITR, id, !!level);
    spin_unlock_irqrestore(&pwm_lock, flags);
}

static void set_pwm_idle_level(int id, int level)
{
    unsigned long flags;

    spin_lock_irqsave(&pwm_lock, flags);
    pwm_set_bit(PWM_INITR, id + 16, !!level);
    spin_unlock_irqrestore(&pwm_lock, flags);
}

static void set_pwm_irq_mask(int id, int mask)
{
    unsigned long flags;

    /* clear irq flag */
    pwm_write_reg(PWM_DSCR, 1 << id);

    spin_lock_irqsave(&pwm_lock, flags);
    pwm_set_bit(PWM_DINTC, id, !!mask);
    spin_unlock_irqrestore(&pwm_lock, flags);
}

static void get_pwm_source_rate_freq_first(int id, unsigned long freq)
{
    int i;
    unsigned long rate;

    i = 0;
    rate = clk_src_rate;
    while((rate / freq > PWM_DUTY_MAX_COUNT) && (i < 7)) {
        rate = rate / 2;
        i++;
    }

    pwmdata[id].rate = rate;
    pwmdata[id].full_num = rate / freq;
    if (pwmdata[id].full_num > PWM_DUTY_MAX_COUNT)
        pwmdata[id].full_num = PWM_DUTY_MAX_COUNT;

    pwmdata[id].real_rate = rate / pwmdata[id].full_num;
}

static void get_pwm_source_rate_levels_first(int id, unsigned long freq, unsigned long levels)
{
    int i;
    unsigned long rate;
    unsigned long delta;

    i = 0;
    rate = clk_src_rate;
    while((rate / freq > PWM_DUTY_MAX_COUNT) && (i < 7)) {
        rate = rate / 2;
        i++;
    }

    pwmdata[id].rate = rate;

    if (rate / freq < levels) {
        pwmdata[id].full_num = levels;
    } else {
        delta = (rate / freq) % levels;
        pwmdata[id].full_num = rate / freq - delta;
    }

    if (pwmdata[id].full_num > PWM_DUTY_MAX_COUNT)
        pwmdata[id].full_num = PWM_DUTY_MAX_COUNT;

    pwmdata[id].real_rate = rate / pwmdata[id].full_num;
}

void soc_pwm_set_not_really_disable(int id, int enable)
{
    pwmdata[id].not_really_disable = !!enable;
}

void soc_pwm_set_not_really_enable(int id, int enable)
{
    pwmdata[id].not_really_enable = !!enable;
}

void soc_pwm_enable_channels(unsigned int channels)
{
    pwm_write_reg(PWM_ENS, channels);
}

void soc_pwm_disable_channels(unsigned int channels)
{
    pwm_write_reg(PWM_ENC, channels);
}

int soc_pwm_config(int id, struct pwm_config_data *config)
{
    int ret = 0;
    unsigned long freq = config->freq;
    unsigned long levels = config->levels;
    unsigned int idle_level = config->idle_level;

    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: %s, No support pwm ch %d !\n", __func__, id);
        return -1;
    }

    if (freq == 0) {
        printf("PWM: pwm ch %d only support frequency high than 0 Hz\n", id);
        return -1;
    }

    if (freq > clk_src_rate) {
        printf("PWM: pwm only support frequency low than %lu\n", clk_src_rate);
        return -1;
    }

    if (levels >= PWM_DUTY_MAX_COUNT) {
        printf("PWM: pwm ch %d levels need less than %d\n", id, PWM_DUTY_MAX_COUNT);
        return -1;
    }

    spin_lock(&pwmdata[id].lock);

    if (pwmdata[id].dma_busy == 1) {
        printf("PWM: pwm ch%d is dma busy!\n", id);
        ret = -1;
        goto unlock;
    }

    if (!pwmdata[id].is_request) {
        printf("PWM: pwm ch %d is not request!\n", id);
        ret = -1;
        goto unlock;
    }

    if (pwmdata[id].is_enable) {
        printf("PWM: pwm ch %d Cannot configure at working\n", id);
        ret = -1;
        goto unlock;
    }

    pwmdata[id].levels = levels;
    pwmdata[id].idle_level = !!idle_level;

    if (config->accuracy_priority == PWM_accuracy_freq_first)
        get_pwm_source_rate_freq_first(id, freq);
    else
        get_pwm_source_rate_levels_first(id, freq, levels);

    /* set pwm clk div */
    pwm_set_clk_div(id);

    /* set pwm level */
    set_pwm_idle_level(id, pwmdata[id].idle_level);
    set_pwm_init_level(id, !pwmdata[id].idle_level);

    /* updata mode */
    set_pwm_mode(id, 0);

    pwmdata[id].is_config = PWM_CONFIG_REG_MODE;

unlock:
    spin_unlock(&pwmdata[id].lock);

    return ret;
}

int soc_pwm_request(int gpio, const char *name)
{
    int id;
    char buf[8];
    struct jz_pwm_gpio *gpio_def;

    gpio_def = pwm_get_gpio_func_def(gpio);
    if (gpio_def == NULL) {
        printf("PWM: %s not support as pwm.\n", gpio_to_str(gpio, buf, sizeof(buf)));
        return -1;
    }

    id = gpio_def->id;

    spin_lock(&pwmdata[id].lock);

    if (pwmdata[id].is_request)
        goto unlock;

    if (gpio_request(gpio, name)) {
        printf("PWM: pwm ch %d, request IO %s error !\n", id, gpio_to_str(gpio, buf, sizeof(buf)));
        id = -1;
        goto unlock;
    }

    pwmdata[id].name = name;
    pwmdata[id].gpio_def = gpio_def;
    pwmdata[id].rate = clk_src_rate;
    pwmdata[id].is_request = 1;
    pwmdata[id].dma_busy = 0;

unlock:
    spin_unlock(&pwmdata[id].lock);

    return id;
}

int soc_pwm_set_level(int id, unsigned long level)
{
    int ret = 0;
    unsigned long pwm_level = 0;

    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: %s, No support pwm ch %d !\n", __func__, id);
        return -1;
    }


    spin_lock(&pwmdata[id].lock);

    if (pwmdata[id].dma_busy == 1) {
        printf("PWM: pwm ch%d is dma busy!\n", id);
        ret = -1;
        goto unlock;
    }

    if (pwmdata[id].is_config != PWM_CONFIG_REG_MODE) {
        printf("PWM: pwm ch%d is not config!\n", id);
        ret = -1;
        goto unlock;
    }

    if (level > pwmdata[id].levels) {
        printf("PWM: pwm ch%d set level more than max level!\n", id);
        ret = -1;
        goto unlock;
    }

    if (level != 0) {
        level = level * pwmdata[id].full_num / pwmdata[id].levels;
        if (level == 0)
            level = 1;
    }

    pwmdata[id].half_num = level;

    /* set pwm waveform config */
    if (pwmdata[id].idle_level) {
        pwmdata[id].high_levels = pwmdata[id].full_num - level;
        pwmdata[id].low_levels = level;
    } else {
        pwmdata[id].low_levels = pwmdata[id].full_num - level;
        pwmdata[id].high_levels = level;
    }

    /* set pwm init level */
    if (pwmdata[id].low_levels == 0) {
        pwmdata[id].func_is_clear = 1;
        gpio_set_func(pwmdata[id].gpio_def->gpio, GPIO_OUTPUT1);
        set_bit_field(&pwm_level, PWM_WAVWFROM_LOW, 1);
        set_bit_field(&pwm_level, PWM_WAVEFROM_HIGH, pwmdata[id].full_num - 1);
        pwm_write_reg(PWM_WCFG(id), pwm_level);
        // pwm_write_reg(PWM_UP, 1 << id);

        if (!pwmdata[id].not_really_disable)
            pwm_disable(id);
        goto unlock;
    } else if (pwmdata[id].high_levels == 0) {
        pwmdata[id].func_is_clear = 1;
        gpio_set_func(pwmdata[id].gpio_def->gpio, GPIO_OUTPUT0);
        set_bit_field(&pwm_level, PWM_WAVWFROM_LOW, pwmdata[id].full_num - 1);
        set_bit_field(&pwm_level, PWM_WAVEFROM_HIGH, 1);
        pwm_write_reg(PWM_WCFG(id), pwm_level);
        // pwm_write_reg(PWM_UP, 1 << id);

        if (!pwmdata[id].not_really_disable)
            pwm_disable(id);
        goto unlock;
    }
    if (pwmdata[id].func_is_clear) {
        pwmdata[id].func_is_clear = 0;
        gpio_set_func(pwmdata[id].gpio_def->gpio, pwmdata[id].gpio_def->func);
    }

    set_bit_field(&pwm_level, PWM_WAVWFROM_LOW, pwmdata[id].low_levels);
    set_bit_field(&pwm_level, PWM_WAVEFROM_HIGH, pwmdata[id].high_levels);
    pwm_write_reg(PWM_WCFG(id), pwm_level);

    /* set pwm updata */
    pwm_write_reg(PWM_UP, 1 << id);

    pwm_enable(id);

    /* wait to update pwm */
    if (!pwmdata[id].not_really_enable) {
        udelay(1);
        while(pwm_get_bit(PWM_BUSY, id));
    }

unlock:
    spin_unlock(&pwmdata[id].lock);
    return ret;
}

void soc_pwm_release(int id)
{
    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: %s, No support pwm ch %d !\n", __func__, id);
        return;
    }

    spin_lock(&pwmdata[id].lock);

    if (pwmdata[id].is_config == PWM_CONFIG_DMA_MODE && pwmdata[id].dma_loop)
        soc_pwm_dma_disable_loop(id);

    pwm_disable(id);

    if (pwmdata[id].is_request) {
        gpio_release(pwmdata[id].gpio_def->gpio);

        pwmdata[id].is_request = 0;
    }

    pwmdata[id].is_config = 0;

    spin_unlock(&pwmdata[id].lock);
}

unsigned long soc_pwm_get_freq(int id)
{
    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: %s, No support pwm ch %d !\n", __func__, id);
        return -1;
    }

    return pwmdata[id].real_rate;
}


int soc_pwm_dma_init(int id, struct pwm_dma_config *dma_config)
{
    int ret = 0;

    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: No support pwm ch %d !\n", id);
        return -1;
    }

    spin_lock(&pwmdata[id].lock);

    if (!pwmdata[id].is_request) {
        printf("PWM: pwm ch %d is not request!\n", id);
        ret = -1;
        goto unlock;
    }

    if (pwmdata[id].is_enable) {
        printf("PWM: pwm ch %d Cannot configure at working\n", id);
        ret = -1;
        goto unlock;
    }

    /* set pwm clk div, count clk must not less than AHB2 clk*/
    pwmdata[id].rate = clk_src_rate;
    pwm_set_clk_div(id);

    /* updata dma mode */
    set_pwm_mode(id, 1);

    /* set pwm idle and init level */
    pwmdata[id].init_level = !!dma_config->start_level;
    pwmdata[id].idle_level = !!dma_config->idle_level;
    set_pwm_init_level(id, pwmdata[id].init_level);
    set_pwm_idle_level(id, pwmdata[id].idle_level);

    pwmdata[id].is_config = PWM_CONFIG_DMA_MODE;
    ret = pwmdata[id].rate;

unlock:
    spin_unlock(&pwmdata[id].lock);
    return ret;
}

int soc_pwm_dma_update(int id, struct pwm_dma_data *dma_data)
{
    int ret = 0;
    unsigned long flags;

    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: No support pwm ch %d !\n", id);
        return -1;
    }

    if (dma_data == NULL || dma_data->data == NULL || dma_data->data_count == 0) {
        printf("PWM: pwm ch%d not have dma data!\n", id);
        return -1;
    }

    if (dma_data->dma_loop && dma_data->data_count % 4) {
        printf("PWM: pwm ch%d dma data length must 4 word align!\n", id);
        return -1;
    }

#ifdef PWM_CHECK_DMA_DATA
    for (int i = 0; i < dma_data->data_count; i++) {
        if (dma_data->data[i].high == 0 || dma_data->data[i].low == 0) {
            printf("PWM: pwm ch%d dma mode, data cannot be zero\n", id);
            return -1;
        }
    }
#endif

    spin_lock_irqsave(&pwmdata[id].lock, flags);

    if (pwmdata[id].dma_busy == 1) {
        printf("PWM: pwm ch%d is dma busy!\n", id);
        return -1;
    }

    if (pwmdata[id].is_config != PWM_CONFIG_DMA_MODE) {
        printf("PWM: pwm ch%d is not config dma mode!\n", id);
        ret = -1;
        goto unlock;
    }

    if (pwmdata[id].dma_loop) {
        printf("PWM: pwm ch%d work in dma loop mode\n", id);
        ret = -1;
        goto unlock;
    }

    assert(!pwmdata[id].dma_data);

    pwmdata[id].dma_data = dma_data->data;

    /* update dma loop mode */
    pwmdata[id].dma_loop = !!dma_data->dma_loop;
    set_pwm_dma_loop(id, pwmdata[id].dma_loop);

    flush_dcache_force((unsigned long)pwmdata[id].dma_data, dma_data->data_count * sizeof(struct pwm_data));
    /* set dma data_addr and data_len */
    pwm_write_reg(PWM_DAR(id), virt_to_phys(pwmdata[id].dma_data));
    pwm_write_reg(PWM_DTLR(id), dma_data->data_count);

    /* enable irq */
    set_pwm_irq_mask(id, pwmdata[id].dma_loop);

    /* enable dma */
    pwm_write_reg(PWM_DES, 1 << id);
    /* trigger dma */
    pwm_write_reg(PWM_DTRIG, 1 << id);

    pwm_enable(id);

    /* normal mode wait dma complete */
    if (!pwmdata[id].not_really_enable && !dma_data->dma_loop) {
        /* wait dma_end */
        pwmdata[id].dma_busy = 1;
        spin_unlock_irqrestore(&pwmdata[id].lock, flags);
        thread_waiter_wait(&pwmdata[id].dma_wait);
        spin_lock_irqsave(&pwmdata[id].lock, flags);
        pwmdata[id].dma_busy = 0;
        /* disable irq */
        set_pwm_irq_mask(id, 1);

        /* disable dma */
        pwm_write_reg(PWM_DEC, 1 << id);

        /* wait fifo data is empty */
        while (!pwm_get_bit(PWM_DFER, id));

        pwmdata[id].dma_data = NULL;
    }
unlock:
    spin_unlock_irqrestore(&pwmdata[id].lock, flags);

    return ret;
}

int soc_pwm_dma_disable_loop(int id)
{
    int ret = 0;

    if (id < 0 || id >= PWM_NUMS) {
        printf("PWM: No support pwm ch %d !\n", id);
        return -1;
    }

    spin_lock(&pwmdata[id].lock);
    if (pwmdata[id].is_config != PWM_CONFIG_DMA_MODE) {
        printf("PWM: pwm ch%d is not config dma mode!\n", id);
        ret = -1;
        goto unlock;
    }

    if (!pwmdata[id].is_enable) {
        printf("PWM: pwm ch%d dma is not enable\n", id);
        ret = -1;
        goto unlock;
    }

    if (!pwmdata[id].dma_loop) {
        printf("PWM: pwm ch%d dma is not loop\n", id);
        ret = -1;
        goto unlock;
    }

    /* disable dma */
    pwm_write_reg(PWM_DEC, 1 << id);

    /* wait fifo data is empty */
    while (!pwm_get_bit(PWM_DFER, id));

    pwmdata[id].dma_data = NULL;
    pwmdata[id].dma_loop = 0;

unlock:
    spin_unlock(&pwmdata[id].lock);
    return ret;
}

static void pwm_irq_handler(int irq, void *data)
{
    int i;
    unsigned int end_status_reg;

    end_status_reg = pwm_read_reg(PWM_DSR);
    for (i = 0; i < PWM_NUMS; i++) {
        if ((end_status_reg >> i) & 0x1)
            thread_waiter_wakeup(&pwmdata[i].dma_wait);
    }
    pwm_write_reg(PWM_DSCR, end_status_reg);
}

int soc_pwm_init(void)
{
    int i;
    struct clk *ahb2_clk;
    struct clk *mpll_clk;

    ahb2_clk = clk_get("h2clk");
    assert(ahb2_clk);
    clk_src_rate = clk_get_rate(ahb2_clk);
    clk_put(ahb2_clk);

    pwm_clk = clk_get("cgu_pwm");
    assert(pwm_clk);
    mpll_clk = clk_get("mpll");
    assert(mpll_clk);
    clk_set_parent(pwm_clk, mpll_clk);
    clk_set_rate(pwm_clk, clk_src_rate);
    clk_enable(pwm_clk);
    clk_put(mpll_clk);

    pwm_gate_clk = clk_get("gate_pwm");
    assert(pwm_gate_clk);
    clk_enable(pwm_gate_clk);

    for (i = 0; i < PWM_NUMS; i++) {
        spin_lock_init_recursive(&pwmdata[i].lock);
        thread_waiter_init(&pwmdata[i].dma_wait);
    }

    request_irq(IRQ_PWM, 0, pwm_irq_handler, "pwm", NULL);

    return 0;
}

void soc_pwm_exit(void)
{
    release_irq(IRQ_PWM);
    clk_disable(pwm_gate_clk);
    clk_disable(pwm_clk);

    clk_put(pwm_gate_clk);
    clk_put(pwm_clk);
}
