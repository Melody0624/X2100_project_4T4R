#include <common.h>
#include <list.h>
#include <spinlock.h>
#include <os.h>
#include <driver/hrtimer.h>

#include "workqueue.h"

static void workqueue_thread(void *data)
{
	unsigned long flags;
	struct work_struct *work;
	struct workqueue_struct *wq = data;

	spin_lock_irqsave(&wq->lock, flags);
	while(wq->active) {
		while(list_empty(&wq->list)) {
			spin_unlock_irqrestore(&wq->lock, flags);

			thread_wait();

			if (!wq->active) {
				thread_waiter_wakeup(&wq->thread_notifier);
				return;
			}
			spin_lock_irqsave(&wq->lock, flags);
		}

		work = list_first_entry(&wq->list, struct work_struct, entry);
		work->state = WORK_STRUCT_STATE_PENDING;
		spin_unlock_irqrestore(&wq->lock, flags);

		if (work->thread_func)
			work->thread_func(work->data);

		spin_lock_irqsave(&wq->lock, flags);
		list_del(&work->entry);
		work->state = WORK_STRUCT_STATE_INACTIVE;
	}
	spin_unlock_irqrestore(&wq->lock, flags);
	thread_waiter_wakeup(&wq->thread_notifier);
}

void create_workqueue(struct workqueue_struct *wq)
{
	assert(wq);

	INIT_LIST_HEAD(&wq->list);
	spin_lock_init(&wq->lock);

	wq->active = 1;
	thread_waiter_init(&wq->thread_notifier);
	wq->thread_task = thread_create("workqueue_thread", 4096, workqueue_thread, wq);
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	unsigned int active;
	unsigned long flags;
	struct work_struct *work;

	assert(wq);

	spin_lock_irqsave(&wq->lock, flags);
	active = wq->active;
	if (active) {
		while(!list_empty(&wq->list)) {
			work = list_first_entry(&wq->list, struct work_struct, entry);
			list_del(&work->entry);
			work->state = WORK_STRUCT_STATE_INACTIVE;
		}

		wq->active = 0;
		thread_wakeup(wq->thread_task);
	}
	spin_unlock_irqrestore(&wq->lock, flags);

	if (active)
		thread_waiter_wait(&wq->thread_notifier);
}

void init_work_struct(struct work_struct *work, thread_func_t thread_func, void *data)
{
	assert(work);

	work->state = WORK_STRUCT_STATE_INACTIVE;
	work->data = data;
	work->thread_func = thread_func;
}

int queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	int ret = 0;
	unsigned long flags;

	assert(wq);
	assert(work);

	spin_lock_irqsave(&wq->lock, flags);
	assert(wq->active);
	if (work->state == WORK_STRUCT_STATE_INACTIVE) {
		work->state = WORK_STRUCT_STATE_ACTIVE;
		list_add_tail(&work->entry, &wq->list);
		thread_wakeup(wq->thread_task);
		ret = 1;
	}
	spin_unlock_irqrestore(&wq->lock, flags);

	return ret;
}

int work_pending(struct work_struct *work)
{
	return work->state != WORK_STRUCT_STATE_INACTIVE;
}


static void delayed_work_timer_callback(struct hrtimer *hrtimer)
{
	struct delayed_work *dwork = container_of(hrtimer, struct delayed_work, timer);

	queue_work(dwork->wq, &dwork->work);
}

void init_delayed_work_struct(struct delayed_work *dwork, thread_func_t thread_func, void *data)
{
	assert(dwork);

	hrtimer_init(&dwork->timer, delayed_work_timer_callback);
	init_work_struct(&dwork->work, thread_func, data);
}

void queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay)
{
	assert(wq);
	assert(dwork);

	dwork->wq = wq;
	hrtimer_start(&dwork->timer, delay);
}

void cancel_delayed_work(struct delayed_work *dwork)
{
	assert(dwork);

	hrtimer_cancel(&dwork->timer);
}

int delayed_work_pending(struct delayed_work *dwork)
{
	return dwork->work.state != WORK_STRUCT_STATE_INACTIVE;
}
