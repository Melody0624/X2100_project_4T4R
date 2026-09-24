#include <os.h>
#include <driver/systick.h>

uint64_t timeout_to_systick_us(uint32_t timeout_ms)
{
    if (timeout_ms == OS_TIMEOUT_NOT_LIMIT_MS)
        return OS_TIMEOUT_NOT_LIMIT_END;

    return systick_get_time_us() + (uint64_t)timeout_ms * 1000;
}

uint32_t systick_us_to_timeout(uint64_t end)
{
    if (end == OS_TIMEOUT_NOT_LIMIT_END)
        return OS_TIMEOUT_NOT_LIMIT_MS;

    uint64_t now = systick_get_time_us();
    if (now >= end)
        return 0;
    return (end - now + 1000-1) / 1000;
}

int thread_waiter_wait_until(thread_waiter_t *waiter, uint64_t end)
{
    return thread_waiter_wait_timeout(waiter, systick_us_to_timeout(end));
}

int mutex_lock_until(struct mutex * mutex, uint64_t end)
{
    return mutex_lock_timeout(mutex, systick_us_to_timeout(end));
}

int thread_wait_until(uint64_t end)
{
    return thread_wait_timeout(systick_us_to_timeout(end));
}

int thread_cond_wait_until(
    thread_cond_t *cond, struct mutex *mutex, uint64_t end)
{
    return thread_cond_wait_timeout(cond, mutex, systick_us_to_timeout(end));
}

int critical_thread_cond_wait_until(
    critical_thread_cond_t *cond, uint64_t end)
{
    return critical_thread_cond_wait_timeout(cond, systick_us_to_timeout(end));
}

int semaphore_wait_until(semaphore_t *sem, uint64_t end)
{
    return semaphore_wait_timeout(sem, systick_us_to_timeout(end));
}
