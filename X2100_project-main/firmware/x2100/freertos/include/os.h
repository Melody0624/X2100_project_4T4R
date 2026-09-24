#ifndef _OS_H_
#define _OS_H_

#include <stdint.h>

#define OS_TIMEOUT_NOT_LIMIT_MS -1
#define OS_TIMEOUT_NOT_LIMIT_END -1

uint64_t timeout_to_systick_us(uint32_t timeout_ms);

uint32_t systick_us_to_timeout(uint64_t end);

extern void os_enter_critical(void);

extern void os_exit_critical(void);

extern int os_is_enter_critical(void);

extern int os_in_handler_mode(void);

extern void os_start_scheduler(void);

extern void os_end_scheduler(void);

extern int os_is_runing(void);

extern void os_schedule(void);

extern void os_set_errno(int error);

extern int os_get_errno(void);

extern uint64_t os_get_total_idle_time(void);

extern void os_disable_preempt(void);

extern void os_enable_preempt(void);

extern int os_preempt_is_disabled(void);

extern void os_stop_other_cpu(void);

extern void os_start_other_cpu(void);

#include <os/thread.h>
#include <os/mutex.h>
#include <os/thread_cond.h>
#include <os/critical_thread_cond.h>
#include <os/thread_waiter.h>
#include <os/semaphore.h>

#endif /* _OS_H_ */
