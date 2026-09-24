#ifndef _CPU_SPINLOCK_H_
#define _CPU_SPINLOCK_H_

#include <irqflags.h>
#include <assert.h>

#include <cpu/arch_spinlock_type.h>

typedef struct spinlock {
    const char *name;
    short count;
    short is_irq;
    arch_spinlock_t arch_lock;
    short recursive;
    short re_count;
    volatile void *thread;
} spinlock_t;

#define SPINLOCK_INITIALIZER(x) (spinlock_t){ \
    .count = 0, .is_irq = 0, .recursive = 0, .re_count = 0, .name = #x, \
    .thread = NULL, .arch_lock = __ARCH_SPIN_LOCK_UNLOCKED }

#define DEFINE_SPINLOCK(x)	spinlock_t x = SPINLOCK_INITIALIZER(x)

#define SPINLOCK_RECURSIVE_INITIALIZER(x) (spinlock_t){ \
    .count = 0, .is_irq = 0, .recursive = 1, .re_count = 0, .name = #x, \
    .thread = NULL, .arch_lock = __ARCH_SPIN_LOCK_UNLOCKED }

#define DEFINE_SPINLOCK_RECURSIVE(x)	spinlock_t x = SPINLOCK_RECURSIVE_INITIALIZER(x)

void spin_lock_init(spinlock_t *lock);

void spin_lock_init_recursive(spinlock_t *lock);

void spin_lock(spinlock_t *lock);

void spin_unlock(spinlock_t *lock);

void spin_lock_irq(spinlock_t *lock);

void spin_unlock_irq(spinlock_t *lock);

#define spin_lock_irqsave(lock, flags) \
    do { \
        local_irq_save(flags); \
        spin_lock(lock); \
    } while (0)

#define spin_unlock_irqrestore(lock, flags) \
    do { \
        spin_unlock(lock); \
        local_irq_restore(flags); \
    } while (0)

#endif /* _CPU_SPINLOCK_H_ */