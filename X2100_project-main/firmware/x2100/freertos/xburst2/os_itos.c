#include <common.h>
#include <os.h>
#include <spinlock.h>
#include <cpu/cpu.h>
#include <driver/cache.h>

#include <cpu/arch_spinlock.h>

#include "os/itos/task.h"

static volatile unsigned long flags;
static volatile int cpu_id;
static volatile int count;

static arch_spinlock_t lock = __ARCH_SPIN_LOCK_UNLOCKED;

#define m_spin_lock_irqsave(lock, flags) \
    do { \
        local_irq_save(flags); \
        while (!arch_spin_trylock(lock)); \
    } while (0)

#define m_spin_unlock(lock) \
    do { \
        arch_spin_unlock(lock); \
    } while (0)

#define m_spin_unlock_irqrestore(lock, flags) \
    do { \
        arch_spin_unlock(lock); \
        local_irq_restore(flags); \
    } while (0)

int os_is_enter_critical(void)
{
    if (cpu_id != arch_get_cpu_id())
        return 0;

    return !!count;
}

void os_enter_critical(void)
{
    unsigned long tmp;

    while (1) {
        m_spin_lock_irqsave(&lock, tmp);
        int cur_cpu_id = arch_get_cpu_id();
        if (count == 0) {
            flags = tmp;
            cpu_id = cur_cpu_id;
            break;
        }

        if (cpu_id == cur_cpu_id)
            break;

        m_spin_unlock_irqrestore(&lock, tmp);
    }

    count++;

    m_spin_unlock(&lock);
}

void os_exit_critical(void)
{
    unsigned long tmp;
    int need_restore = 0;
    unsigned long copy_flags;

    m_spin_lock_irqsave(&lock, tmp);

    if (count <= 0 ||
        cpu_id != arch_get_cpu_id())
        panic("unbalanced os_exit_critical called\n");

    if (--count == 0) {
        copy_flags = flags;
        need_restore = 1;
    }

    m_spin_unlock_irqrestore(&lock, tmp);

    if (need_restore)
        local_irq_restore(copy_flags);
}

int os_handler_mode[2];

int os_in_handler_mode(void)
{
    int ret;
    unsigned long tmp;

    local_irq_save(tmp);
    ret = os_handler_mode[arch_get_cpu_id()];
    local_irq_restore(tmp);

    return ret;
}

static __align(8) unsigned char irq_stack_mem[CONFIG_OS_EXCEPTION_STACK_SIZE*2];

unsigned char *irq_stack_pointer[2] = {
    irq_stack_mem + CONFIG_OS_EXCEPTION_STACK_SIZE,
    irq_stack_mem + CONFIG_OS_EXCEPTION_STACK_SIZE*2,
};

static void start_second_cpu(void)
{
    int id = arch_get_cpu_id();

    it_task_start_kernel(id);
}

static int m_cpu_id;

void os_start_scheduler(void)
{
    m_cpu_id = arch_get_cpu_id();

    if (m_cpu_id != 1)
        arch_startup_cpu(1, (unsigned long)start_second_cpu);

    it_task_start_kernel(m_cpu_id);
}

void os_stop_other_cpu(void)
{
    if (m_cpu_id == 0)
        it_task_stop_cpu(1);
}

void os_start_other_cpu(void)
{
    if (m_cpu_id == 0)
        it_task_start_cpu(1);
}

void os_end_scheduler(void)
{
    panic("not implemented\n");
}
