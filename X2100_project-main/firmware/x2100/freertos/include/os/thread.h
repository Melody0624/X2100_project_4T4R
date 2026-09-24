#ifndef _FREERTOS_THREAD_H_
#define _FREERTOS_THREAD_H_

#include <stdio.h>
#include <unistd.h>

/*
 * os thread priority id.
 */
typedef enum {
    OS_priority_idle = 0,               /*!< os priority idle, the lowest priority */
    OS_priority_low,                     /*!< os priority lower, the lower priority than normal */
    OS_priority_normal,                  /*!< os priority normal, the default priority of a thread. */
    OS_priority_high,                    /*!< os priority high, the higher priority than normal */
    OS_priority_realtime,                /*!< os priority realtime, the highest priority. */
    OS_priority_err,                     /*!< error priority */
} os_priority_id;

/*
 * @brief Test if the priority is the valid priority
 * @param priority the test priority
 */
static inline int os_priority_is_valid(os_priority_id priority)
{
    return (unsigned int)priority < OS_priority_err;
}

/**
 * Identify of a os thread.
 * It only create by thread_create(), and destroy by thread_delete().
 */
typedef void * thread_ptr_t;

/*
 * os thread state id.
 */
typedef enum {
    OS_thread_running = 0,         /*!< os thread state running, thread is the current thread */
    OS_thread_ready,               /*!< os thread state ready, thread is ready to run */
    OS_thread_blocked,             /*!< os thread state ready, thread is in blocked state */
    OS_thread_suspended,           /*!< os thread state ready, thread is in suspend state, and can not wake up by itself */
    OS_thread_deleted,             /*!< os thread state ready, thread has been deleted, but its TCB not yet been freed.  */
    OS_thread_err,
} os_thread_state_id;

/*
 * Entry fucntion of a os thread.
 * It is one of the parameter of thread_create().
 * thread will exit if return from this function.
 */
typedef void (*thread_func_t) (void *data);

/*
 * @brief Create a os thread in normal os priority.
 * @param thread_name Pointer to thread name. It must in heap or static, not the stack variable.
 * @param stack_size Stack size in bytes of your thread.
 * @param thread_func Pointer to the thread entry function .
 * @param data Pointer to the thread private data, will be the entry function's parameter.
 * @retval thread_ptr_t
 * @details the following code is example of use a thread.
 */
extern thread_ptr_t thread_create(const char *thread_name,  unsigned int stack_depth, thread_func_t thread_func, void *data);

/*
 * @brief Delete the specified thread
 * @param thread The thread pointer which created by thread_create().
 *                  NULL is mean current thread.
 * @details thread_delete() must called before your thread_func want to return.
 *          The following code a example.
 *          And other thread can call thread_join() to wait thread delete or exit.
 */
extern void thread_delete(thread_ptr_t thread);

/*
 * @brief set the retval of the thread
 * @param thread The thread pointer which created by thread_create().
 *                  NULL is mean current thread.
 */
extern void thread_set_retval(thread_ptr_t thread, void *retval);

/*
 * @brief Wait the specified thread deleted
 * @param thread The thread pointer which created by thread_create().
 * @param retval if not null, then it will get the retval set by thread_set_retval()
 * @retval 0: thread is deleted and the retval is geted if set.
 *        -ENODEV: thread is not valid when you call this function
 * @details if you notify a thread to exit, you should call thread_join() as soon as possible.
 *          otherwise the thread's resource may be automatically released by idle thread,
 *          then thread_join() will be failed with -ENODEV retval.
 */
extern int thread_join(thread_ptr_t thread, void **retval);

/*
 * @brief   Set the priority of a thread.
 * @details We do not suggest to change the thread priority.
 *          Most of time, set the priority is no different than not set.
 *          And it may let your code highly dependent on this attribute.
 *          Think more times, before you want change the priority.
 * @param thread The thread pointer which created by thread_create().
 *                  NULL is mean the current thread.
 * @param priority  The priority id you want to set.
 */
extern void thread_set_priority(thread_ptr_t thread, os_priority_id priority);

/*
 * @brief get the priority of a thread.
 * @retval os_priority_id
 */
extern os_priority_id thread_get_priority(thread_ptr_t thread);

