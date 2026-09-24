#include <common.h>
#include <soc/base.h>
#include <driver/watchdog.h>
#include <driver/clk.h>
#include <os.h>
#include <soc/cpm.h>
#include <soc/rtc.h>

#define WDT_IDLE     0
#define WDT_BUSY     1

#define WDT_FULL     0x0
#define WDT_ENABLE   0x4
#define WDT_COUNT    0x8
#define WDT_CONTROL  0xC
#define TCU_TSSTR    0x2C
#define TCU_TSCLR    0x3C

#define TCEN         0

#define WDT_MAX_COUNT       (0xFFFF)

#define RTC_EN              1
#define PRESCALE            3, 5
#define CLRZ                10
#define WDT_CLK_DIV_1       0
#define WDT_CLK_DIV_4       1
#define WDT_CLK_DIV_16      2
#define WDT_CLK_DIV_64      3
#define WDT_CLK_DIV_256     4
#define WDT_CLK_DIV_1024    5

#define WDTSS        16
#define WDTSC        16

#define WDT_ADDR(reg) (io_addr(WDT_IOBASE + reg))

static inline void wdt_write_reg(unsigned int reg, unsigned int value)
{
    *WDT_ADDR(reg) = value;
}

static inline unsigned int wdt_read_reg(unsigned int reg)
{
    return *WDT_ADDR(reg);
}

static inline void wdt_set_bit(unsigned int reg, int bit, unsigned int val)
{
    set_bit_field_v(WDT_ADDR(reg), bit, bit, val);
}

static inline unsigned int wdt_get_bit(unsigned int reg, int bit)
{
    return get_bit_field_v(WDT_ADDR(reg), bit, bit);
}

static inline void wdt_set_bits(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(WDT_ADDR(reg), start, end, val);
}

static inline unsigned int wdt_get_bits(unsigned int reg, int start, int end)
{
    return get_bit_field_v(WDT_ADDR(reg), start, end);
}

static unsigned int status = 0;

static void jz_wdt_set_timeout(unsigned long ms)
{
    unsigned int us;
    unsigned long count = ms;
    unsigned int clock_div = 0;

    unsigned long rate = get_rtc_internal_clk_rate();

    us = 1000000 / rate;

    count = ms * 1000 / us;

    while (count > WDT_MAX_COUNT) {
        if (clock_div == WDT_CLK_DIV_1024)
            panic("error: count more than the WATCHDOG_MAX_COUNT!\n");

        count /= 4;
        clock_div += 1;
    }

    wdt_set_bit(WDT_ENABLE, TCEN, 0);

    wdt_set_bit(WDT_CONTROL, CLRZ, 1);

    wdt_set_bits(WDT_CONTROL, PRESCALE, clock_div);

    wdt_set_bit(WDT_CONTROL, RTC_EN, 1);

    wdt_write_reg(WDT_FULL, count);
}

void soc_wdt_start(unsigned long ms)
{
    os_enter_critical();

    if (status != WDT_IDLE)
        panic("WDT: watchdog is running ! If you want to operate it, you should use 'wdt_stop()' first.\n");

    status = WDT_BUSY;

    wdt_set_bit(TCU_TSCLR, WDTSC, 1); //使能看门狗计数器
    jz_wdt_set_timeout(ms);

    wdt_set_bit(WDT_ENABLE, TCEN, 1);

    os_exit_critical();
}

void soc_wdt_stop(void)
{
    os_enter_critical();

    if (status != WDT_IDLE) {
        wdt_set_bit(WDT_ENABLE, TCEN, 0);
        wdt_set_bit(TCU_TSSTR, WDTSS, 1); //失能看门狗计数器
        status = WDT_IDLE;
    }

    os_exit_critical();
}

void soc_wdt_feed(void)
{
    os_enter_critical();

    if (status != WDT_IDLE)
        wdt_set_bit(WDT_CONTROL, CLRZ, 1);

    os_exit_critical();
}

void soc_reset(void)
{
    os_enter_critical();

    soc_wdt_stop();

    soc_wdt_start(0);

    while (1) {
        mdelay(10);
        printf("WDT: reset faild\n");
    }

    os_exit_critical();
}


#define RSR_ADDR        (io_addr(CPM_IOBASE + 0x0008))
#define CPSPR_ADDR      (io_addr(CPM_IOBASE + 0x0034))
#define CPSPPR_ADDR     (io_addr(CPM_IOBASE + 0x0038))
#define CPSPPR_WIRTE    0x5a5a
#define CPSPPR_CLEAR    0xa5a5

unsigned int soc_reset_status_read(void)
{
    return *RSR_ADDR;
}

void soc_reset_status_wirte(unsigned int value)
{
    *RSR_ADDR = value;
}

unsigned int soc_scratch_pad_read(void)
{
    return *CPSPR_ADDR;
}

void soc_scratch_pad_wirte(unsigned int value)
{
    os_enter_critical();

    *CPSPPR_ADDR = CPSPPR_WIRTE;
    *CPSPR_ADDR = value;
    *CPSPPR_ADDR = CPSPPR_CLEAR;

    os_exit_critical();
}
