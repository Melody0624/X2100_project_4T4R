#ifndef _IT_TASK_H_
#define _IT_TASK_H_

#include <stdio.h>
#include <list.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <os.h>

#include "task.h"

#include <soc/cpu_regs.h>

struct it_task;

typedef void *(*it_task_entry)(void *user_data);

enum it_task_status {
    it_task_status_ready,
    it_task_status_running,
    it_task_status_suspend,
    it_task_status_wait,
    it_task_status_delete,
    it_task_status_first_runing,
};

#include "it_task_port.h"

void it_task_start_kernel(int cpu_id);
void it_task_stop_cpu(int cpu_id);
void it_task_start_cpu(int cpu_id);

int it_task_kernel_is_running(void);

struct it_task *it_task_create(
    const char *task_name, int task_priority, it_task_entry task_entry, 
    int stack_size, void *userdata);

void it_task_delete(struct it_task *task);

struct it_task *it_task_get_current(int cpu_id);

void it_task_suspend(struct it_task *task, unsigned int ticks);
void it_task_resume(struct it_task *task);

int it_task_wait(unsigned int ticks, unsigned long *event_value);
void it_task_wakeup(struct it_task *task, unsigned long event_value);

void it_task_yield(void);

void it_task_disable_preempt(void);
void it_task_enable_preempt(void);
int it_task_preempt_is_enable(void);

void it_task_lock_tasks(void);
void it_task_unlock_tasks(void);

void it_task_set_priority(struct it_task *task, int task_priority);
int it_task_get_priority(struct it_task *task);

void it_task_set_name(struct it_task *task, const char *task_name);
const char *it_task_get_name(struct it_task *task);

int it_task_get_id(struct it_task *task);

int it_task_get_cpu_id(struct it_task *task);

int it_task_get_task_nums(void);

struct it_task **it_task_get_tasks(void);

void it_task_free_tasks(struct it_task **array);

enum it_task_status it_task_get_status(struct it_task *task);

struct task_data *it_task_get_task_data(struct it_task *task);

void it_task_add_ticks(unsigned int ticks);

struct it_task *it_task_switch_context(void);

int it_task_get_available_stack_size(unsigned char *stack_base, int size);

#endif /* _IT_TASK_H_ */
