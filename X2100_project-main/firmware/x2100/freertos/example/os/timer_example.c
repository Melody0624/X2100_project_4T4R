#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

static int count = 0;

void timer_thread(union sigval v)
{
    printf("timer_thread function! %d\n", v.sival_int);
    count++;
}

void test_timer(void)
{
    struct sigevent evp;
    struct itimerspec ts1, ts2;
    timer_t timer;
    int ret;

    evp.sigev_value.sival_int = 111;
    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = timer_thread;

    ret = timer_create(CLOCK_REALTIME, &evp, &timer);
    if (ret) {
        printf("timer_create is failure : %d\n", errno);
    }

    ret = timer_gettime(timer, &ts1);
    if (ret) {
        printf("timer_gettime is failure : %d\n", ret);
        return;
    }

    ts1.it_value.tv_sec = 1;
    ts1.it_value.tv_nsec = 0;
    ts1.it_interval.tv_sec = 1;
    ts1.it_interval.tv_nsec = 0;

    ret = timer_settime(timer, 0, &ts1, NULL);
    if(ret) {
        printf("timer_settime is failure : %d\n", errno);
        return;
    }

    ret = timer_getoverrun(timer);
    printf("the overrun is : %d\n", ret);


    while (count < 5) {
        usleep(1000);
    }
    printf("we will close the timer\n");

    ts2.it_value.tv_sec = 0;
    ts2.it_value.tv_nsec = 0;
    ts2.it_interval.tv_sec = 0;
    ts2.it_interval.tv_nsec = 0;

    ret = timer_settime(timer, 0, &ts2, NULL);
    if(ret) {
        printf("timer_settime is failure : %d\n", errno);
        return;
    }

    ret = timer_delete(timer);
    if (ret)
        printf("timer_delete is failure : %d\n", ret);

    while(1);
}