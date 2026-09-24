#include <common.h>
#include <os.h>
#include <irqflags.h>
#include "cpu/cpu.h"

static unsigned long flags;
static int count;

int os_is_enter_critical(void)
{
    return !!count;
}

void os_enter_critical(void)
{
    unsigned long tmp;

    if (arch_get_cpu_id() == 1) return;

    local_irq_save(tmp);
    if (count++ == 0)
        flags = tmp;
}

void os_exit_critical(void)
{
    if (arch_get_cpu_id() == 1) return;

    if (count <= 0)
        panic("unbalanced os_exit_critical called %d times\n", count);

    if (--count == 0)
        local_irq_restore(flags);
}

int os_handler_mode = 0;

int os_in_handler_mode(void)
{
    return os_handler_mode;
}

void os_stop_other_cpu(void)
{

}

void os_start_other_cpu(void)
{

}
