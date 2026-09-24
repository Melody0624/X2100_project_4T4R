#include <common.h>
#include <soc/base.h>
#include <driver/irq.h>
#include <assert.h>
#include <soc/cpm.h>
#include <os.h>
#include <driver/gpio.h>
#include <driver/gpio_pin.h>

#include "pm.h"

#define RTC_RTCCR   0x00
#define RTC_RTCSR   0x04
#define RTC_RTCSAR  0x08
#define RTC_RTCGR   0x0C

#define RTC_HCR     0x20
#define RTC_HWFCR   0x24
#define RTC_HRCR    0x28
#define RTC_HWCR    0x2C
#define RTC_HWRSR   0x30
#define RTC_HSPR    0x34
#define RTC_WENR    0x3C
#define RTC_WKUPPINCR   0x48

#define RTCCR_WRDY  7, 7
#define RTCCR_1HZ   6, 6
#define RTCCR_1HZIE 5, 5
#define RTCCR_AF    4, 4
#define RTCCR_AIE   3, 3
#define RTCCR_AE    2, 2
#define RTCCR_SELEXC    1, 1
#define RTCCR_RTCE  0, 0

#define RTCGR_LOCK  31, 31
#define RTCGR_ADJC  16, 25
#define RTCGR_NC1HZ 0 , 15

#define HCR_PD      0, 0
#define HWFCR       5, 15
#define HRCR        11, 14

#define HWCR_EALM   0, 0

#define HWRSR_APD   8, 8
#define HWRSR_HR    5, 5
#define HWRSR_PPR   4, 4
#define HWRSR_PIN   1, 1
#define HWRSR_ALM   0, 0

#define HSPR_PAT    0, 31

#define WENR_WEN    31, 31
#define WENR_WENPAT 0, 15

#define WKUPPINCR_FAST_BOOT_RAM_SHUTDOWN 23,23
#define WKUPPINCR_FAST_BOOT_RAM_SLEEP    22,22
#define WKUPPINCR_DDR_BUFFER_EN          21,21
#define WKUPPINCR_FAST_BOOT_EN           20,20
#define WKUPPINCR_DRIVING_SELECTOR       18,19
#define WKUPPINCR_OSC_EN    16, 16
#define WKUPPINCR_P_JUD_LEN  4, 7
#define WKUPPINCR_P_RST_EN  0,  3

#define RTC_FREQ_DIVIDER (32768 - 1)

#define WENR_EN     0xa55a
#define HSPR_PAT_VALID  0x52544356

#define OPCR_ERCS_BIT   2
#define CLKGR_RTC_BIT   29

#define RTC_TIMEOUT_US  200

#define RTC_REG_BASE  KSEG1ADDR(RTC_IOBASE)

#define RTC_ADDR(reg) ((volatile unsigned long *)(RTC_REG_BASE + (reg)))

static inline unsigned int jz_rtc_get_bit(unsigned int reg, int start, int end)
{
    unsigned long int now = systick_get_time_us();

    unsigned int data = get_bit_field_v(RTC_ADDR(reg), start, end);

    while (get_bit_field_v(RTC_ADDR(reg), start, end) != data) {
        data = get_bit_field_v(RTC_ADDR(reg), start, end);

        if (systick_get_time_us() - now > RTC_TIMEOUT_US) {
            printf("PM: rtc read reg timeout\n");
            break;
        }

    }

    return get_bit_field_v(RTC_ADDR(reg), start, end);
}

static inline void wait_write_ready(void)
{
    unsigned long int now = systick_get_time_us();

    while (!jz_rtc_get_bit(RTC_RTCCR, RTCCR_WRDY)) {
        if (systick_get_time_us() - now > RTC_TIMEOUT_US) {
            printf("PM: wait rtc write ready timeout\n");
            break;
        }
    }
}

static inline void jz_rtc_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    unsigned long int now;

    wait_write_ready();

    *RTC_ADDR(RTC_WENR) = WENR_EN;

    now = systick_get_time_us();

    while (!jz_rtc_get_bit(RTC_WENR, WENR_WEN)) {
        if (systick_get_time_us() - now > RTC_TIMEOUT_US) {
            printf("PM: rtc enadble write timeout\n");
            break;
        }
    }

    wait_write_ready();

    set_bit_field_v(RTC_ADDR(reg), start, end, val);

    wait_write_ready();
}

static inline void jz_rtc_write_reg(unsigned int reg, unsigned int val)
{
    unsigned long int now;

    wait_write_ready();

    *RTC_ADDR(RTC_WENR) = WENR_EN;

    now = systick_get_time_us();

    while (!jz_rtc_get_bit(RTC_WENR, WENR_WEN)) {
        if (systick_get_time_us() - now > RTC_TIMEOUT_US) {
            printf("PM: rtc enadble write timeout\n");
            break;
        }
    }

    wait_write_ready();

    *RTC_ADDR(reg) = val;

    wait_write_ready();
}

static inline unsigned int jz_rtc_read_reg(unsigned int reg)
{
    unsigned long int now = systick_get_time_us();

    unsigned int data = *RTC_ADDR(reg);

    while (*RTC_ADDR(reg) != data) {
        data = *RTC_ADDR(reg);

        if (systick_get_time_us() - now > RTC_TIMEOUT_US) {
            printf("PM: rtc read reg timeout\n");

            break;
        }
    }

    return data;
}

#define WAKEUP_FILTER_DIV 32
#define WAKEUP_TIME_MS(x) ((x) * 32768 / WAKEUP_FILTER_DIV / 1000)

void soc_pm_power_off(void)
{
    os_enter_critical();

    /* Set minimum wakeup_n pin low-level assertion time for wakeup: 1000ms */
    jz_rtc_set_bit(RTC_HWFCR, HWFCR, WAKEUP_TIME_MS(1000));

    /* Set reset pin low-level assertion time after wakeup: must  > 60ms */
    jz_rtc_set_bit(RTC_HRCR, HRCR, 1);

    /* clear wakeup status register */
    jz_rtc_write_reg(RTC_HWRSR, 0x0);

    jz_rtc_write_reg(RTC_HWCR, 0x0);

    jz_rtc_set_bit(RTC_RTCCR, RTCCR_RTCE, 1);

    /* Put CPU to hibernate mode */
    jz_rtc_write_reg(RTC_HCR, 1);

    mdelay(200);

    while(1)
        printf("PM: Why i am here.RTC_HCR = %x\n", jz_rtc_read_reg(RTC_HCR));

    os_exit_critical();
}

void soc_pm_init(void)
{
    struct gpio_pin reduce_voltage = {CONFIG_X2000_PM_REDUCE_VOLTAGE};

    sleep_param->gpio = reduce_voltage.gpio;
    sleep_param->gpio_level = reduce_voltage.enable_level;

    if (gpio_is_valid(reduce_voltage.gpio)) {
        assert(!gpio_request(reduce_voltage.gpio, "sleep_reduce_voltage"));
        gpio_set_func(reduce_voltage.gpio , reduce_voltage.enable_level ? GPIO_OUTPUT0 : GPIO_OUTPUT1);
    }
}
