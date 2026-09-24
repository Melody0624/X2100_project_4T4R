#include "os.h"
#include "common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "wait_queue.h"
#include <kernel_symbol.h>
#include <driver/hrtimer.h>

void thread_init_task_data(struct task_data *task_data)
{
    task_data->console = NULL;
    task_data->user_data = NULL;
    task_data->tag = task_data;
    task_data->ret = NULL;
    thread_waiter_init(&task_data->waiter);
}

void thread_deinit_task_data(struct task_data *task_data)
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
    TaskHandle_t handle = NULL;
    BaseType_t ret;

    assert(thread_name && thread_func);

    stack_size /= sizeof(portSTACK_TYPE);

    if (stack_size < configMINIMAL_STACK_SIZE)
        stack_size = configMINIMAL_STACK_SIZE;

    ret = xTaskCreate(thread_func, thread_name, stack_size, data, (UBaseType_t) OS_priority_normal, &handle);
    assert(ret == pdPASS);

    return (thread_ptr_t) handle;
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

    vTaskDelete((TaskHandle_t) thread);
}
EXPORT_SYMBOL(thread_delete);

void thread_set_priority(thread_ptr_t thread, os_priority_id priority)
{
    assert(os_priority_is_valid(priority));
    vTaskPrioritySet((TaskHandle_t) thread, (UBaseType_t) priority);
}
EXPORT_SYMBOL(thread_set_priority);

os_priority_id thread_get_priority(thread_ptr_t thread)
{
    return (os_priority_id) uxTaskPriorityGet((TaskHandle_t) thread);
}
EXPORT_SYMBOL(thread_get_priority);

void thread_suspend(thread_ptr_t thread)
{
    vTaskSuspend((TaskHandle_t) thread);
}
EXPORT_SYMBOL(thread_suspend);

void thread_resume(thread_ptr_t thread)
{
    assert(thread);

    if (os_in_handler_mode())
        xTaskResumeFromISR((TaskHandle_t) thread);
    else
        vTaskResume((TaskHandle_t) thread);
}
EXPORT_SYMBOL(thread_resume);

void thread_yield(void)
{
    portYIELD();
}
EXPORT_SYMBOL(thread_yield);

os_thread_state_id thread_get_state(thread_ptr_t thread)
{
    eTaskState state;
    os_thread_state_id os_state;

    assert(thread);

    state = eTaskGetState((TaskHandle_t) thread);
    switch (state) {
    case eRunning:
        os_state = OS_thread_running;
        break;
    case eReady:
        os_state = OS_thread_ready;
        break;
    case eBlocked:
        os_state = OS_thread_blocked;
        break;
    case eSuspended:
        os_state = OS_thread_suspended;
        break;
    case eDeleted:
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
    return (thread_ptr_t) xTaskGetCurrentTaskHandle();
}
EXPORT_SYMBOL(thread_get_current);

const char *thread_get_name(thread_ptr_t thread)
{
    return pcTaskGetName(thread);
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

    int ret = xTaskNotifyWait(0, 0, NULL, msecs_to_ticks(ms));

    return ret ? 0 : -1;
}
EXPORT_SYMBOL(thread_wait_timeout);

void thread_wait(void)
{
    thread_wait_timeout(portMAX_DELAY);
}
EXPORT_SYMBOL(thread_wait);

void thread_wakeup(thread_ptr_t thread)
{
    assert(thread);

    BaseType_t pxHigherPriorityTaskWoken;

    if (os_in_handler_mode()) {
        xTaskNotifyFromISR((TaskHandle_t) thread, 0, eNoAction, &pxHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
    } else
        xTaskNotify((TaskHandle_t) thread, 0, eNoAction);
}
EXPORT_SYMBOL(thread_wakeup);

void msleep(unsigned int msec)
{
    assert(!os_in_handler_mode());
    vTaskDelay(msecs_to_ticks(msec));
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