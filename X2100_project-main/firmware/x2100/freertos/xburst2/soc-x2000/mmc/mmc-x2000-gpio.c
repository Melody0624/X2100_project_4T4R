/*
 * Copyright (c) 2021, Ingenic Semiconductor
 *
 */

#include <driver/gpio.h>
#include "mmc_cpm.h"
#include "mmc-core.h"

/*
 * MMC0
 */
#define GPIO_MMC0_CLK                   GPIO_PD(17)
#define GPIO_MMC0_CMD                   GPIO_PD(18)
#define GPIO_MMC0_D0                    GPIO_PD(19)
#define GPIO_MMC0_D1                    GPIO_PD(20)
#define GPIO_MMC0_D2                    GPIO_PD(21)
#define GPIO_MMC0_D3                    GPIO_PD(22)
#define GPIO_MMC0_D4                    GPIO_PD(23)
#define GPIO_MMC0_D5                    GPIO_PD(24)
#define GPIO_MMC0_D6                    GPIO_PD(25)
#define GPIO_MMC0_D7                    GPIO_PD(26)

/*
 * SDIO1 (无MMC1)
 */
#define GPIO_SDIO1_CLK                  GPIO_PD(8)
#define GPIO_SDIO1_CMD                  GPIO_PD(9)
#define GPIO_SDIO1_D0                   GPIO_PD(10)
#define GPIO_SDIO1_D1                   GPIO_PD(11)
#define GPIO_SDIO1_D2                   GPIO_PD(12)
#define GPIO_SDIO1_D3                   GPIO_PD(13)

/*
 * MMC2
 */
#define GPIO_MMC2_CLK                   GPIO_PE(0)
#define GPIO_MMC2_CMD                   GPIO_PE(1)
#define GPIO_MMC2_D0                    GPIO_PE(2)
#define GPIO_MMC2_D1                    GPIO_PE(3)
#define GPIO_MMC2_D2                    GPIO_PE(4)
#define GPIO_MMC2_D3                    GPIO_PE(5)

static inline void gpio_init(int gpio, enum gpio_function func, const char *name)
{
    assert(!gpio_request(gpio, name));
    gpio_set_func(gpio , func);
}

/*
 * MMC GPIO initialization
 */
static inline void soc_mmc0_init_gpio_pd_4bit(void)
{
    gpio_init(GPIO_MMC0_CLK, GPIO_FUNC_0, "mmc0_clk");
    gpio_init(GPIO_MMC0_CMD, GPIO_FUNC_0, "mmc0_cmd");

    gpio_init(GPIO_MMC0_D0,  GPIO_FUNC_0, "mmc0_d0");
    gpio_init(GPIO_MMC0_D1,  GPIO_FUNC_0, "mmc0_d1");
    gpio_init(GPIO_MMC0_D2,  GPIO_FUNC_0, "mmc0_d2");
    gpio_init(GPIO_MMC0_D3,  GPIO_FUNC_0, "mmc0_d3");
}

static inline void soc_mmc0_init_gpio_pd_8bit(void)
{
    gpio_init(GPIO_MMC0_CLK, GPIO_FUNC_0, "mmc0_clk");
    gpio_init(GPIO_MMC0_CMD, GPIO_FUNC_0, "mmc0_cmd");

    gpio_init(GPIO_MMC0_D0,  GPIO_FUNC_0, "mmc0_d0");
    gpio_init(GPIO_MMC0_D1,  GPIO_FUNC_0, "mmc0_d1");
    gpio_init(GPIO_MMC0_D2,  GPIO_FUNC_0, "mmc0_d2");
    gpio_init(GPIO_MMC0_D3,  GPIO_FUNC_0, "mmc0_d3");
    gpio_init(GPIO_MMC0_D4,  GPIO_FUNC_0, "mmc0_d4");
    gpio_init(GPIO_MMC0_D5,  GPIO_FUNC_0, "mmc0_d5");
    gpio_init(GPIO_MMC0_D6,  GPIO_FUNC_0, "mmc0_d6");
    gpio_init(GPIO_MMC0_D7,  GPIO_FUNC_0, "mmc0_d7");
}

static void soc_mmc1_init_gpio_pd_4bit(void)
{
    gpio_init(GPIO_SDIO1_CMD, GPIO_FUNC_0, "sdio1_cmd");
    gpio_init(GPIO_SDIO1_CLK, GPIO_FUNC_0, "sdio1_clk");

    gpio_init(GPIO_SDIO1_D0,  GPIO_FUNC_0, "sdio1_d0");
    gpio_init(GPIO_SDIO1_D1,  GPIO_FUNC_0, "sdio1_d1");
    gpio_init(GPIO_SDIO1_D2,  GPIO_FUNC_0, "sdio1_d2");
    gpio_init(GPIO_SDIO1_D3,  GPIO_FUNC_0, "sdio1_d3");
}

static void soc_mmc2_init_gpio_pe_4bit(void)
{
    gpio_init(GPIO_MMC2_CMD, GPIO_FUNC_0, "mmc2_cmd");
    gpio_init(GPIO_MMC2_CLK, GPIO_FUNC_0, "mmc2_clk");

    gpio_init(GPIO_MMC2_D0,  GPIO_FUNC_0, "mmc2_d0");
    gpio_init(GPIO_MMC2_D1,  GPIO_FUNC_0, "mmc2_d1");
    gpio_init(GPIO_MMC2_D2,  GPIO_FUNC_0, "mmc2_d2");
    gpio_init(GPIO_MMC2_D3,  GPIO_FUNC_0, "mmc2_d3");
}

void soc_mmc_gpio_init(int index)
{
    switch (index) {
    case 0:
#ifdef CONFIG_SOC_X2000_MSC0_PD_8BIT
        soc_mmc0_init_gpio_pd_8bit();
#else
        soc_mmc0_init_gpio_pd_4bit();
#endif
        break;


    case 1:
        soc_mmc1_init_gpio_pd_4bit();
        break;


    case 2:
        soc_mmc2_init_gpio_pe_4bit();
        break;

    default:
        panic("mmc%d is not support.\n", index);
        break;
    }
}