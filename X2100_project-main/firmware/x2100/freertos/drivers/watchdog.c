#include <driver/watchdog.h>

void soc_wdt_start(unsigned long ms);
void soc_wdt_stop(void);
void soc_wdt_feed(void);
void soc_reset(void);
unsigned int soc_reset_status_read(void);
void soc_reset_status_wirte(unsigned int value);
unsigned int soc_scratch_pad_read(void);
void soc_scratch_pad_wirte(unsigned int value);

void wdt_start(unsigned long ms)
{
    soc_wdt_start(ms);
}

void wdt_stop(void)
{
    soc_wdt_stop();
}

void wdt_feed(void)
{
    soc_wdt_feed();
}

void reset(void)
{
    soc_reset();
}

unsigned int reset_status_read(void)
{
    return soc_reset_status_read();
}

void reset_status_wirte(unsigned int value)
{
    soc_reset_status_wirte(value);
}

unsigned int scratch_pad_read(void)
{
    return soc_scratch_pad_read();
}

void scratch_pad_wirte(unsigned int value)
{
    soc_scratch_pad_wirte(value);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(wdt_start);
EXPORT_SYMBOL(wdt_stop);
EXPORT_SYMBOL(wdt_feed);
EXPORT_SYMBOL(reset);
EXPORT_SYMBOL(reset_status_read);
EXPORT_SYMBOL(reset_status_wirte);
EXPORT_SYMBOL(scratch_pad_read);
EXPORT_SYMBOL(scratch_pad_wirte);