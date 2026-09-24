#include <driver/systick.h>
#include <delay.h>
#include <module.h>
#include <limits.h>

extern void ndelay(unsigned int nsecs);

void udelay(unsigned int us)
{
    if (us < 1000000) {
        ndelay(us * 1000);
    } else {
        uint64_t now = systick_get_time_us();
        while (systick_get_time_us() - now <= us);
    }
}
EXPORT_SYMBOL(udelay);

void mdelay(unsigned int ms)
{
    uint64_t now = systick_get_time_us();
    uint64_t us = (uint64_t)ms * 1000;

    while (systick_get_time_us() - now <= us);
}
EXPORT_SYMBOL(mdelay);
