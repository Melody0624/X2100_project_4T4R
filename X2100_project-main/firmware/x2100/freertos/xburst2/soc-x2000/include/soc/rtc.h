#ifndef _SOC_RTC_H_
#define _SOC_RTC_H_

void init_rtc_internal_clk(void);

unsigned long get_rtc_internal_clk_rate(void);

#ifdef CONFIG_RTC

void rtc_set_time(unsigned long long time);

unsigned long long rtc_get_current_time(void);

#endif

#endif /* _SOC_RTC_H_ */