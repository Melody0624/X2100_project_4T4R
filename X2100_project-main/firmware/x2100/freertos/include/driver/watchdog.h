#ifndef _WATCHDOG_H_
#define _WATCHDOG_H_

#include <soc/watchdog.h>

void wdt_start(unsigned long ms);

void wdt_stop(void);

void wdt_feed(void);

void reset(void);

unsigned int reset_status_read(void);

void reset_status_wirte(unsigned int value);

unsigned int scratch_pad_read(void);

void scratch_pad_wirte(unsigned int value);

#endif