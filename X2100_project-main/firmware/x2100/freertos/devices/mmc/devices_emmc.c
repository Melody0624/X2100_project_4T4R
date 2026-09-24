/*
 * Copyright (c) 2019, Ingenic Semiconductor
 *
 */

#include <common.h>
#include <driver/gpio.h>
#include <driver/irq.h>
#include "mmc-core.h"
#include "mmc_devices.h"

static struct mmc_device_gpio emmc_card_gpio = {
    .reset              = CONFIG_EMMC_DEVICE_GPIO_RESET,
    .reset_level        = 0,

    .power              = CONFIG_EMMC_DEVICE_GPIO_POWER,
    .power_level        = 0,

    .detect             = CONFIG_EMMC_DEVICE_GPIO_DETECT,
    .detect_level       = 0,

    .removal            = CONFIG_EMMC_DEVICE_REMOVABLE_TYPE,
};

static void thread_device_emmc_card_status(void *data)
{
    struct mmc_device_gpio *dev = (struct mmc_device_gpio *)data;
    int detect_gpio = dev->detect;

    mutex_init(&dev->mlock);
    thread_cond_init(&dev->cond);

    mutex_lock(&dev->mlock);

    while (1) {
        if (!dev->detect_change)
            thread_cond_wait(&dev->cond, &dev->mlock);

        dev->detect_change = 0;

        /* 可拔插默认状态为拔出 */
        int card_insert = dev->removal == MMC_DEVICE_REMOVABLE ? 0 : 1;

        if (detect_gpio)
            card_insert = gpio_get_value(detect_gpio) == dev->detect_level;
        //printf("emmc-device card detect:[%s]\n", card_insert ? "Insert" : "Removed");

        if (card_insert)
            mmc_devices_init();
        else
            mmc_devices_deinit();
    }

    mutex_unlock(&dev->mlock);
}


static void card_detect_timer_callback(struct hrtimer *hrtimer)
{
    struct mmc_device_gpio *dev = &emmc_card_gpio;

    thread_cond_broadcast(&dev->cond);
}

static void emmc_card_detect_interrupt(int irq, void *data)
{
    //printf("emmc-device detect, irq:%d\n", irq);
    struct mmc_device_gpio *dev = (struct mmc_device_gpio *)data;
    struct hrtimer *cd_timer = &dev->cd_timer;
    int debouncd_ms = 30 * 1000;
    dev->detect_change++;

    hrtimer_start(cd_timer, debouncd_ms);
}

static int emmc_gpio_init(void)
{
    struct mmc_device_gpio *gpio = &emmc_card_gpio;
    char gpio_name[10];
    int ret = 0;

    if (gpio->is_inited)
        return 0;

    if (gpio->power != -1) {
        ret = gpio_request(gpio->power, "emmc-device power");
        if (ret) {
            printf("emmc-device: request power pin(%s) failed.\n", gpio_to_str(gpio->power, gpio_name, sizeof(gpio_name)));
            goto err_power_pin;
        }
        gpio_direction_output(gpio->power, !gpio->power_level);
    }

    if (gpio->reset != -1) {
        ret = gpio_request(gpio->reset, "emmc-device reset");
        if (ret) {
            printf("emmc-device: request reset pin(%s) failed.\n", gpio_to_str(gpio->reset, gpio_name, sizeof(gpio_name)));
            goto err_reset_pin;
        }
        gpio_direction_output(gpio->reset, gpio->reset_level);
    }

    if (gpio->detect != -1) {
        ret = gpio_request(gpio->detect, "emmc-device detect");
        if (ret) {
            printf("emmc-device: request detect pin(%s) failed.\n", gpio_to_str(gpio->detect, gpio_name, sizeof(gpio_name)));
            goto err_detect_pin;
        }


        request_irq(gpio_to_irq(gpio->detect), IRQ_TYPE_EDGE_FALLING, emmc_card_detect_interrupt, "emmc-device detect", gpio);
    }

    hrtimer_init(&gpio->cd_timer, card_detect_timer_callback);

    thread_create("emmc card status", 8192, thread_device_emmc_card_status, gpio);

    gpio->is_inited = 1;

    return 0;

err_detect_pin:
    if (gpio->reset != -1) {
        gpio_release(gpio->reset);
    }

err_reset_pin:
    if (gpio->power != -1) {
        gpio_release(gpio->power);
    }

err_power_pin:
    return ret;
}

static void emmc_power_on(void)
{
    struct mmc_device_gpio *gpio = &emmc_card_gpio;

    if (gpio->power != -1) {
        gpio_direction_output(gpio->power, gpio->power_level);
        msleep(10);
    }

    if (gpio->reset != -1) {
        gpio_direction_output(gpio->reset, !gpio->reset_level);
        msleep(10);
        gpio_direction_output(gpio->reset, gpio->reset_level);
        msleep(10);
    }
}

static void emmc_power_off(void)
{
    struct mmc_device_gpio *gpio = &emmc_card_gpio;

    if (gpio->reset != -1) {
        gpio_direction_output(gpio->reset, gpio->reset_level);
    }

    if (gpio->power != -1) {
        gpio_direction_output(gpio->power, !gpio->power_level);
    }
}

/*
 * return =1: card is insert
 *        =0: card is remove
 */
static int emmc_card_detect_change(void)
{
    struct mmc_device_gpio *gpio = &emmc_card_gpio;
    int value = 0;

    if (gpio->detect != -1) {
        value = gpio_get_value(gpio->detect);
        return (value == gpio->detect_level) ? 1 : 0;

    } else {
        return 1;
    }
}

struct mmc_devices_config emmc_config = {
    .name               = "eMMC-Device",
    .index              = CONFIG_EMMC_DEVICE_BUSNUM,
    .ocr_avail          = MMC_VDD_33_34 | MMC_VDD_32_33 | MMC_VDD_165_195,
    .max_freq           = 200 * 1000 * 1000,
    .capacity           = MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_ERASE
#if (CONFIG_EMMC_DEVICE_BUSWIDTH == 8)
                        | MMC_CAP_8_BIT_DATA | MMC_CAP_4_BIT_DATA | MMC_CAP_UHS_SDR104,
#elif (CONFIG_EMMC_DEVICE_BUSWIDTH == 4)
                        | MMC_CAP_4_BIT_DATA | MMC_CAP_UHS_SDR104,
#elif (CONFIG_EMMC_DEVICE_BUSWIDTH == 1)
                        | MMC_CAP_UHS_SDR104,
#endif
    .capacity2          = MMC_CAP2_HS200,
    .gpio               = &emmc_card_gpio,
    .device_init        = emmc_gpio_init,
    .device_power_on    = emmc_power_on,
    .device_power_off   = emmc_power_off,
    .device_card_change = emmc_card_detect_change,
};
