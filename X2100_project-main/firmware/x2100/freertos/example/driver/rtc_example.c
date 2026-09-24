#include <driver/rtc.h>

/* 表示时间为 2019年5月20日 23点59分58秒 */
struct rtc_time tm = {
    .tm_sec = 58,
    .tm_min = 59,
    .tm_hour = 23,
    .tm_mday = 20,
    .tm_mon = 5,
    .tm_year = 2019,
};

void rtc_test(void)
{
    struct rtc_time default_tm;

    /* 获取当前时间 */
    rtc_get_current_tm(&default_tm);

    /* 打印当前时间 */
    rtc_show_current_tm(&default_tm);

    /* 获取编译日期 */
    tm.tm_year = get_compile_year();
    tm.tm_mon = get_compile_month();
    tm.tm_mday = get_compile_day();

    /* 设置当前时间为:编译当天的23点59分58秒 */
    rtc_set_tm(&tm);

    /* 打印当前时间 */
    rtc_show_current_tm(&tm);
}