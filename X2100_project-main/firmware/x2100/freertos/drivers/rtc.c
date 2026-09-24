#include <driver/rtc.h>

int soc_rtc_set_alarm(unsigned int sec, void (*rtc_cb)(void));

void soc_rtc_init(void);

unsigned long long get_compile_year_ms(void)
{
    int year = get_compile_year() - 1;
    int month = 12;
    int day = 31;
    int hour = 23;
    int min = 59;
    int sec = 59;
    unsigned long long total_ms = 0;
    unsigned long long day_ms = 86400000ULL;

    for(int i = 1970; i < year; i++) {
        if (is_leap_year(i))
            total_ms += 366 * day_ms;
        else
            total_ms += 365 * day_ms;
    }

    int days_in_month[12] = {31, is_leap_year(year) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    for(int i = 0; i < month - 1; i++) {
        total_ms += days_in_month[i] * day_ms;
    }

    total_ms += (day - 1) * day_ms;
    total_ms += hour * 3600000ULL;
    total_ms += min * 60000ULL;
    total_ms += sec * 1000ULL;

    return total_ms;
}

void rtc_get_current_tm(struct rtc_time *tm)
{
    unsigned long long tmp = rtc_get_current_time();

    if (tmp == 0) {

        struct rtc_time default_tm = {0};

        default_tm.tm_year = get_compile_year();
        default_tm.tm_mon = 1;
        default_tm.tm_mday = 1;

        rtc_set_tm(&default_tm);

        tmp = rtc_get_current_time();

    }

    rtc_time_to_tm(tmp, tm);
}

void rtc_set_tm(struct rtc_time *tm)
{
    struct rtc_time rtc;
    unsigned long long time = 0;

    rtc.tm_year = tm->tm_year - 1900;
    rtc.tm_mon = tm->tm_mon - 1;
    rtc.tm_mday = tm->tm_mday;
    rtc.tm_hour = tm->tm_hour;
    rtc.tm_min  = tm->tm_min;
    rtc.tm_sec  = tm->tm_sec;
    rtc.tm_isdst= tm->tm_isdst;
    rtc.tm_wday = tm->tm_wday;
    rtc.tm_yday = tm->tm_yday;

    if (rtc_valid_tm(&rtc) == 1)
        printf("RTC: valid rtc time\n");

    rtc_tm_to_time(&rtc, &time);

    rtc_set_time(time);
}

void rtc_show_current_tm(struct rtc_time *tm)
{
    rtc_get_current_tm(tm);

    printf("[current time] %d/%d/%d, %d:%d:%d\n",
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

void rtc_set_alarm(unsigned int sec, void (*rtc_cb)(void))
{
   soc_rtc_set_alarm(sec, rtc_cb);
}

void rtc_init(void)
{
    soc_rtc_init();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(rtc_get_current_tm);
EXPORT_SYMBOL(rtc_show_current_tm);
EXPORT_SYMBOL(rtc_set_alarm);
EXPORT_SYMBOL(rtc_set_tm);
