#ifndef __OS_MUTEX_H__
#define __OS_MUTEX_H__

#include <os/os_def.h>

/** @addtogroup framework_os_freertos
 *  @{
 */

/** @defgroup os_mutex
 *  @{
 */

/**
 * Identify of a os mutex.
 * It only inited by mutex_init()
 */

struct mutex {
    volatile unsigned int count;
    unsigned int recursive;
    unsigned int re_count;
    void *thread;
    wait_queue_t wait_queue;
};

typedef struct mutex mutex_t;

#define __MUTEX_INITIALIZER(mutexname) \
    {.count = 0,\
     .recursive = 0, \
     .re_count = 0, \
     .wait_queue = LIST_HEAD_INIT(mutexname.wait_queue)}

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

#define __MUTEX_RECURSIVE_INITIALIZER(mutexname) \
    {.count = 0,\
     .recursive = 1, \
     .re_count = 0, \
     .wait_queue = LIST_HEAD_INIT(mutexname.wait_queue)}

#define DEFINE_MUTEX_RECURSIVE(mutexname) \
	struct mutex mutexname = __MUTEX_RECURSIVE_INITIALIZER(mutexname)

/**
 * @brief Init a mutex lock.
 * @param mutex The mutex pointer
 */
void mutex_init(struct mutex * mutex);

/**
 * @brief Init a recursive mutex lock .
 * @param mutex The mutex pointer
 */
void mutex_init_recursive(struct mutex * mutex);

/**
 * @brief Hold a mutex lock.
 * @param mutex The mutex pointer which inited by mutex_init().
 */
extern void mutex_lock(struct mutex * mutex);

/**
 * @brief Unlock the mutex lock.
 * @param mutex The mutex pointer which inited by mutex_init().
 */
extern void mutex_unlock(struct mutex * mutex);

/**
 * @brief Try to lock a mutex lock with a timeout
 * @param mutex The mutex pointer which inited by mutex_init().
 * @param timeout_ms msecs to try
 * @return 0 if the mutex has been acquired successfully, -1 is timeout
 */
int mutex_lock_timeout(struct mutex * mutex, unsigned int timeout_ms);

/*
 * @brief Try to lock a mutex lock until end time
 * @param mutex The mutex pointer which inited by mutex_init().
 * @param end is the end time base on systick_get_time_us(). -1 if no end
 * @return 0 if the mutex has been acquired successfully, -1 is timeout
 */
extern int mutex_lock_until(struct mutex * mutex, uint64_t end);

/**
 * @brief Try to lock a mutex lock
 * @param mutex The mutex pointer which inited by mutex_init().
 * @return 1 if the mutex has been acquired successfully, 0 is not
 */
extern int mutex_try_lock(struct mutex * mutex);


/**
 * @brief Test if the mutex lock is locked.
 * @param mutex The mutex pointer which inited by mutex_init().
 * @return 1 if the mutex has been lock by some one, 0 is not
 */
int mutex_is_locked(struct mutex * mutex);

/**
 *  @}
 */

/**
 *  @}
 */

#endif	/* __OS_MUTEX_H__ */
