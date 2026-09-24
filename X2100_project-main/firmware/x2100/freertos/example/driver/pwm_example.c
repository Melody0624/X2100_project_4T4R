#include <driver/pwm.h>
#include <driver/gpio.h>
#include <common.h>

struct pwm_config_data pwm0_config = {
    .shutdown_mode = PWM_graceful_shutdown, /* 设置PWM停止输出时以一个完整的周期结束 */
    .idle_level = PWM_idle_low, /* 设置PWM空闲电平为低电平 */
    .accuracy_priority = PWM_accuracy_levels_first, /* 设置输出PWM时，优先满足pwm的级数 */
    .freq = 120000, /* 设置PWM频率为12kHz，在选择了优先满足PWM级数的情况下最终调制后的频率不等于120kHz */
    .levels = 5000, /* 设置PWM最大级数为5000 */
    .clk_id = "rtc", /* 设置PWM时钟源为RTC，可参考对应SOC中的clk.h,选填"pclk" " "rtc" "ext1"*/
};

void pwm0_test(void)
{
    int pwm0 = pwm_request(GPIO_PC(12), "pwm0");

    pwm_config(pwm0, &pwm0_config);

    /* 如果有需要可以使用 pwm_get_freq 获取 PWM 最终调制后的频率 */
    printf("real freq: %ld\n", pwm_get_freq(pwm0));

    pwm_set_level(pwm0, 500);/* 设置 level 值，输出相对应的 PWM */

    mdelay(1000);

    pwm_set_level(pwm0, 0);/* 当 level 值为0， 停止输出 PWM */
}

struct pwm_config_data pwm1_config = {
    .shutdown_mode = PWM_abrupt_shutdown, /* 设置PWM在停止输出时立刻将pwm设置成空闲时电平 */
    .idle_level = PWM_idle_high,    /* 设置PWM空闲电平为高电平 */
    .accuracy_priority = PWM_accuracy_freq_first, /* 设置输出PWM时，优先满足PWM调制后频率的精度 */
    .freq = 1000000, /* 设置PWM调制后频率为1MHz */
    .levels = 100, /* 设置PWM最大级数为100 */
    .clk_id = NULL, /* 对于置NULL的时钟源，默认选择ext1作为时钟源 */
};

void pwm1_test(void)
{
    int pwm1 = pwm_request(GPIO_PC(24), "pwm1");

    pwm_config(pwm1, &pwm1_config);

    /* 如果有需要可以使用 pwm_get_freq 获取 PWM 最终调制后的频率 */
    printf("real freq: %ld\n", pwm_get_freq(pwm1));

    pwm_set_level(pwm1, 50);

    mdelay(100);

    pwm_release(pwm1);
}

void pwm_test(void)
{
    pwm0_test();

    pwm1_test();
}

//////////////////////////////////////////////
struct pwm_data pwm_data[] = {
    {
        .low = 1000,
        .high = 1000,
    },
    {
        .low = 2000,
        .high = 2000,
    },
    {
        .low = 3000,
        .high = 3000,
    },
    {
        .low = 4000,
        .high = 4000,
    },
};

struct pwm_dma_data pwm_dma_data = {
    .data = pwm_data,
    .data_count = 4,
    .dma_loop = 1,//循环dma模式：函数不会阻塞，需要调用pwm_dma_disable_loop停止dma
};

struct pwm_dma_config dma_config;

void pwm_dma_test(void)
{
    int rate;
    int pwm0 = pwm_request(GPIO_PC(24), "pwm1");

    memset(&dma_config, 0, sizeof(struct pwm_dma_config));
    dma_config.idle_level = PWM_idle_low;
    dma_config.start_level = PWM_start_high;
    rate = pwm_dma_init(pwm0, &dma_config);
    if (rate < 0) {
        printf("pwm_dma_init failed!\n");
    }

    pwm_dma_update(pwm0, &pwm_dma_data);
}


struct pwm_config_data pwm0_sync_config = {
    .shutdown_mode = PWM_graceful_shutdown, /* 设置PWM停止输出时以一个完整的周期结束 */
    .idle_level = PWM_idle_low, /* 设置PWM空闲电平为低电平 */
    .accuracy_priority = PWM_accuracy_freq_first, /* 设置输出PWM时，优先满足pwm的频率 */
    .freq = 1000000, /* 设置PWM频率为1MHz */
    .levels = 500, /* 设置PWM最大级数为500 */
};

