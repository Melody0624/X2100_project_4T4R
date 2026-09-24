#include <pthread.h>
#include <stdlib.h>
#include <os.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include "time_utils.h"

static void check_init_mutex(pthread_mutex_t *mutex)
{
    os_enter_critical();

    assert(mutex);
    assert(*mutex);

    if (*mutex == PTHREAD_MUTEX_INITIALIZER) {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutex_init(mutex, &attr);
    }

    os_exit_critical();
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (!mutex)
        return ENOMEM;

    struct mutex *lock = malloc(sizeof(*lock));

    if (!attr) {
        pthread_mutexattr_t default_attr;
        pthread_mutexattr_init(&default_attr);
        attr = &default_attr;
    }

    if (attr->type & PTHREAD_MUTEX_RECURSIVE)
        mutex_init_recursive(lock);
    else
        mutex_init(lock);

    *mutex = (unsigned int)lock;

    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    os_enter_critical();

    check_init_mutex(mutex);

    struct mutex *lock = (void *)*mutex;

    assert(!mutex_is_locked(lock));

    free(lock);

    *mutex = 0;

    os_exit_critical();

    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    check_init_mutex(mutex);

    struct mutex *lock = (void *)*mutex;

    mutex_lock(lock);

    return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abstime)
{
    unsigned int msec;

    if (timespec_to_timeout_msecs(abstime, &msec))
        return ETIMEDOUT;

    check_init_mutex(mutex);

    struct mutex *lock = (void *)*mutex;

    if (mutex_lock_timeout(lock, msec))
        return ETIMEDOUT;

    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    check_init_mutex(mutex);

    struct mutex *lock = (void *)*mutex;

    if (mutex_try_lock(lock))
        return 0;

    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    check_init_mutex(mutex);

    struct mutex *lock = (void *)*mutex;

    mutex_unlock(lock);

    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    attr->type = PTHREAD_MUTEX_DEFAULT;
    return 0;
}

int pthread_mutexattr_destroy( pthread_mutexattr_t * attr )
{
    ( void ) attr;

    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
    *type = attr->type;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    switch(type) {
    case PTHREAD_MUTEX_NORMAL:
    case PTHREAD_MUTEX_RECURSIVE:
    case PTHREAD_MUTEX_ERRORCHECK:
    case PTHREAD_MUTEX_ERRORCHECK | PTHREAD_MUTEX_RECURSIVE:
        attr->type = type;
        break;

    default:
        return EINVAL;
    }

    return 0;
}
