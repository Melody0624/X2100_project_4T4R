/* Define POSIX feature macros to enable timer/clock function declarations */
#ifndef _POSIX_TIMERS
#define _POSIX_TIMERS
#endif

#include <os.h>
#include <driver/hrtimer.h>

#include <signal.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>

#include <assert.h>
#include <pthread.h>

#include "time_utils.h"

struct m_timer {
    struct hrtimer hrtimer;
    thread_waiter_t waiter;
    struct sigevent event;
    uint64_t period;
    uint64_t expire_time;
    clock_t clockid;
    int is_running;
    int timer_count;
    pthread_t thread;
};

static void timer_hrtimer_func(struct hrtimer *hrtimer)
{
    struct m_timer *timer = container_of(hrtimer, struct m_timer, hrtimer);

    timer->timer_count++;
    thread_waiter_wakeup(&timer->waiter);

    timer->expire_time = 0;
    if (timer->period) {
        timer->expire_time = systick_get_time_us() + timer->period;
        hrtimer_restart_at_expires(&timer->hrtimer, timer->expire_time);
    }
}

static void *timer_user_thread_func(void *data)
{
    struct m_timer *timer = data;

    timer->event.sigev_notify_function(timer->event.sigev_value);

    return NULL;
}


static void *timer_thread_func(void *data)
{
    struct m_timer *timer = data;

    while (timer->is_running) {
        thread_waiter_wait(&timer->waiter);
        if (!timer->is_running) {
            timer->is_running = -1;
            break;
        }

        os_enter_critical();
        timer->timer_count--;
        os_exit_critical();
        if (timer->event.sigev_notify_attributes) {
            pthread_t thread;
            pthread_create(&thread, timer->event.sigev_notify_attributes, timer_user_thread_func, timer);
        } else {
            timer->event.sigev_notify_function(timer->event.sigev_value);
        }
    }

    free(timer);

    return NULL;
}

int timer_settime(timer_t timerid, int flags,
    const struct itimerspec *value, struct itimerspec *ovalue)
{
    uint64_t expire_time = timespec_to_usecs(&value->it_value);
    uint64_t period_time = timespec_to_usecs(&value->it_interval);

    if (expire_time != 0) {
        if (!timespec_is_valid(&value->it_value) ||
            !timespec_is_valid(&value->it_interval)) {
            errno = EINVAL;
            return -1;
        }
    }

    struct m_timer *timer = (void *) timerid;

    hrtimer_cancel(&timer->hrtimer);

    if (ovalue)
        timer_gettime(timerid, ovalue);

    timer->period = 0;
    timer->timer_count = 0;
    timer->expire_time = 0;

    if (!expire_time)
        return 0;

    timer->period = period_time;

    os_enter_critical();
    struct timespec nowtime;
    clock_gettime(CLOCK_REALTIME, &nowtime);
    uint64_t start_time = timespec_to_usecs(&nowtime);

    if (!(flags & TIMER_ABSTIME))
        expire_time += start_time;

    timer->expire_time = expire_time;
    hrtimer_start_at_expires(&timer->hrtimer, expire_time);
    os_exit_critical();

    return 0;
}

int timer_create(clockid_t clockid, struct sigevent *evp, timer_t *timerid)
{
    (void) clockid;

    /*
     * 1 当 evp == NULL 时,表示用 SIGEV_SIGNAL
     * 2 SIGEV_SIGNAL 不支持
     */
    if(evp == NULL || evp->sigev_notify == SIGEV_SIGNAL) {
        errno = ENOTSUP;
        return -1;
    }

    struct m_timer *timer = malloc(sizeof(*timer));
    if (timer == NULL) {
        errno = ENOMEM;
        return -1;
    }


    timer->event = *evp;
    timer->period = 0;
    timer->is_running = 1;
    timer->clockid = clockid;
    thread_waiter_init(&timer->waiter);
    hrtimer_init(&timer->hrtimer, timer_hrtimer_func);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&timer->thread, &attr, timer_thread_func, timer);

    *timerid = (timer_t) timer;

    return 0;
}

int timer_delete(timer_t timerid)
{
    struct m_timer *timer = (void *) timerid;

    timer->is_running = 0;
    hrtimer_cancel(&timer->hrtimer);
    thread_waiter_wakeup(&timer->waiter);

    return 0;
}

int timer_getoverrun(timer_t timerid)
{
    struct m_timer *timer = (void *) timerid;

    return timer->timer_count;
}

int timer_gettime(timer_t timerid, struct itimerspec *value)
{
    struct m_timer *timer = (void *) timerid;

    uint64_t delta = 0;

    os_enter_critical();
    if (timer->expire_time) {
        uint64_t now = systick_get_time_us();
        if (now > timer->expire_time)
            delta = now - timer->expire_time;
    }

    usecs_to_timespec(delta, &value->it_value);
    usecs_to_timespec(timer->period, &value->it_interval);
    os_exit_critical();

    return 0;
}
