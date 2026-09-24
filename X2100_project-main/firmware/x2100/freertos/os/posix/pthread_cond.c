#include <errno.h>
#include <pthread.h>
#include <os.h>
#include <list.h>
#include <stdlib.h>

#include "time_utils.h"

struct m_waiter {
    struct list_head link;
    struct thread_waiter waiter;
};

struct m_thread_cond2 {
    struct list_head list;
};

struct m_thread_cond {
    critical_thread_cond_t cond;
};

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    (void) attr;

    struct m_thread_cond *cond_data = malloc(sizeof(*cond_data));

    critical_thread_cond_init(&cond_data->cond);

    *cond = (pthread_cond_t) cond_data;

    return 0;
}

static void check_init_cond(pthread_cond_t *cond)
{
    os_enter_critical();

    if (*cond == PTHREAD_COND_INITIALIZER)
        pthread_cond_init(cond, NULL);

    os_exit_critical();
}

int pthread_cond_timedwait(
    pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
    unsigned int msec;
    int ret = 0;

    if (timespec_to_timeout_msecs(abstime, &msec))
        return ETIMEDOUT;

    check_init_cond(cond);

    struct m_thread_cond *cond_data = (void *)*cond;

    os_enter_critical();

    ret = pthread_mutex_unlock(mutex);
    if (ret)
        goto unlock;

    ret = critical_thread_cond_wait_timeout(&cond_data->cond, msec);
    if (ret)
        ret = ETIMEDOUT;

unlock:
    os_exit_critical();

    if (!ret)
        return pthread_mutex_lock(mutex);

    return ret;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return pthread_cond_timedwait(cond, mutex, NULL);
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
    check_init_cond(cond);

    struct m_thread_cond *cond_data = (void *)*cond;

    critical_thread_cond_broadcast(&cond_data->cond);

    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
    check_init_cond(cond);

    struct m_thread_cond *cond_data = (void *)*cond;

    critical_thread_cond_signal(&cond_data->cond);

    return 0;
}

int pthread_cond_destroy( pthread_cond_t * cond )
{
    struct m_thread_cond *cond_data = (void *)*cond;

    check_init_cond(cond);

    os_enter_critical();

    critical_thread_cond_broadcast(&cond_data->cond);

    free(cond_data);
    *cond = 0;

    os_exit_critical();

    return 0;
}
