#include "task.h"
#include "it_task_port.h"
#include <common.h>

static struct it_task *task_a, *task_b;

void dump_delayed_list(void);

void *test_func_A(void *data)
{
    while (1) {
        unsigned long value;
        it_task_wait(-1, &value);
        port_it_task_enter_critical();
        printf("[cpu %d] %s runing: %ld\n", port_it_task_current_cpu_id(), __FUNCTION__, value);
        dump_delayed_list();
        port_it_task_exit_critical();
    }
}

void *test_func_B(void *data)
{
    int i = 0;
    while (1) {
        port_it_task_enter_critical();
        printf("[cpu %d] %s runing\n", port_it_task_current_cpu_id(), __FUNCTION__);
        port_it_task_exit_critical();
        it_task_wakeup(task_a, i++);
        it_task_suspend(NULL, 5);
    }
}

void *test_func_C(void *data)
{
    while (1) {
        port_it_task_enter_critical();
        printf("[cpu %d] %s runing\n", port_it_task_current_cpu_id(), __FUNCTION__);
        port_it_task_exit_critical();
        it_task_suspend(NULL, 3);
    }
}

#include <cpu/irqflags.h>

void *test_func_D(void *data)
{
    while (1) {
        port_it_task_enter_critical();
        printf("[cpu %d] %s runing\n", port_it_task_current_cpu_id(), __FUNCTION__);
        port_it_task_exit_critical();
        it_task_suspend(NULL, 3);
    }
}

void *test_func_E(void *data)
{
    while (1) {
        port_it_task_enter_critical();
        printf("[cpu %d] %s runing\n", port_it_task_current_cpu_id(), __FUNCTION__);
        port_it_task_exit_critical();

        it_task_suspend(NULL, 3);
    }
}

void it_task_test(void)
{
    task_a = it_task_create("task a", 4, test_func_A, 2048, 0);
    task_b = it_task_create("task b", 3, test_func_B, 2048, 0);
    it_task_create("task c", 3, test_func_C, 2048, 0);
    it_task_create("task d", 2, test_func_D, 2048, 0);
    it_task_create("task e", 2, test_func_E, 2048, 0);
}