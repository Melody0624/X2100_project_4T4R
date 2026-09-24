#include <os.h>
#include <os/semaphore.h>
#include <assert.h>
#include "wait_queue.h"

int semaphore_init(semaphore_t *sem, unsigned int value)
{
    sem->value = value;
    INIT_LIST_HEAD(&sem->wait_queue);

    return 0;
}

int semaphore_wait_timeout(semaphore_t *sem, unsigned int timeout_ms)
{
    struct wait_queue_waiter waiter;
    int ret = 0;

    os_enter_critical();

    if (wait_queue_has_waiter(&sem->wait_queue) ||
        sem->value == 0) {
        wait_queue_add_waiter(&sem->wait_queue, &waiter);
        os_exit_critical();
        ret = wait_queue_wait_timeout(&waiter, timeout_ms);
        if (ret)
            return ret;
        os_enter_critical();
    }

    sem->value--;

    os_exit_critical();

    return 0;
}

void semaphore_wait(semaphore_t *sem)
{
    semaphore_wait_timeout(sem, OS_TIMEOUT_NOT_LIMIT_MS);
}

int semaphore_try_wait(semaphore_t *sem)
{
    return semaphore_wait_timeout(sem, 0);
}

void semaphore_post(semaphore_t *sem)
{
    os_enter_critical();

    sem->value++;

    /*
     * 溢出不报错，避免post过多的情况
     */
    if (sem->value == 0)
        sem->value--;

    if (wait_queue_has_waiter(&sem->wait_queue))
        wait_queue_wake_up(&sem->wait_queue, 1);

    os_exit_critical();
}

unsigned int semaphore_get_value(semaphore_t *sem)
{
    return sem->value;
}
