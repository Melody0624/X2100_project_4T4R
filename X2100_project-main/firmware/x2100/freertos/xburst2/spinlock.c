#include "spinlock.h"
#include <common.h>
#include <cpu/cpu.h>
#include <cpu/arch_spinlock.h>

#ifdef CONFIG_OS
#include <os.h>
#endif

static inline void spin_lock_check(spinlock_t *lock)
{
    lock->count++;
    if (lock->count != 1)
        panic("unblanced spin lock: %s, count: %d\n", lock->name, lock->count);
}

static inline void spin_unlock_check(spinlock_t *lock)
{
    if (lock->count != 1)
        panic("unblanced spin unlock: %s, count: %d\n", lock->name, lock->count);
    lock->count--;
}

void spin_lock_init(spinlock_t *lock)
{
    *lock = SPINLOCK_INITIALIZER(lock);
}

void spin_lock_init_recursive(spinlock_t *lock)
{
    *lock = SPINLOCK_RECURSIVE_INITIALIZER(lock);
}

static void *get_current_thread(void)
{
    void *thread;

    if (os_in_handler_mode())
        thread = (void *)-(arch_get_cpu_id() + 1);
    else
        thread = thread_get_current();

    return thread;
}

void spin_lock(spinlock_t *lock)
{
#ifdef CONFIG_OS
    if (!os_in_handler_mode())
        os_disable_preempt();
#endif
    // arch_spin_lock(&lock->arch_lock);
    uint64_t old = systick_get_time_us();
    while (!arch_spin_trylock(&lock->arch_lock)) {
        if (lock->recursive) {
            if (lock->thread == get_current_thread()) {
                lock->re_count++;
#ifdef CONFIG_OS
        if (!os_in_handler_mode())
                os_enable_preempt();
#endif
                return;
            }
        }

        uint64_t now = systick_get_time_us();
        if (now - old >= 1000*1000) {
            printf("error: spin lock is dead: %p(%s)\n", lock, lock->name);
            dump_stack();
            while (1);
        }
    }

    spin_lock_check(lock);

    if (lock->recursive)
        lock->thread = get_current_thread();
}

void spin_unlock(spinlock_t *lock)
{
    if (lock->recursive) {
        if (lock->re_count) {
            assert(lock->thread == get_current_thread());
            lock->re_count--;
            return;
        }

        lock->thread = NULL;
    }

    spin_unlock_check(lock);
    arch_spin_unlock(&lock->arch_lock);
#ifdef CONFIG_OS
    if (!os_in_handler_mode())
        os_enable_preempt();
#endif
}

void spin_lock_irq(spinlock_t *lock)
{
    local_irq_disable();
    spin_lock(lock);
}

void spin_unlock_irq(spinlock_t *lock)
{
    spin_unlock(lock);
    local_irq_enable();
}