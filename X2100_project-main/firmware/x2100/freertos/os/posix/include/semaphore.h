/*
 * POSIX Semaphore API
 * This header provides POSIX semaphore support for FreeRTOS
 * Implementation is in os/posix/semaphore.c
 */

#ifndef _SEMAPHORE_H_
#define _SEMAPHORE_H_

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX        0x7FFFU    /* Maximum value of a sem_t */
#endif

typedef unsigned int sem_t;

int sem_init(sem_t *sem, int pshared, unsigned value);
int sem_getvalue(sem_t *sem, int *sval);
int sem_post(sem_t *sem);
int sem_timedwait(sem_t *sem, const struct timespec *abstime);
int sem_trywait(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_destroy(sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* _SEMAPHORE_H_ */
