#include <rtc_lib.h>

static const unsigned char rtc_days_in_month[] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static __attribute__((unused)) const unsigned short rtc_ydays[2][13] = {
    /* Normal years */
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    /* Leap years */
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

#define LEAPS_THRU_END_OF(y) ((y)/4 - (y)/100 + (y)/400)

static inline int is_leap_year(unsigned int year)
{
    return (!(year % 4) && (year % 100)) || !(year % 400);
}

static int rtc_month_days(unsigned int month, unsigned int year)
{
    return rtc_days_in_month[month] + (is_leap_year(year) && month == 1);
}

void rtc_time_to_tm(unsigned long long time, struct rtc_time *tm)
{
    unsigned int month, year;
    int days;

    days = time / 86400;
    time -= (unsigned int) days * 86400;

    year = 1970 + days / 365;
    days -= (year - 1970) * 365
        + LEAPS_THRU_END_OF(year - 1)
        - LEAPS_THRU_END_OF(1970 - 1);
    if (days < 0) {
        year -= 1;
        days += 365 + is_leap_year(year);
    }
    tm->tm_year = year - 1900;
    tm->tm_yday = days + 1;

    for (month = 0; month < 11; month++) {
        int newdays;

        newdays = days - rtc_month_days(month, year);
        if (newdays < 0)
            break;
        days = newdays;
    }
    tm->tm_mon = month;
    tm->tm_mday = days + 1;

    tm->tm_hour = time / 3600;
    time -= tm->tm_hour * 3600;
    tm->tm_min = time / 60;
    tm->tm_sec = time - tm->tm_min * 60;

    tm->tm_isdst = 0;
}

static unsigned long long mktime(const unsigned int year0, const unsigned int mon0,
       const unsigned int day, const unsigned int hour,
       const unsigned int min, const unsigned int sec)
{
    unsigned int mon = mon0, year = year0;

    /* 1..12 -> 11,12,1..10 */
    if (0 >= (int) (mon -= 2)) {
        mon += 12;  /* Puts Feb last since it has leap day */
        year -= 1;
    }

    return ((((unsigned long long)
            (year/4 - year/100 + year/400 + 367*mon/12 + day) +
            year*365 - 719499
        )*24 + hour /* now have hours */
      )*60 + min /* now have minutes */
    )*60 + sec; /* finally seconds */
}

void rtc_tm_to_time(struct rtc_time *tm, unsigned long long *time)
{
    *time = mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int rtc_valid_tm(struct rtc_time *tm)
{
    if (tm->tm_year < 70
        || ((unsigned)tm->tm_mon) >= 12
        || tm->tm_mday < 1
        || tm->tm_mday > rtc_month_days(tm->tm_mon, tm->tm_year + 1900)
        || ((unsigned)tm->tm_hour) >= 24
        || ((unsigned)tm->tm_min) >= 60
        || ((unsigned)tm->tm_sec) >= 60)
        return 1;
    return 0;
}