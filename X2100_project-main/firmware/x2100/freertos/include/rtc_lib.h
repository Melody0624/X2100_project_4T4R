#ifndef _RTC_LIB_H_
#define _RTC_LIB_H_

struct rtc_time {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

void rtc_time_to_tm(unsigned long long time, struct rtc_time *tm);

void rtc_tm_to_time(struct rtc_time *tm, unsigned long long*time);

int rtc_valid_tm(struct rtc_time *tm);

#endif