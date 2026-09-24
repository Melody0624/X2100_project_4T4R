#ifndef _OS_SEMAPHORE_H_
#define _OS_SEMAPHORE_H_

#include <os/os_def.h>

typedef struct semaphore {
    volatile int value;
    wait_queue_t wait_queue;
} semaphore_t;

#define __SEMAPHORE_INITIALIZER(semname, value) \
    {.value = value,\
     .wait_queue = LIST_HEAD_INIT(semname.wait_queue)}

#define DEFINE_SEMAPHORE(semname, value) \
    semaphore_t semname = __SEMAPHORE_INITIALIZER(semname, value)

int semaphore_init(semaphore_t *sem, unsigned int value);

int semaphore_wait_timeout(semaphore_t *sem, unsigned int timeout_ms);

int semaphore_wait_until(semaphore_t *sem, uint64_t end);

int semaphore_try_wait(semaphore_t *sem);

void semaphore_wait(semaphore_t *sem);

void semaphore_post(semaphore_t *sem);

unsigned int semaphore_get_value(semaphore_t *sem);

#endif /* _OS_SEMAPHORE_H_ */
