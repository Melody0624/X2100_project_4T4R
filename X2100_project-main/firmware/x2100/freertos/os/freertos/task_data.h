#ifndef _TASK_DATA_H_
#define _TASK_DATA_H_

/* This file is here for os inner implement, user do not care it */

#include <common.h>
#include <list.h>
#include <os.h>
#include <errno.h>
#include <driver/console.h>

/* private struct, user do not care */
struct task_data {
	struct console_device *console;
	void *user_data;
	void *tag;
	void *ret;
	thread_waiter_t waiter;
};

/* private function, user do not care */
void thread_init_task_data(struct task_data *task_data);

/* private function, user do not care */
void *thread_get_task_data(thread_ptr_t thread);

/* private function, user do not care */
void thread_deinit_task_data(struct task_data *task_data);

#endif /* _TASK_DATA_H_ */
