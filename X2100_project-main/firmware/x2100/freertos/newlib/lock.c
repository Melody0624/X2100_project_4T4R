#include <os.h>
#include <sys/lock.h>
#include <stdlib.h>
#include <common.h>

DEFINE_MUTEX_RECURSIVE(__lock___sinit_recursive_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___sfp_recursive_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___atexit_recursive_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___at_quick_exit_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___malloc_recursive_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___env_recursive_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___tz_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___dd_hash_mutex);
DEFINE_MUTEX_RECURSIVE(__lock___arc4random_mutex);

void __retarget_lock_init (_LOCK_T *lock)
{
    struct mutex *mutex = malloc(sizeof(struct mutex));

    mutex_init(mutex);

    *lock = (_LOCK_T)mutex;
}

void __retarget_lock_init_recursive(_LOCK_T *lock)
{
    struct mutex *mutex = malloc(sizeof(*mutex));

    mutex_init_recursive(mutex);

    *lock = (_LOCK_T)mutex;
}

void __retarget_lock_close(_LOCK_T lock)
{
    assert(!mutex_is_locked((struct mutex *)lock));
    free((struct mutex *)lock);
}

void __retarget_lock_close_recursive(_LOCK_T lock)
{
    assert(!mutex_is_locked((struct mutex *)lock));
    free((struct mutex *)lock);
}

void __retarget_lock_acquire (_LOCK_T lock)
{
    mutex_lock((struct mutex *)lock);
}

void
__retarget_lock_acquire_recursive (_LOCK_T lock)
{
    mutex_lock((struct mutex *)lock);
}

int
__retarget_lock_try_acquire(_LOCK_T lock)
{
  return mutex_try_lock((struct mutex *)lock);
}

int
__retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
  return mutex_try_lock((struct mutex *)lock);
}

void
__retarget_lock_release (_LOCK_T lock)
{
    mutex_unlock((struct mutex *)lock);
}

void
__retarget_lock_release_recursive (_LOCK_T lock)
{
    mutex_unlock((struct mutex *)lock);
}
