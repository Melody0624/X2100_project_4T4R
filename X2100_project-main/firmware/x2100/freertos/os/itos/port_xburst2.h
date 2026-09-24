#ifndef _PORT_XBURST2_H_
#define _PORT_XBURST2_H_

#include <soc/cpu_regs.h>
#include "task_data.h"

#define config_it_task_cpu_nums 2
#define config_it_task_priority_nums 5
#define config_it_task_min_stack_size 1024
#define config_it_task_stack_size_align 8
#define config_it_task_task_size_align 8

#define config_it_task_tick_hz 1000

#define config_enable_it_task_trace 0

int port_it_task_current_cpu_id(void);
void port_it_task_enter_critical(void);
void port_it_task_exit_critical(void);

void port_it_task_disable_local_irq(void);
void port_it_task_enable_local_irq(void);

void port_it_task_yield(int cpu_id);
void port_it_task_enter_idle(unsigned int ticks);
void *port_it_task_init_stack(void *stack_pointer, it_task_entry task_entry, void *user_data);
void port_it_task_start(int cpu_id);

void port_it_task_startup_cpu(int cpu_id);
void port_it_task_shutdown_cpu_prepare(int cpu_id);
void port_it_task_shutdown_cpu(int cpu_id);

#endif /* _PORT_XBURST2_H_ */
