#include <FreeRTOS.h>
#include <task.h>
#include <list.h>
#include "wait_queue.h"

static void init_waiter(struct wait_queue_waiter *waiter)
{
    thread_ptr_t thread = thread_get_current();
    int priority = thread_get_priority(thread);

    waiter->thread = thread;
    waiter->priority = priority;
    waiter->is_pending = 1;
}

static void add_waiter(wait_queue_t *wq, struct wait_queue_waiter *waiter)
{
    struct list_head *node;
    int priority = waiter->priority;

    list_for_each_prev(node, wq) {
        struct wait_queue_waiter *tmp = list_entry(node, struct wait_queue_waiter, link);

        if (priority <= tmp->priority)
            break;
    }

    list_add(&waiter->link, node);
}

static inline void delete_waiter(struct wait_queue_waiter *waiter)
{
    waiter->is_pending = 0;
    list_del(&waiter->link);
}

static thread_ptr_t get_first_waiter(wait_queue_t *wq)
{
    struct wait_queue_waiter *waiter = list_entry(wq->next, struct wait_queue_waiter, link);

    delete_waiter(waiter);

    return waiter->thread;
}

void wait_queue_add_waiter(wait_queue_t *wq, struct wait_queue_waiter *waiter)
{
    init_waiter(waiter);
    add_waiter(wq, waiter);
}

int wait_queue_wait_timeout(struct wait_queue_waiter *waiter, uint32_t timeout_ms)
{
    BaseType_t ret;

    do {
        ret = xTaskNotifyWait(0, 0, NULL, msecs_to_ticks(timeout_ms));
        if (ret == pdFAIL) {
            os_enter_critical();
            if (waiter->is_pending) {
                ret = -1;
                delete_waiter(waiter);
            } else {
                /* 即使等待超时,但是仍然被唤醒的情况 */
                ret = 0;
            }
            os_exit_critical();
            return ret;
        }
    } while(waiter->is_pending);

    return 0;
}

void wait_queue_wake_up(wait_queue_t *wq, int nr)
{
    thread_ptr_t thread;

    while ((!list_empty(wq)) && (nr-- > 0)) {
        BaseType_t pxHigherPriorityTaskWoken;

        thread = get_first_waiter(wq);
        if (os_in_handler_mode()) {
            xTaskNotifyFromISR((TaskHandle_t) thread, 0, eNoAction, &pxHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
        } else
            xTaskNotify(thread, 0, eNoAction);
    }
}

void wait_queue_wake_up_mutex(wait_queue_t *wq, struct mutex *mutex, int nr)
{
    struct list_head *node;

    if (list_empty(wq) || nr <= 0)
        return ;

    assert(mutex);

    if (mutex->count == 0) {
        nr--;

        /**
         *  if mutex in unlock statue, then lock the mutex
         *  and notify a thread, and move others threads in
         *  thread cond wait queue to mutex wait queue
         */
        mutex->count++;

        wait_queue_wake_up(wq, 1);
    }

    while ((!list_empty(wq)) && ((nr--) > 0)) {

        node = wq->next;
        list_del(wq->next);

        /**
         *  equals to mutex_lock()
         */
        mutex->count++;

        list_add(node, &mutex->wait_queue);
    }
}
