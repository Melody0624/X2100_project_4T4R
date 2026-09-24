#include <stdio.h>
#include <os.h>
#include <common.h>
#include <wake_lock.h>

static int wake_lock_count = 0;

static int cond_is_wait = 0;

static DEFINE_CRITICAL_THREADCOND(cond);

static LIST_HEAD(list);

static void do_wake_lock(struct wake_lock *lock)
{
    lock->is_locked++;
    if (lock->is_locked == 1)
        wake_lock_count++;
}

static void do_wake_unlock(struct wake_lock *lock)
{
    if (!lock->is_locked)
        return;
    lock->is_locked--;
    if (lock->is_locked == 0)
        wake_lock_count--;
}

void wake_lock_hrimer_func(struct hrtimer *timer)
{
    struct wake_lock *lock = container_of(timer, struct wake_lock, timer);
    if (lock->is_timer) {
        lock->is_timer = 0;
        do_wake_unlock(lock);
    }
}

void wake_lock_init(struct wake_lock *lock, const char *name)
{
    struct list_head *pos;

    os_enter_critical();

    list_for_each(pos, &list) {
        struct wake_lock *tmp = list_entry(pos, struct wake_lock, link);
        if (tmp == lock)
            panic("wake lock: %s has been inited\n", name);
    }

    lock->name = name;
    lock->is_init = 1;
    lock->is_locked = 0;
    lock->is_timer = 0;
    hrtimer_init(&lock->timer, wake_lock_hrimer_func);
    list_add_tail(&lock->link, &list);

    os_exit_critical();
}

void wake_lock_deinit(struct wake_lock *lock)
{
    os_enter_critical();

    assert(lock->is_init);
    lock->is_init = 0;

    if (lock->is_locked)
        panic("deinit a locked wake lock: %s\n", lock->name);

    list_del(&lock->link);

    os_exit_critical();
}

void wake_lock(struct wake_lock *lock)
{
    os_enter_critical();

    assert(lock->is_init);

    do_wake_lock(lock);

    os_exit_critical();
}

void wake_lock_timeout(struct wake_lock *lock, unsigned int timeout)
{
    os_enter_critical();

    assert(lock->is_init);

    if (lock->is_timer)
        hrtimer_cancel(&lock->timer);
    else {
        lock->is_timer = 1;
        do_wake_lock(lock);
    }

    hrtimer_start(&lock->timer, timeout);

    os_exit_critical();
}

void wake_unlock(struct wake_lock *lock)
{
    os_enter_critical();

    assert(lock->is_init);

    if (lock->is_locked == 1 && lock->is_timer) {
        lock->is_timer = 0;
        hrtimer_cancel(&lock->timer);
    }

    do_wake_unlock(lock);

    if (cond_is_wait && !wake_lock_count) {
        cond_is_wait = 0;
        critical_thread_cond_broadcast(&cond);
    }

    os_exit_critical();
}

int wake_lock_is_locked(struct wake_lock *lock)
{
    return lock->is_locked != 0;
}

int wake_lock_get_locked_count(void)
{
    return wake_lock_count;
}

int wake_lock_wait_unlock_timeout(unsigned int timeout)
{
    int ret = 0;

    os_enter_critical();

    if (wake_lock_count) {
        cond_is_wait = 1;
        ret = critical_thread_cond_wait_timeout(&cond, timeout);
    }

    os_exit_critical();

    return ret;
}

int wake_lock_wait_timeout(unsigned int timeout)
{
    int ret = 0;

    if (wake_lock_count) {
        cond_is_wait = 1;
        ret = critical_thread_cond_wait_timeout(&cond, timeout);
    }

    return ret;
}

void wake_locks_show(void)
{
    struct list_head *pos, *n;

    printf("wake lock list\n");

    list_for_each_safe(pos, n, &list) {
        struct wake_lock *lock = list_entry(pos, struct wake_lock, link);
        printf("%s %d\n", lock->name, (int)lock->is_locked);
    }
}
