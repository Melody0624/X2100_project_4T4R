#include <os.h>
#include "wait_queue.h"
#include <os/thread_cond.h>
#include <os/critical_thread_cond.h>
#include <kernel_symbol.h>

void critical_thread_cond_init(critical_thread_cond_t *cond)
{
    INIT_LIST_HEAD(&cond->wait_queue);
}
EXPORT_SYMBOL(critical_thread_cond_init);

int critical_thread_cond_wait_timeout(critical_thread_cond_t *cond, uint32_t timeout_ms)
{
    int ret;
    struct wait_queue_waiter waiter;

    assert(!os_in_handler_mode());

    assert(os_is_enter_critical());

    wait_queue_add_waiter(&cond->wait_queue, &waiter);

    os_exit_critical();

    assert(!os_is_enter_critical());

    ret = wait_queue_wait_timeout(&waiter, msecs_to_ticks(timeout_ms));

    os_enter_critical();

    return ret;
}
EXPORT_SYMBOL(critical_thread_cond_wait_timeout);

void critical_thread_cond_wait(critical_thread_cond_t *cond)
{
    critical_thread_cond_wait_timeout(
            cond, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
}
EXPORT_SYMBOL(critical_thread_cond_wait);

void critical_thread_cond_signal(critical_thread_cond_t *cond)
{
    os_enter_critical();
    wait_queue_wake_up(&cond->wait_queue, 1);
    os_exit_critical();
}
EXPORT_SYMBOL(critical_thread_cond_signal);

void critical_thread_cond_broadcast(critical_thread_cond_t *cond)
{
    os_enter_critical();
    wait_queue_wake_up(&cond->wait_queue, MAX_WAITERS);
    os_exit_critical();
}
EXPORT_SYMBOL(critical_thread_cond_broadcast);
