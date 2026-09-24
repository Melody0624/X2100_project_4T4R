#include <os.h>
#include <os/thread_cond.h>
#include <os/thread_waiter.h>
#include <kernel_symbol.h>
#include <assert.h>

void thread_waiter_init(thread_waiter_t *waiter)
{
    waiter->thread = NULL;
    waiter->is_pending = 0;
}
EXPORT_SYMBOL(thread_waiter_init);

int thread_waiter_wait_timeout(thread_waiter_t *waiter, uint32_t timeout_ms)
{
    int ret = 0;

    os_enter_critical();

    assert(!waiter->thread);
    waiter->thread = thread_get_current();

    os_exit_critical();

    while (!waiter->is_pending) {
        ret = thread_wait_timeout(timeout_ms);
        if (ret < 0)
            break;
    }

    os_enter_critical();

    waiter->thread = NULL;
    if (!ret)
        waiter->is_pending = 0;

    os_exit_critical();

    return ret;
}
EXPORT_SYMBOL(thread_waiter_wait_timeout);

void thread_waiter_wait(thread_waiter_t *waiter)
{
    thread_waiter_wait_timeout(waiter, THREAD_COND_TIMEOUT_NO_LIMIT_MS);
}
EXPORT_SYMBOL(thread_waiter_wait);

void thread_waiter_wakeup(thread_waiter_t *waiter)
{
    os_enter_critical();

    if (!waiter->is_pending) {
        waiter->is_pending = 1;
        if (waiter->thread)
            thread_wakeup((thread_ptr_t) waiter->thread);
    }

    os_exit_critical();
}
EXPORT_SYMBOL(thread_waiter_wakeup);