/*
 * @brief Supspend the specified thread
 *        Excute twice or more times thread_suspend() to one thread,
 *        will resume by a single call thread_resume()
 * @param thread The thread pointer which created by thread_create().
 *                  NULL is mean current thread.
 * @attention thread_suspend can not excute from ISR.
 *            thread_resume can excute from ISR.
 */
extern void thread_suspend(thread_ptr_t thread);

/*
 * @brief Get name of giving thread handle
 * @param thread The thread pointer which created by thread_create().
 *                  NULL is mean current thread.
 * @return name of thread
 */
const char *thread_get_name(thread_ptr_t thread);

/*
 * @brief resume the specified suspended thread
 *        Excute twice or more times thread_suspend() to one thread,
 *        will resume by a single call thread_resume()
 * @param thread The thread pointer which created by thread_create().
 * @attention thread_suspend can not excute from ISR.
 *            thread_resume can excute from ISR.
 *            Do not pass NULL to resume current thread, it is no possible to resume one thread itself.
 */
extern void thread_resume(thread_ptr_t thread);

/*
 * @brief yield from current thread
 */
extern void thread_yield(void);

/*
 * @brief get the thread state.
 * @param thread The thread pointer which created by thread_create().
 * @retval os_thread_state_id
 * @attention Do not pass NULL to get the current thread's state.
 *            Current thread is always return OS_thread_running.
 */
extern os_thread_state_id thread_get_state(thread_ptr_t thread);

/*
 * @brief get the current running thread.
 * @retval thread_ptr_t
 */
extern thread_ptr_t thread_get_current(void);

/*
 * @brief Test if thread is suspended.
 * @param The thread id which created by thread_create().
 * @retrun 1 is true, 0 is not.
 */
extern int thread_is_suspend(thread_ptr_t thread);


/*
 * @brief Test if thread is ready to run.
 * @param The thread id which created by thread_create().
 * @retrun 1 is true, 0 is not.
 */
extern int thread_is_ready(thread_ptr_t thread);

/*
 * @brief Test if thread is deleted.
 * @param The thread id which created by thread_create().
 * @retrun 1 is true, 0 is not.
 */
extern int thread_is_deleted(thread_ptr_t thread);

/*
 * @brief Thread wait
 */
extern void thread_wait(void);

/*
 * @brief Thread wait timeout
 * @param ms is the count of timeout.
 * @return -1 is timeout, 0 is not timeout.
 */
extern int thread_wait_timeout(uint32_t ms);

/*
 * @brief Thread wait until end time
 * @param end is the end time base on systick_get_time_us(). -1 if no end
 * @return -1 is timeout, 0 is not timeout.
 */
extern int thread_wait_until(uint64_t end);

/*
 * @brief Wake up thread from ISR or from other thread by thread_id
 */
extern void thread_wakeup(thread_ptr_t thread);

/*
 * @brief sleep sec seconds
 */
unsigned sleep(unsigned int sec);

/*
 * @brief sleep msec milliseconds
 */
extern void msleep(unsigned int msec);

/*
 * @brief sleep usec microsecond
 * @return 0 on success
 */
extern int usleep(useconds_t usec);

/*
 * @brief get the user data of given thread
 * @param thread the thread ptr, NULL means the current thread
 */
extern void *thread_get_user_data(thread_ptr_t thread);

/*
 * @brief set the user data of given thread
 * @param thread the thread ptr, NULL means the current thread
 * @param user_data the user data ptr
 */
extern void thread_set_user_data(thread_ptr_t thread, void *user_data);

/*
 * @brief get the console of given thread
 * @param thread the thread ptr, NULL means the current thread
 */
extern struct console_device *thread_get_console(thread_ptr_t thread);

/*
 * @brief set the console of given thread
 * @param thread the thread ptr, NULL means the current thread
 * @param console the console ptr
 */
extern void thread_set_console(thread_ptr_t thread, struct console_device *console);

/*
 * @brief get the total run timer of given thread
 * @param thread the thread ptr, NULL means the current thread
 */
extern uint64_t thread_get_totalruntime(thread_ptr_t thread);

#endif /* _FREERTOS_THREAD_H_ */
