#ifndef WAIT_QUEUE_H
#define WAIT_QUEUE_H
#include <os.h>
#include "task.h"
#include "task_data.h"

struct wait_queue_waiter {
    int is_pending;
    int priority;
    struct list_head link;
    thread_ptr_t *thread;
};

static inline uint32_t msecs_to_ticks(uint32_t msecs)
{
    if (msecs == -1)
        return msecs;
    if (config_it_task_tick_hz < 1000)
        return msecs / (1000 / config_it_task_tick_hz);
    else
        return msecs * (config_it_task_tick_hz / 1000);
}

static inline int wait_queue_has_waiter(wait_queue_t *wq)
{
    return !list_empty(wq);
}

void wait_queue_add_waiter(wait_queue_t *wq, struct wait_queue_waiter *waiter);

int wait_queue_wait_timeout(struct wait_queue_waiter *waiter, uint32_t timeout_ms);

void wait_queue_wake_up(wait_queue_t *wq, int nr);

void wait_queue_wake_up_mutex(wait_queue_t *wq, struct mutex *mutex, int nr);

#endif      //WAIT_QUEUE_H
