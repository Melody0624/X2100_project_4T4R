#include "os.h"
#include "wait_queue.h"
#include "FreeRTOS.h"
#include "task.h"
#include <common.h>
#include <os/thread_cond.h>
#include <kernel_symbol.h>

void mutex_init(struct mutex * mutex)
{
    mutex->count = 0;
    mutex->re_count = 0;
    mutex->recursive = 0;
    mutex->thread = NULL;
    INIT_LIST_HEAD(&mutex->wait_queue);
}
EXPORT_SYMBOL(mutex_init);

void mutex_init_recursive(struct mutex * mutex)
{
    mutex_init(mutex);
    mutex->recursive = 1;
}
EXPORT_SYMBOL(mutex_init_recursive);

int mutex_lock_timeout(struct mutex * mutex, unsigned int timeout_ms)
{
    struct wait_queue_waiter waiter;

    assert(!os_in_handler_mode());

    os_enter_critical();

    mutex->count++;
    assert(mutex->count >= 1);

    if (mutex->count == 1) {
        if (mutex->recursive) {
            mutex->re_count = 1;
            mutex->thread = thread_get_current();
        }
        os_exit_critical();
        return 0;
    }

    if (mutex->recursive) {
        if (mutex->thread == thread_get_current()) {
            mutex->re_count++;
            os_exit_critical();
            return 0;
        }
    }

    wait_queue_add_waiter(&mutex->wait_queue, &waiter);

    os_exit_critical();

    int ret = wait_queue_wait_timeout(&waiter, timeout_ms);
    if (ret) {
        os_enter_critical();
        mutex->count--;
        os_exit_critical();
        return ret;
    }

    if (mutex->recursive) {
        mutex->thread = thread_get_current();
        mutex->re_count = 1;
    }

    return 0;
}
EXPORT_SYMBOL(mutex_lock_timeout);

void mutex_lock(struct mutex * mutex)
{
    mutex_lock_timeout(mutex, portMAX_DELAY);
}
EXPORT_SYMBOL(mutex_lock);

void mutex_unlock(struct mutex * mutex)
{
    assert(!os_in_handler_mode());

    os_enter_critical();

    mutex->count--;
    assert(mutex->count >= 0);

    if (mutex->recursive) {
        if (mutex->thread != thread_get_current())
            panic("can't mutex_unlock() because this thread not hold it\n");

        mutex->re_count--;
        assert(mutex->re_count >= 0);

        if (mutex->re_count) {
            assert(mutex->count);
            os_exit_critical();
            return ;
        }

        mutex->thread = NULL;
    }

    if (!mutex->count) {
        os_exit_critical();
        return ;
    }

    wait_queue_wake_up(&mutex->wait_queue, 1);

    os_exit_critical();
}
EXPORT_SYMBOL(mutex_unlock);

int mutex_try_lock(struct mutex * mutex)
{
    int ret = 0;

    assert(!os_in_handler_mode());

    os_enter_critical();

    if ((mutex->count == 0) ||
        (mutex->recursive && mutex->thread == thread_get_current())) {
        ret = 1;
        mutex_lock(mutex);
    }

    os_exit_critical();

    return ret;
}
EXPORT_SYMBOL(mutex_try_lock);

int mutex_is_locked(struct mutex * mutex)
{
    return mutex->count;
}
EXPORT_SYMBOL(mutex_is_locked);

void thread_cond_init(thread_cond_t *cond)
{
    cond->mutex = NULL;
    INIT_LIST_HEAD(&cond->wait_queue);
}
EXPORT_SYMBOL(thread_cond_init);

int thread_cond_wait_timeout(thread_cond_t *cond, struct mutex * mutex, uint32_t timeout_ms)
{
    BaseType_t ret;
    struct wait_queue_waiter waiter;

    assert(cond);
    assert(mutex);
    assert(cond->mutex == NULL || cond->mutex == mutex);
    assert(!os_in_handler_mode());
    assert(!mutex->recursive);

    os_enter_critical();

    cond->mutex = mutex;
    mutex_unlock(mutex);
    wait_queue_add_waiter(&cond->wait_queue, &waiter);

    os_exit_critical();

    ret = wait_queue_wait_timeout(&waiter, msecs_to_ticks(timeout_ms));

    if (ret < 0) {
        mutex_lock(mutex);
    }
    else {
        /* thread_cond_del_notify_nr was called already,
         * and mutex was locked in wait_queue_wake_up_mutex
         */
    }

    return ret;
}
EXPORT_SYMBOL(thread_cond_wait_timeout);

void thread_cond_wait(thread_cond_t *cond, struct mutex * mutex)
{
    thread_cond_wait_timeout(cond, mutex, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
}
EXPORT_SYMBOL(thread_cond_wait);

void thread_cond_signal(thread_cond_t *cond)
{
    assert(cond);

    os_enter_critical();
    wait_queue_wake_up_mutex(&cond->wait_queue, cond->mutex, 1);
    os_exit_critical();
}
EXPORT_SYMBOL(thread_cond_signal);

void thread_cond_broadcast(thread_cond_t *cond)
{
    assert(cond);

    os_enter_critical();
    wait_queue_wake_up_mutex(&cond->wait_queue, cond->mutex, MAX_WAITERS);
    os_exit_critical();
}
EXPORT_SYMBOL(thread_cond_broadcast);