#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <os.h>
#include <assert.h>

struct m_thread_barrier {
    unsigned int count;
    unsigned int index;
    critical_thread_cond_t cond;
};

int pthread_barrier_init(pthread_barrier_t *barrier,
    const pthread_barrierattr_t *attr, unsigned int count)
{
    (void) attr;

    if (count == 0)
        return EINVAL;

    struct m_thread_barrier *barrier_data = malloc(sizeof(*barrier_data));

    barrier_data->index = 0;
    barrier_data->count = count;
    critical_thread_cond_init(&barrier_data->cond);
    
    *barrier = (pthread_barrier_t) barrier_data;

    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
    struct m_thread_barrier *barrier_data = (void *)*barrier;

    assert(!barrier_data->index);

    free(barrier_data);

    *barrier = 0;

    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
    int ret = 0;
    struct m_thread_barrier *barrier_data = (void *)*barrier;

    os_enter_critical();

    unsigned int index = barrier_data->index++;

    if (barrier_data->index == barrier_data->count) {
        barrier_data->index = 0;
        critical_thread_cond_broadcast(&barrier_data->cond);
    } else {
        critical_thread_cond_wait(&barrier_data->cond);
    }

    if (index == 0)
        ret = PTHREAD_BARRIER_SERIAL_THREAD;

    os_exit_critical();

    return ret;
}
