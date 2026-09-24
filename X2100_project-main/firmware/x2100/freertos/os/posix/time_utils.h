#ifndef _TIME_UTILS_H_
#define _TIME_UTILS_H_

#include <time.h>
#include <driver/systick.h>
#include <os.h>

static inline uint64_t timespec_to_usecs(const struct timespec *tp)
{
    return (uint64_t) tp->tv_sec * USEC_PER_SEC + tp->tv_nsec / 1000;
}

static inline void usecs_to_timespec(uint64_t us, struct timespec *tp)
{
    tp->tv_sec = us / USEC_PER_SEC;
    tp->tv_nsec = us % USEC_PER_SEC * 1000;
}

static inline int timespec_is_valid(const struct timespec *tp)
{
    if (!tp)
        return 0;
    
    if (0 <= tp->tv_nsec && tp->tv_nsec < NSEC_PER_SEC)
        return 1;
    
    return 0;
}

int timespec_to_timeout_msecs(
    const struct timespec *abstime, unsigned int *msec);

#endif /* _TIME_UTILS_H_ */