struct pwm_config_data pwm1_sync_config = {
    .shutdown_mode = PWM_graceful_shutdown, /* 设置PWM停止输出时以一个完整的周期结束 */
    .idle_level = PWM_idle_low, /* 设置PWM空闲电平为低电平 */
    .accuracy_priority = PWM_accuracy_freq_first, /* 设置输出PWM时，优先满足pwm的频率 */
    .freq = 1000000, /* 设置PWM频率为1MHz */
    .levels = 100, /* 设置PWM最大级数为100 */
};

void pwm_multi_channel_sync_test(void)
{
    int pwm0 = pwm_request(GPIO_PB(12), "pwm0");
    int pwm1 = pwm_request(GPIO_PB(13), "pwm1");
    unsigned int channels = 0;

    pwm_config(pwm0, &pwm0_sync_config);
    pwm_config(pwm1, &pwm1_sync_config);

    /* 如果有需要可以使用 pwm_get_freq 获取 PWM 最终调制后的频率 */
    printf("real freq: %ld\n", pwm_get_freq(pwm1));

    pwm_set_not_really_enable(pwm0, 1);
    pwm_set_not_really_enable(pwm1, 1);

    pwm_set_level(pwm0, 100);
    pwm_set_level(pwm1, 50);

    channels |= (1 << pwm0);
    channels |= (1 << pwm1);

    pwm_enable_channels(channels);

    mdelay(5000);

    pwm_disable_channels(channels);

    pwm_release(pwm0);
    pwm_release(pwm1);
}


struct pwm_data pwm0_dma_sync_data[] = {
    {
        .low = 1000,
        .high = 1000,
    },
    {
        .low = 2000,
        .high = 2000,
    },
    {
        .low = 3000,
        .high = 3000,
    },
    {
        .low = 4000,
        .high = 4000,
    },
};

struct pwm_data pwm1_dma_sync_data[] = {
    {
        .low = 4000,
        .high = 4000,
    },
    {
        .low = 3000,
        .high = 3000,
    },
    {
        .low = 2000,
        .high = 2000,
    },
    {
        .low = 1000,
        .high = 1000,
    },
};

struct pwm_dma_data pwm0_dma_sync_data_config = {
    .data = pwm0_dma_sync_data,
    .data_count = 4,
    .dma_loop = 1,
};

struct pwm_dma_data pwm1_dma_sync_data_config = {
    .data = pwm1_dma_sync_data,
    .data_count = 4,
    .dma_loop = 1,
};

struct pwm_dma_config pwm0_dma_sync_config;
struct pwm_dma_config pwm1_dma_sync_config;

void pwm_dma_multi_channel_sync_test(void)
{
    int pwm0 = pwm_request(GPIO_PB(12), "pwm0");
    int pwm1 = pwm_request(GPIO_PB(13), "pwm1");
    unsigned int channels = 0;

    pwm_set_not_really_enable(pwm0, 1);
    pwm_set_not_really_enable(pwm1, 1);

    memset(&pwm0_dma_sync_config, 0, sizeof(struct pwm_dma_config));
    pwm0_dma_sync_config.idle_level = PWM_idle_low;
    pwm0_dma_sync_config.start_level = PWM_start_high;

    memset(&pwm1_dma_sync_config, 0, sizeof(struct pwm_dma_config));
    pwm1_dma_sync_config.idle_level = PWM_idle_low;
    pwm1_dma_sync_config.start_level = PWM_start_high;

    pwm_dma_init(pwm0, &pwm0_dma_sync_config);
    pwm_dma_init(pwm1, &pwm1_dma_sync_config);

    pwm_dma_update(pwm0, &pwm0_dma_sync_data_config);
    pwm_dma_update(pwm1, &pwm1_dma_sync_data_config);

    channels |= (1 << pwm0);
    channels |= (1 << pwm1);
    pwm_enable_channels(channels);

    mdelay(5000);//延时5s后关闭pwm

    pwm_disable_channels(channels);

    pwm_release(pwm0);
    pwm_release(pwm1);
}
//////////////////////////////////////////////