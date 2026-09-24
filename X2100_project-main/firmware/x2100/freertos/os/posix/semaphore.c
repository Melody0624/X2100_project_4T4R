#include <os.h>
#include <semaphore.h>
#include <errno.h>
#include <os/semaphore.h>
#include <stdlib.h>
#include "time_utils.h"

int sem_destroy(sem_t *sem)
{
    (void) sem;
    return 0;
}

int sem_getvalue(sem_t *sem, int *sval)
{
    semaphore_t *ap = (void *)*sem;

    *sval = semaphore_get_value(ap);

    return 0;
}

int sem_init(sem_t *sem, int pshared, unsigned value)
{
    ( void ) pshared;

    semaphore_t *ap = malloc(sizeof(*ap));

    if(value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }

    semaphore_init(ap, value);

    *sem = (unsigned int)ap;

    return 0;
}

int sem_post(sem_t *sem)
{
    int ret = 0;

    semaphore_t *ap = (void *)*sem;

    os_enter_critical();

    semaphore_post(ap);
    if (ap->value > SEM_VALUE_MAX)
        ret = EOVERFLOW;

    os_exit_critical();

    return ret;
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    unsigned int msec;

    if (timespec_to_timeout_msecs(abstime, &msec)) {
        errno = ETIMEDOUT;
        return -1;
    }

    semaphore_t *ap = (void *)*sem;

    if (semaphore_wait_timeout(ap, msec)) {
        errno = ETIMEDOUT;
        return -1;
    }

    return 0;
}

int sem_trywait(sem_t *sem)
{
    /* POSIX specifies that this function should set errno to EAGAIN and not
     * ETIMEDOUT. */
    semaphore_t *ap = (void *)*sem;

    if (semaphore_try_wait(ap)) {
        errno = EAGAIN;
        return -1;
    }

    return 0;
}

int sem_wait(sem_t *sem)
{
    semaphore_t *ap = (void *)*sem;

    semaphore_wait(ap);

    return 0;
}
