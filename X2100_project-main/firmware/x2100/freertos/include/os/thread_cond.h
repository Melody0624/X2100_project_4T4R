#ifndef __OS_THREAD_COND_H__
#define __OS_THREAD_COND_H__

#include <os/os_def.h>
#include <os/mutex.h>

/** @addtogroup framework_os_freertos
 *  @{
 */

/** @defgroup thread_cond
 *  @{
 */

/**
 * struct of thread condition
 * It is init by thread_cond_init().
 */
typedef struct thread_cond_struct {
    struct mutex *mutex;

    wait_queue_t wait_queue;
} thread_cond_t;


#define MAX_WAITERS INT32_MAX

#define THREAD_COND_TIMEOUT_NO_LIMIT_MS -1

#define __THREAD_COND_INITIALIZER(condname) \
    {.mutex = NULL,\
     .wait_queue = LIST_HEAD_INIT(condname.wait_queue)}

#define DEFINE_THREADCOND(condname) \
	thread_cond_t condname = __THREAD_COND_INITIALIZER(condname)

/**
 * @brief init a thread_cond_t
 * @param cond thre pointer to thread_cond_t
 */
extern void thread_cond_init(thread_cond_t *cond);

/**
 * @brief wait on a thread_cond_t with a timeout. see thread_cond_wait() for details.
 * @param cond the pointer to thread_cond_t
 * @param mutex the struct mutex * to unlock before wait, and lock after wait. it can not be NULL.
 * @param timeout_ms timeout_ms milliseconds to wait. To set no limit timeout use THREAD_COND_TIMEOUT_NO_LIMIT_MS.
 * @return 0 if wake up by a signal from other thread, -1 if timeout.
 * @attention no matter the timeout is happen or not, the mutex will be lock when this function is return.
 */
extern int thread_cond_wait_timeout(thread_cond_t *cond, struct mutex * mutex, uint32_t timeout_ms);

/**
 * @brief wait on a thread_cond_t until the end time. see thread_cond_wait() for details.
 * @param cond the pointer to thread_cond_t
 * @param mutex the struct mutex * to unlock before wait, and lock after wait. it can not be NULL.
 * @param end end is the end time base on systick_get_time_us(). -1 if no end
 * @return 0 if wake up by a signal from other thread, -1 if timeout.
 * @attention no matter the timeout is happen or not, the mutex will be lock when this function is return.
 */
extern int thread_cond_wait_until(thread_cond_t *cond, struct mutex * mutex, uint64_t end);

/**
 * @brief wait on a thread_cond_t
 * @param cond the pointer to thread_cond_t
 * @param mutex the struct mutex * to unlock before wait, and lock after wait. it can not be NULL.
 * @details thread_cond_wait() is used to wait some signal from other thread.
 *          And the mutex is help to create a atomic region to test/modify the condition
 *          This is the example code to descript the details.
 * @code
 * static unsigned int count_cond = 0;
 * static struct mutex m_mutex;
 * static thread_cond_t m_cond;
 *
 * void thread_consumer1(void *data)
 * {
 *     mutex_lock(&m_mutex);
 *     while (count_cond < 20) {
 *         thread_cond_wait(&m_cond, &m_mutex);
 *         printf("%s is wake up %u\r\n", __func__, count_cond);
 *     }
 *     mutex_unlock(&m_mutex);
 *     thread_delete(NULL);
 * }
 *
 * void thread_consumer2(void *data)
 * {
 *     mutex_lock(&m_mutex);
 *     while (count_cond < 40) {
 *         thread_cond_wait(&m_cond, &m_mutex);
 *         printf("%s is wake up %u\r\n", __func__, count_cond);
 *     }
 *     mutex_unlock(&m_mutex);
 *     thread_delete(NULL);
 * }
 *
 * void thread_producer(void *data)
 * {
 *     while (1) {
 *         msleep(3000);
 *
 *         mutex_lock(&m_mutex);
 *         count_cond += 1;
 *         printf("\r\n%s is broadcast signal\r\n", __func__);
 *         thread_cond_broadcast(&m_cond);
 *         mutex_unlock(&m_mutex);
 *     }
 * }
 *
 * void test_thread_cond(void)
 * {
 *     mutex_init(&m_mutex);
 *     thread_cond_init(&m_cond);
 *     thread_create("consumer1", 8192, thread_consumer1, NULL);
 *     thread_create("consumer2", 8192, thread_consumer2, NULL);
 *     thread_create("producer", 8192, thread_producer, NULL);
 * }
 *
 * @endcode
 */
void thread_cond_wait(thread_cond_t *cond, struct mutex * mutex);

/**
 * @brief send a signal to a thread_cond_t, to wake up one waiter.
 * @param cond the pointer to thread_cond_t
 */
void thread_cond_signal(thread_cond_t *cond);

/**
 * @brief send a signal to a thread_cond_t, to wake up all waiters.
 * @param cond the pointer to thread_cond_t
 */
void thread_cond_broadcast(thread_cond_t *cond);

/**
 *  @}
 */

/**
 *  @}
 */

#endif    /* __OS_THREAD_COND_H__ */
