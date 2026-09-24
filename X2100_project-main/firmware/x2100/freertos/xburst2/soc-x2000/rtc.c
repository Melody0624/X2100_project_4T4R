#include <driver/rtc.h>
#include <driver/clk.h>

#include <common.h>
#include <soc/base.h>
#include <driver/irq.h>
#include <assert.h>
#include <soc/cpm.h>
#include <os.h>

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

#define WKUPPINCR_DEFAULT	(0x00af0064)

#define RTC_FREQ_DIVIDER (32768 - 1)

#define WENR_EN     0xa55a
#define HSPR_PAT_VALID  0x52544356

#define OPCR_ERCS_BIT   2
#define CLKGR_RTC_BIT   27

#define RTC_TIMEOUT_US  50000

#define RTC_REG_BASE  KSEG1ADDR(RTC_IOBASE)

#define RTC_ADDR(reg) ((volatile unsigned long *)(RTC_REG_BASE + (reg)))

static inline unsigned int jz_rtc_get_bit(unsigned int reg, int start, int end)
{
    unsigned long int timeout = systick_get_time_us() + RTC_TIMEOUT_US;
    unsigned int data = get_bit_field_v(RTC_ADDR(reg), start, end);

    while (get_bit_field_v(RTC_ADDR(reg), start, end) != data) {
        data = get_bit_field_v(RTC_ADDR(reg), start, end);

        if (systick_get_time_us() >= timeout) {
            printf("RTC: rtc read reg timeout\n");
            break;
        }
    }

    return get_bit_field_v(RTC_ADDR(reg), start, end);
}

static inline void wait_write_ready(void)
{
    unsigned long int timeout = systick_get_time_us() + RTC_TIMEOUT_US;

    while (!jz_rtc_get_bit(RTC_RTCCR, RTCCR_WRDY))
    {
        if (systick_get_time_us() >= timeout) {
            printf("RTC: wait rtc write ready timeout\n");
            break;
        }
    }
}

static inline void jz_rtc_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    unsigned long int timeout;

    wait_write_ready();

    *RTC_ADDR(RTC_WENR) = WENR_EN;

    timeout = systick_get_time_us() + RTC_TIMEOUT_US;

    while (!jz_rtc_get_bit(RTC_WENR, WENR_WEN))
    {
        if (systick_get_time_us() >= timeout) {
            printf("RTC: rtc enadble write timeout\n");
            break;
        }
    }

    wait_write_ready();

    set_bit_field_v(RTC_ADDR(reg), start, end, val);

    wait_write_ready();
}

static inline void jz_rtc_write_reg(unsigned int reg, unsigned int val)
{
    unsigned long int timeout;

    wait_write_ready();

    *RTC_ADDR(RTC_WENR) = WENR_EN;

    timeout = systick_get_time_us() + RTC_TIMEOUT_US;

    while (!jz_rtc_get_bit(RTC_WENR, WENR_WEN))
    {
        if (systick_get_time_us() >= timeout) {
            printf("RTC: rtc enadble write timeout\n");
            break;
        }
    }

    wait_write_ready();

    *RTC_ADDR(reg) = val;

    wait_write_ready();
}

static inline unsigned int jz_rtc_read_reg(unsigned int reg)
{
    unsigned long int timeout = systick_get_time_us() + RTC_TIMEOUT_US;

    unsigned int data = *RTC_ADDR(reg);

    while (*RTC_ADDR(reg) != data) {
        data = *RTC_ADDR(reg);

        if (systick_get_time_us() >= timeout) {
            printf("RTC: rtc read reg timeout\n");
            break;
        }
    }

    return data;
}

void init_rtc_internal_clk(void)
{
#ifdef CONFIG_XBURST2_RTC_32K
    /* 选择 32K RTC作为时钟源 */
    cpm_clear_bit(CLKGR_RTC_BIT, CPM_CLKGR0);

    cpm_set_bit(OPCR_ERCS_BIT, CPM_OPCR);

    jz_rtc_set_bit(RTC_RTCCR, RTCCR_SELEXC, 0);
#endif
}

unsigned long get_rtc_internal_clk_rate(void)
{
#ifndef CONFIG_XBURST2_RTC_32K
    struct clk *wdt_clk;
    unsigned long rate;

    wdt_clk = clk_get("ext1");

    rate = clk_get_rate(wdt_clk) / 512;

    return rate;
#else
    return 32768;
#endif
}

#include <rtc_lib.h>
#include <driver/rtc.h>

