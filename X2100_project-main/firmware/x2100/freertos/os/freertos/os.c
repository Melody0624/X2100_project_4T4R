#include "os.h"
#include "common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "task_data.h"
#include <kernel_symbol.h>

uint64_t os_total_idle_time;

void os_start_scheduler(void)
{
    assert(!os_in_handler_mode());

    vTaskStartScheduler();
}
EXPORT_SYMBOL(os_start_scheduler);

void os_end_scheduler(void)
{
    assert(!os_in_handler_mode());

    vTaskEndScheduler();
}
EXPORT_SYMBOL(os_end_scheduler);

int os_is_runing(void)
{
    return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}
EXPORT_SYMBOL(os_is_runing);

void os_schedule(void)
{
    portYIELD();
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

static int count_disable_preempt = 0;

void os_disable_preempt(void)
{
	if (os_in_handler_mode()) {
		count_disable_preempt++;
		return;
	}

	assert(!count_disable_preempt);

	vTaskSuspendAll();
}
EXPORT_SYMBOL(os_disable_preempt);

void os_enable_preempt(void)
{
	if (os_in_handler_mode()) {
		count_disable_preempt--;
		assert(count_disable_preempt >= 0);
		return;
	}

	assert(!count_disable_preempt);

	xTaskResumeAll();
}
EXPORT_SYMBOL(os_enable_preempt);

int os_preempt_is_disabled(void)
{
	if (os_in_handler_mode())
		return count_disable_preempt;
	else
		return xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED;
}
EXPORT_SYMBOL(os_preempt_is_disabled);
