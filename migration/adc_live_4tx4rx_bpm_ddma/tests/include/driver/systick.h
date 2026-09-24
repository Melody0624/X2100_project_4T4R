#ifndef HOST_SYSTICK_H
#define HOST_SYSTICK_H
#include <stdint.h>
#include <time.h>
static inline uint64_t systick_get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}
#endif