static DEFINE_MUTEX(lock);

static unsigned long long tmp_time;
struct rtc_time rtc;

struct rtc_alarm {
    unsigned int alarm_status;
    unsigned int is_alarmed; /* 是否已经触发 */
    unsigned int sec;
    unsigned int record_sec;
    void (*alarm_cb)(void);
};

static struct rtc_alarm rtc_alarm;

static void jz_rtc_set_alarm(void)
{
    unsigned int sec = 0;

    sec = jz_rtc_read_reg(RTC_RTCSR);

    rtc_alarm.record_sec = sec;

    sec += rtc_alarm.sec;

    jz_rtc_write_reg(RTC_RTCSAR, sec);

    jz_rtc_set_bit(RTC_RTCCR, RTCCR_AF, 0);
}

void soc_rtc_set_alarm(unsigned int sec, void (*rtc_cb)(void))
{
    if (sec == 0) {
        rtc_cb();

        return;
    }

    mutex_lock(&lock);

    rtc_alarm.alarm_status = 1;
    rtc_alarm.is_alarmed = 0;
    rtc_alarm.sec = sec;
    rtc_alarm.alarm_cb = rtc_cb;

    jz_rtc_set_alarm();

    mutex_unlock(&lock);
}

void rtc_set_time(unsigned long long time)
{
    unsigned int current_sec;

    mutex_lock(&lock);

    if (time > tmp_time)
        time -= tmp_time;
    else
        panic("RTC: maye be time too early!\n");

    if (!rtc_alarm.is_alarmed && rtc_alarm.alarm_status) {
        current_sec = jz_rtc_read_reg(RTC_RTCSR);

        rtc_alarm.sec -= (current_sec - rtc_alarm.record_sec);

        jz_rtc_write_reg(RTC_RTCSR, time);

        jz_rtc_set_alarm();
    } else {
        jz_rtc_write_reg(RTC_RTCSR, time);
    }

    mutex_unlock(&lock);
}

unsigned long long rtc_get_current_time(void)
{
    unsigned long long tmp;

    mutex_lock(&lock);

    tmp = jz_rtc_read_reg(RTC_RTCSR);

    if (tmp == 0) {
        mutex_unlock(&lock);
        return 0;
    }

    tmp += tmp_time;

    mutex_unlock(&lock);

    return tmp;
}

void rtc_intr_handler(int irq, void *data)
{
    if (jz_rtc_get_bit(RTC_RTCCR, RTCCR_AF)) {
        jz_rtc_set_bit(RTC_RTCCR, RTCCR_AF, 0);

        if (rtc_alarm.alarm_cb)
            rtc_alarm.alarm_cb();

        rtc_alarm.is_alarmed = 1;
        rtc_alarm.alarm_status = 0;
    }
}

static void jz_rtc_init(void)
{
    unsigned int hspr, nc_1hz;

    struct clk *clk = clk_get("gate_rtc");

    clk_enable(clk);

    hspr = jz_rtc_get_bit(RTC_HSPR, HSPR_PAT);
    nc_1hz = jz_rtc_get_bit(RTC_RTCGR, RTCGR_NC1HZ);

    if ((hspr != HSPR_PAT_VALID) || (nc_1hz != RTC_FREQ_DIVIDER)) {
        jz_rtc_set_bit(RTC_RTCGR, RTCGR_NC1HZ, RTC_FREQ_DIVIDER);

        jz_rtc_write_reg(RTC_RTCSR, 0);

        jz_rtc_set_bit(RTC_RTCCR, RTCCR_RTCE, 1);

        jz_rtc_set_bit(RTC_HSPR, HSPR_PAT, HSPR_PAT_VALID);
    }

    tmp_time = (unsigned long long)get_compile_year_ms() / 1000;

    request_irq(IRQ_RTC, 0, rtc_intr_handler, "rtc" , NULL);

    jz_rtc_write_reg(RTC_HWRSR, 0);

    jz_rtc_set_bit(RTC_HWCR, HWCR_EALM, 1);
    jz_rtc_write_reg(RTC_WKUPPINCR, WKUPPINCR_DEFAULT);

    jz_rtc_set_bit(RTC_RTCCR, RTCCR_AF, 0);
    jz_rtc_set_bit(RTC_RTCCR, RTCCR_AIE, 1);
    jz_rtc_set_bit(RTC_RTCCR, RTCCR_AE, 1);
}

void soc_rtc_init(void)
{
    jz_rtc_init();
}
