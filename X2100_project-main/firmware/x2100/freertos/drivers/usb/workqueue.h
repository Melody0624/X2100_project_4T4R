#ifndef _WORKQUEUE_H
#define _WORKQUEUE_H

#include <driver/hrtimer.h>

struct workqueue_struct {
	struct list_head	list;

	spinlock_t lock;

	unsigned int active;
	thread_waiter_t	thread_notifier;
	thread_ptr_t thread_task;
};

#define WORK_STRUCT_STATE_UNINIT		0
#define WORK_STRUCT_STATE_INACTIVE		1
#define WORK_STRUCT_STATE_ACTIVE		2
#define WORK_STRUCT_STATE_PENDING		3

struct work_struct {
	struct list_head entry;

	unsigned int state;

	void *data;
	thread_func_t thread_func;
};

struct delayed_work {
	struct work_struct work;

	struct hrtimer timer;
	struct workqueue_struct *wq;
};

extern void create_workqueue(struct workqueue_struct *wq);
extern void destroy_workqueue(struct workqueue_struct *wq);

extern void init_work_struct(struct work_struct *work, thread_func_t thread_func, void *data);
extern int queue_work(struct workqueue_struct *wq, struct work_struct *work);
extern int work_pending(struct work_struct *work);

extern void init_delayed_work_struct(struct delayed_work *dwork, thread_func_t thread_func, void *data);
extern void queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork, unsigned long delay);
extern void cancel_delayed_work(struct delayed_work *dwork);
extern int delayed_work_pending(struct delayed_work *dwork);

#endif
