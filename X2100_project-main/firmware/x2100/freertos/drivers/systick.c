#include <driver/systick.h>

/*
 * soc 需要实现
 */
uint64_t soc_systick_get_time_us(void);
uint64_t soc_systick_get_time_ns(void);
void soc_systick_set_next_time(uint64_t expires);
void soc_systick_init(void);
void soc_systick_set_event_callback(systick_event_handler_t callback);

static uint64_t offset_us = 0;
static uint64_t offset_ns = 0;

void systick_add_time_us(uint64_t usecs)
{
    offset_us = offset_us + usecs;
    offset_ns = offset_us * 1000;
}

uint64_t systick_get_time_us(void)
{
    return offset_us + soc_systick_get_time_us();
}

uint64_t systick_get_time_ns(void)
{
    return offset_ns + soc_systick_get_time_ns();
}

uint64_t systick_get_time_ms(void)
{
    return systick_get_time_us() / 1000;
}

void systick_set_next_time(uint64_t expires)
{
    soc_systick_set_next_time(expires);
}

void systick_init(void)
{
    soc_systick_init();
}

void systick_set_event_callback(systick_event_handler_t callback)
{
    soc_systick_set_event_callback(callback);
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(systick_get_time_us);
