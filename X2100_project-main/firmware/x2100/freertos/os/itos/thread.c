#include "os.h"
#include "common.h"
#include "task.h"
#include "wait_queue.h"
#include <kernel_symbol.h>
#include <driver/hrtimer.h>

static inline void *thread_get_task_data(thread_ptr_t thread)
{
    return it_task_get_task_data(thread);
}

void port_it_task_init_task_data(struct task_data *task_data)
{
    task_data->console = NULL;
    task_data->user_data = NULL;
    task_data->tag = task_data;
    task_data->ret = NULL;
    thread_waiter_init(&task_data->waiter);
}

void port_it_task_deinit_task_data(struct task_data *task_data)
{
    task_data->tag = NULL;
}

struct console_device *thread_get_console(thread_ptr_t thread)
{
    struct task_data *task_data = thread_get_task_data(thread);

    return task_data ? task_data->console : NULL;
}
EXPORT_SYMBOL(thread_get_console);

void thread_set_console(thread_ptr_t thread, struct console_device *console)
{
    struct task_data *task_data = thread_get_task_data(thread);

    if (task_data)
        task_data->console = console;
}
EXPORT_SYMBOL(thread_set_console);

void *thread_get_user_data(thread_ptr_t thread)
{
    struct task_data *task_data = thread_get_task_data(thread);

    return task_data ? task_data->user_data : NULL;
}
EXPORT_SYMBOL(thread_get_user_data);

void thread_set_user_data(thread_ptr_t thread, void *user_data)
{
    struct task_data *task_data = thread_get_task_data(thread);

    if (task_data)
        task_data->user_data = user_data;
}
EXPORT_SYMBOL(thread_set_user_data);

thread_ptr_t thread_create(const char *thread_name,  unsigned int stack_size, thread_func_t thread_func, void *data)
{
    assert(thread_name && thread_func);

    struct it_task *task = it_task_create(
        thread_name, OS_priority_normal, (it_task_entry)thread_func, stack_size, data);

    return (thread_ptr_t) task;
}
EXPORT_SYMBOL(thread_create);

void thread_set_retval(thread_ptr_t thread, void *retval)
{
    struct task_data *task_data = thread_get_task_data(thread);

    task_data->ret = retval;
}

int thread_join(thread_ptr_t thread, void **retval)
{
    assert(thread);

    struct task_data *task_data = thread_get_task_data(thread);
    void *tag = task_data->tag;

    if (tag == thread)
        goto out;

    if (tag != task_data)
        return -ENODEV;

    thread_waiter_wait(&task_data->waiter);

out:
    if (retval)
        *retval = task_data->ret;
    return 0;
}

void thread_delete(thread_ptr_t thread)
{
    struct task_data *task_data = thread_get_task_data(thread);

    task_data->tag = thread ? thread : thread_get_current();
    thread_waiter_wakeup(&task_data->waiter);

    /* 等10ms,让其他线程有机会调用 thread_jion
     * 否则vTaskDelete 几乎会立刻删掉thread
     */
    usleep(30*1000);

    it_task_delete((struct it_task *) thread);
}
EXPORT_SYMBOL(thread_delete);

void thread_set_priority(thread_ptr_t thread, os_priority_id priority)
{
    assert(os_priority_is_valid(priority));
    it_task_set_priority((struct it_task *) thread, priority);
}
EXPORT_SYMBOL(thread_set_priority);

os_priority_id thread_get_priority(thread_ptr_t thread)
{
    return (os_priority_id) it_task_get_priority((struct it_task *) thread);
}
EXPORT_SYMBOL(thread_get_priority);

void thread_suspend(thread_ptr_t thread)
{
    it_task_suspend((struct it_task *) thread, -1);
}
EXPORT_SYMBOL(thread_suspend);

void thread_resume(thread_ptr_t thread)
{
    assert(thread);
    it_task_resume((struct it_task *) thread);
}
EXPORT_SYMBOL(thread_resume);

void thread_yield(void)
{
    it_task_yield();
}
EXPORT_SYMBOL(thread_yield);

os_thread_state_id thread_get_state(thread_ptr_t thread)
{
    enum it_task_status status;
    os_thread_state_id os_state;

    assert(thread);

    status = it_task_get_status((struct it_task *) thread);
    switch (status) {
    case it_task_status_running:
        os_state = OS_thread_running;
        break;
    case it_task_status_ready:
        os_state = OS_thread_ready;
        break;
    case it_task_status_wait:
        os_state = OS_thread_blocked;
        break;
    case it_task_status_suspend:
        os_state = OS_thread_suspended;
        break;
    case it_task_status_delete:
        os_state = OS_thread_deleted;
        break;
    default:
        os_state = OS_thread_err;
        break;
    }

    return os_state;
}
EXPORT_SYMBOL(thread_get_state);

thread_ptr_t thread_get_current(void)
{
    return (thread_ptr_t) it_task_get_current(-1);
}
EXPORT_SYMBOL(thread_get_current);

const char *thread_get_name(thread_ptr_t thread)
{
    return it_task_get_name(thread);
}
EXPORT_SYMBOL(thread_get_name);

int thread_is_suspend(thread_ptr_t thread)
{
    return thread_get_state(thread) == OS_thread_suspended;
}
EXPORT_SYMBOL(thread_is_suspend);

int thread_is_ready(thread_ptr_t thread)
{
    return thread_get_state(thread) == OS_thread_ready;
}
EXPORT_SYMBOL(thread_is_ready);

int thread_is_deleted(thread_ptr_t thread)
{
    return thread_get_state(thread) == OS_thread_deleted;
}
EXPORT_SYMBOL(thread_is_deleted);

int thread_wait_timeout(uint32_t ms)
{
    assert(!os_in_handler_mode());

    return it_task_wait(msecs_to_ticks(ms), NULL);
}
EXPORT_SYMBOL(thread_wait_timeout);

void thread_wait(void)
{
    thread_wait_timeout(-1);
}
EXPORT_SYMBOL(thread_wait);

void thread_wakeup(thread_ptr_t thread)
{
    assert(thread);

    it_task_wakeup(thread, 0);
}
EXPORT_SYMBOL(thread_wakeup);

void msleep(unsigned int msec)
{
    assert(!os_in_handler_mode());
    it_task_suspend(NULL, msecs_to_ticks(msec));
}
EXPORT_SYMBOL(msleep);

unsigned sleep(unsigned int sec)
{
    const int N = 1000;

    while (sec > N) {
        msleep(1000*N);
        sec -= N;
    }

    if (sec)
        msleep(sec*1000);

    return 0;
}
EXPORT_SYMBOL(sleep);

struct usleep_data {
    struct hrtimer timer;
    thread_waiter_t waiter;
};

static void timer_callback(struct hrtimer *timer)
{
    struct usleep_data *temp;

    temp = container_of(timer, struct usleep_data, timer);
    thread_waiter_wakeup(&temp->waiter);
}

int usleep(useconds_t usec)
{
    struct usleep_data data;

    hrtimer_init(&data.timer, timer_callback);
    thread_waiter_init(&data.waiter);

    hrtimer_start(&data.timer, usec);
    thread_waiter_wait(&data.waiter);

    return 0;
}
EXPORT_SYMBOL(usleep);
