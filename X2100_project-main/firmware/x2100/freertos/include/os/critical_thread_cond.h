#ifndef CRITICAL_THREAD_COND_H
#define CRITICAL_THREAD_COND_H

#include <os/os_def.h>

/**
 * struct of critical thread condition
 * It is init by critical_thread_cond_init().
 */
typedef struct critical_thread_cond_struct {
    wait_queue_t wait_queue;
} critical_thread_cond_t;

#define __CRITICAL_THREAD_COND_INITIALIZER(condname) \
    {.wait_queue = LIST_HEAD_INIT(condname.wait_queue)}

#define DEFINE_CRITICAL_THREADCOND(condname) \
	critical_thread_cond_t condname = __CRITICAL_THREAD_COND_INITIALIZER(condname)

/**
 * @brief wait on a critical_thread_cond_t with a timeout. see critical_thread_cond_wait() for details.
 * @param cond the pointer to critical_thread_cond_t
 * @param timeout_ms timeout_ms milliseconds to wait. To set no limit timeout use THREAD_COND_TIMEOUT_NO_LIMIT_MS.
 * @return 0 if wake up by a signal from other thread, -1 if timeout.
 * @attention no matter the timeout is happen or not,  os_enter_critical() will be called when this function is return.
 */
int critical_thread_cond_wait_timeout(
        critical_thread_cond_t *cond, uint32_t timeout_ms);

/**
 * @brief wait on a critical_thread_cond_t until the end time. see critical_thread_cond_wait() for details.
 * @param cond the pointer to critical_thread_cond_t
 * @param end end is the end time base on systick_get_time_us(). -1 if not end.
 * @return 0 if wake up by a signal from other thread, -1 if timeout.
 * @attention no matter the timeout is happen or not,  os_enter_critical() will be called when this function is return.
 */
int critical_thread_cond_wait_until(
        critical_thread_cond_t *cond, uint64_t end);

/**
 * @brief init a critical_thread_cond_t
 * @param cond thre pointer to critical_thread_cond_t
 */
void critical_thread_cond_init(critical_thread_cond_t *cond);

/**
 * @brief wait on a critical_thread_cond_t
 * @param cond the pointer to critical_thread_cond_t
 * @details critical_thread_cond_wait() is used to wait some signal from other thread.
 *          This is the example code to descript the details.
 * @code
 * static unsigned int count_cond = 0;
 * static critical_thread_cond_t m_cond;
 *
 * void thread_consumer1(void *data)
 * {
 *      os_enter_critical();
 *      while (count_cond < 20) {
 *          critical_thread_cond_wait(&m_cond);
 *          printf("%s is wake up %u\r\n", __func__, count_cond);
 *      }
 *      os_exit_critical();
 * }
 *
 * void thread_consumer2(void *data)
 * {
 *      os_enter_critical();
 *      while (count_cond < 40) {
 *          critical_thread_cond_wait(&m_cond);
 *          printf("%s is wake up %u\r\n", __func__, count_cond);
 *      }
 *      os_exit_critical();
 * }
 *
 * void thread_producer(void *data)
 * {
 *      while (1) {
 *          msleep(3000);
 *
 *          os_enter_critical();
 *          count_cond += 1;
 *          printf("\r\n%s is broadcast signal\r\n", __func__);
 *          critical_thread_cond_broadcast(&m_cond);
 *          os_exit_critical();
 *      }
 * }
 *
 * void test_thread_cond(void)
 * {
 *      critical_thread_cond_init(&m_cond);
 *      thread_create("consumer1", 8192, thread_consumer1, NULL);
 *      thread_create("consumer2", 8192, thread_consumer2, NULL);
 *      thread_create("producer", 8192, thread_producer, NULL);
 * }
 *
 * @endcode
 */
void critical_thread_cond_wait(critical_thread_cond_t *cond);

/**
 * @brief send a signal to a critical_thread_cond_t, to wake up one waiter.
 * @param cond the pointer to critical_thread_cond_t
 */
void critical_thread_cond_signal(critical_thread_cond_t *cond);

/**
 * @brief send a signal to a critical_thread_cond_t, to wake up all waiters.
 * @param cond the pointer to critical_thread_cond_t
 */
void critical_thread_cond_broadcast(critical_thread_cond_t *cond);

#endif  //CRITICAL_THREAD_COND_H
