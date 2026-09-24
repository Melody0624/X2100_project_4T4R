#ifndef _RTC_H_
#define _RTC_H_

#include <soc/rtc.h>

#include <rtc_lib.h>

#include <common.h>

unsigned long long get_compile_year_ms(void);

void rtc_get_current_tm(struct rtc_time *tm);

void rtc_set_tm(struct rtc_time *tm);

void rtc_show_current_tm(struct rtc_time *tm);

void rtc_set_alarm(unsigned int sec, void (*rtc_cb)(void));

void rtc_init(void);

static inline int is_leap_year(unsigned int year)
{
    return (!(year % 4) && (year % 100)) || !(year % 400);
}

static inline int get_compile_year(void)
{
    char *tmp = __DATE__;

    while (*tmp++ != ' ');
    while (*tmp++ == ' ');
    while (*tmp++ != ' ');

    return atoi(tmp);
}

static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static inline int get_compile_month(void)
{
    const char *tmp = __DATE__;

    for(int i = 0; i < ARRAY_SIZE(months); i++) {
        if (strncmp(tmp, months[i], strlen(months[i])) == 0) {
            return i + 1;
        }
    }

    return -1;
}

static inline int get_compile_day(void)
{
    const char *tmp = __DATE__;

    while (*tmp != ' ')    tmp++;
    while (*tmp == ' ')    tmp++;

    return atoi(tmp);
}
#endif