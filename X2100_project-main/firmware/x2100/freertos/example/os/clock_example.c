#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>


void test_clock(void)
{
    int ret;
    struct timespec ts1, ts2;

    ts1.tv_sec = 1;
    ts1.tv_nsec = 0;

    ret = clock_settime(CLOCK_REALTIME, &ts1);
    if (ret)
        printf("settime failure : %d\n", errno);

    ret = clock_gettime(CLOCK_REALTIME, &ts1);
    if (ret) {
        printf("gettime failure : %d\n", ret);
        return;
    }

    printf("monotonic time: %lld, %ld\n", ts1.tv_sec, ts1.tv_nsec);

    ret = nanosleep(&ts1, &ts2);
    if (ret) {
        printf("nanosleep is failure : %d\n", errno);
        printf("remaining time: %lld, %ld\n", ts2.tv_sec, ts2.tv_nsec);
        return;
    }
}