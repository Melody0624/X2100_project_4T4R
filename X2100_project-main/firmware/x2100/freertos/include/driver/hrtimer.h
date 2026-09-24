#ifndef _HR_TIMER_H_
#define _HR_TIMER_H_

#include "stdint.h"
#include "spinlock.h"
#include "list.h"
#include <driver/systick.h>

#define TIME_MAX                        (~((uint64_t)0))

/*
 * Values to track state of the timer
 *
 * Possible states:
 *
 * 0x00     inactive
 * 0x01     enqueued into rbtree
 * 0x02     callback function running
 *
 *
 * The "callback function running and enqueued" status is only possible on
 * SMP. It happens for example when a posix timer expired and the callback
 * queued a signal. Between dropping the lock which protects the posix timer
 * and reacquiring the base lock of the hrtimer, another CPU can deliver the
 * signal and rearm the timer. We have to preserve the callback running state,
 * as otherwise the timer could be removed before the softirq code finishes the
 * the handling of the timer.
 *
 * The HRTIMER_STATE_ENQUEUED bit is always or'ed to the current state
 * to preserve the HRTIMER_STATE_CALLBACK in the above scenario. This
 * also affects HRTIMER_STATE_MIGRATE where the preservation is not
 * necessary. HRTIMER_STATE_MIGRATE is cleared after the timer is
 * enqueued on the new cpu.
 *
 * All state transitions are protected by cpu_base->lock.
 */
#define HRTIMER_STATE_INACTIVE          0x00
#define HRTIMER_STATE_ENQUEUED          0x01
#define HRTIMER_STATE_CALLBACK          0x02


struct hrtimer {
    struct list_head node;
    uint64_t expires;
    void (*function)(struct hrtimer *);
    struct hrtimer_cpu_base *cpu_base;
    unsigned long state;
};

struct hrtimer_cpu_base {
    spinlock_t lock;
    unsigned int active_bases;

    uint64_t expires_next;
    struct clock_event_device *clock_base;
    struct list_head queue;
};

/*
 * Helper function to check, whether the timer is on one of the queues
 */
static inline int hrtimer_is_queued(struct hrtimer *timer)
{
    return timer->state & HRTIMER_STATE_ENQUEUED;
}

/*
 * Helper function to check, whether the timer is running the callback
 * function
 */
static inline int hrtimer_callback_running(struct hrtimer *timer)
{
    return timer->state & HRTIMER_STATE_CALLBACK;
}

/*
 * Forward a hrtimer so it expires after now:
 */
static inline void hrtimer_forward(struct hrtimer *timer, uint64_t interval)
{
    timer->expires = timer->expires + interval;
}

/*
 * hrtimer_start - (re)start an hrtimer
 * @timer:  the timer to be added
 * @tim:    expiry time
 *
 * Returns:
 *  0 on success
 *  1 when the timer was active
 */
extern int hrtimer_start_at_expires(struct hrtimer *timer, uint64_t expires);

extern int hrtimer_start(struct hrtimer *timer, uint64_t tim);

/*
 * similar as hrtimer_start, but it more sample than hrtimer_start.
 * it is invoked ONLY when there is hrtimer handler callback context
 */
extern void hrtimer_restart(struct hrtimer *timer, uint64_t tim);

/*
 * similar as hrtimer_start_at_expires
 * it is invoked ONLY when there is hrtimer handler callback context
 */
extern void hrtimer_restart_at_expires(struct hrtimer *timer, uint64_t expires);

/*
 * hrtimer_try_to_cancel - try to deactivate a timer
 * @timer:  hrtimer to stop
 *
 * Returns:
 *  0 when the timer was not active
 *  1 when the timer was active
 * -1 when the timer is currently excuting the callback function and
 *    cannot be stopped
 */
extern int hrtimer_try_to_cancel(struct hrtimer *timer);

/*
 * hrtimer_cancel - cancel a timer and wait for the handler to finish.
 * @timer:  the timer to be cancelled
 *
 * Returns:
 *  0 when the timer was not active
 *  1 when the timer was active
 */
extern int hrtimer_cancel(struct hrtimer *timer);

/*
 * hrtimer_init - initialize a timer to the given clock
 * @timer:  the timer to be initialized
 */
extern void hrtimer_init(struct hrtimer *timer, void (*function_handler)(struct hrtimer *));

extern void hrtimer_core_init(void);

/*
 * low level api, do not call it unless you know the details
 */
void hrtimer_reset_irq_time(void);

#endif /* _HR_TIMER_H_ */
