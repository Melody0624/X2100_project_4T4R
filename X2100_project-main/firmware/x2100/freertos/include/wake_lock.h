#ifndef _WAKE_LOCK_H_
#define _WAKE_LOCK_H_

#include <driver/hrtimer.h>
#include <list.h>

struct wake_lock {
    const char *name;
    unsigned int is_locked;
    unsigned char is_init;
    unsigned char is_timer;
    struct hrtimer timer;
    struct list_head link;
};

/**
 * @brief 初始化wake lock
 * @param lock wake lock 的指针
 * @param name wake lock 的名字
 */
void wake_lock_init(struct wake_lock *lock, const char *name);

/**
 * @brief 将wake lock移出内部列表
 * @param lock wake lock 的指针
 */
void wake_lock_deinit(struct wake_lock *lock);

/**
 * @brief 增加wake lock 的引用计数
 * @param lock wake lock 的指针
 */
void wake_lock(struct wake_lock *lock);

/**
 * @brief 增加wake lock 的引用计数,并在 timeout之后自动减少wake lock 引用计数
 * @param lock wake lock 的指针
 * @param timeout 增加引用计数的时间, 单位是 us
 */
void wake_lock_timeout(struct wake_lock *lock, unsigned int timeout);

/**
 * @brief 减少wake lock 的引用计数
 * @param lock wake lock 的指针
 * @note 如果使用了 wake_lock_timeout 并且减少后的引用计数是0,
 * 那么本函数会取消timeout
 */
void wake_unlock(struct wake_lock *lock);

/**
 * @brief 判断当前wake lock引用计数是否不为0
 * @param lock wake lock 的指针
 * @return 0: 当前wake lock 引用计数为0  1: 引用计数不为0
 */
int wake_lock_is_locked(struct wake_lock *lock);

/**
 * @brief 获得所有引用计数不为0的wake lock的数量
 * @return 引用计数不为0的wake lock的数量
 */
int wake_lock_get_locked_count(void);

/**
 * @brief 等待所有的wake lock的引用计数为0
 * @return 0:当前wake lock 引用计数为0  -1:等待超时，引用计数不为0
 */
int wake_lock_wait_unlock_timeout(unsigned int timeout);

/**
 * @brief 等待所有的wake lock的引用计数为0
 * @return 0:当前wake lock 引用计数为0  -1:等待超时，引用计数不为0
 * @note 必须在os_enter_critical环境下
 */
int wake_lock_wait_timeout(unsigned int timeout);

/**
 * @brief 打印所有wake lock的名字及其引用计数
 */
void wake_locks_show(void);

#endif /* _WAKE_LOCK_H_ */