#ifndef _THREAD_WAITER_H_
#define _THREAD_WAITER_H_

#include "os_def.h"

typedef struct thread_waiter {
    volatile void *thread;
    volatile int is_pending;
} thread_waiter_t;

#define __THREAD_WAITER_INITIALIZER(waitername) \
    {.thread = NULL, .is_pending = 0}

#define DEFINE_THREAD_WAITER(waitername) \
    thread_waiter_t waitername = __THREAD_WAITER_INITIALIZER(waitername)

/**
 * @brief 初始化 thread_waiter_t
 */
void thread_waiter_init(thread_waiter_t *waiter);

/**
 * @brief 带超时的等待
 * @param waiter 当前线程在 watier上等待,其他线程/中断唤醒 watier
 * @param timeout_ms 超时的时间,单位毫秒 OS_TIMEOUT_NOT_LIMIT_MS 表示永不超时
 * @return 0 表示没有超时, -1 表示超时
 */
int thread_waiter_wait_timeout(thread_waiter_t *waiter, uint32_t timeout_ms);

/**
 * @brief 带超时的等待
 * @param waiter 当前线程在 watier上等待,其他线程/中断唤醒 watier
 * @param end 超时的时间,基于 systick_get_time_us() 算出, -1 永不不超时
 * @return 0 表示没有超时, -1 表示超时
 */
int thread_waiter_wait_until(thread_waiter_t *waiter, uint64_t end);

/**
 * @brief 等待
 * @param waiter 当前线程在 watier上等待,其他线程/中断唤醒 watier
 * @code
 * #include <os.h>
 *
 * static thread_waiter_t waiter;
 * static struct mutex lock;
 *
 * void wait_something(void)
 * {
 *     mutex_lock(&lock);
 * 
 *     thread_waiter_wait(&waiter);
 * 
 *     printf("wakeuped\n");
 * 
 *     mutex_unlock(&lock);
 * }
 *
 * void wakeup_something(void)
 * {
 *     thread_waiter_wakeup(&waiter);
 * }
 *
 * void m_init(void)
 * {
 *     thread_waiter_init(&waiter);
 *     mutex_init(&lock);
 * }
 */
void thread_waiter_wait(thread_waiter_t *waiter);

/**
 * @brief 唤醒等待者
 * @param waiter 唤醒等待在waiter上的thread
 */
void thread_waiter_wakeup(thread_waiter_t *waiter);

#endif /* _THREAD_WAITER_H_ */