/* Define POSIX feature macros to enable clock_gettime declaration */
#ifndef _POSIX_TIMERS
#define _POSIX_TIMERS
#endif

#include "time_utils.h"

int timespec_to_timeout_msecs(
    const struct timespec *abstime, unsigned int *msec)
{
    struct timespec nowtime;

    if (!abstime) {
        *msec = OS_TIMEOUT_NOT_LIMIT_MS;
        return 0;
    }

    clock_gettime(CLOCK_REALTIME, &nowtime);

    uint64_t now = timespec_to_usecs(&nowtime);
    uint64_t abs = timespec_to_usecs(abstime);

    if (abs < now)
        return -1;

    *msec = (abs - now + USEC_PER_MSEC - 1) / USEC_PER_MSEC;

    return 0;
}