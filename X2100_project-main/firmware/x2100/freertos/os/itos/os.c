#include "os.h"
#include "common.h"
#include "task.h"
#include "task_data.h"
#include <kernel_symbol.h>

uint64_t os_total_idle_time;

int os_is_runing(void)
{
    return it_task_kernel_is_running();
}
EXPORT_SYMBOL(os_is_runing);

void os_schedule(void)
{
    it_task_yield();
}
EXPORT_SYMBOL(os_schedule);

uint64_t os_get_total_idle_time(void)
{
    return os_total_idle_time;
}
EXPORT_SYMBOL(os_get_total_idle_time);

void os_set_errno(int error)
{
    errno = error;
}
EXPORT_SYMBOL(os_set_errno);

int os_get_errno(void)
{
    return errno;
}
EXPORT_SYMBOL(os_get_errno);

void os_disable_preempt(void)
{
	it_task_disable_preempt();
}
EXPORT_SYMBOL(os_disable_preempt);

void os_enable_preempt(void)
{
	it_task_enable_preempt();
}
EXPORT_SYMBOL(os_enable_preempt);

int os_preempt_is_disabled(void)
{
	return !it_task_preempt_is_enable();
}
EXPORT_SYMBOL(os_preempt_is_disabled);
