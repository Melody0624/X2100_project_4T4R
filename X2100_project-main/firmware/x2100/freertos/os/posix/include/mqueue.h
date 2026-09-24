/*
 * POSIX Message Queue API
 * This header provides POSIX message queue support for FreeRTOS
 * Implementation is in os/posix/mqueue.c
 */

#ifndef _MQUEUE_H_
#define _MQUEUE_H_

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int mqd_t;

struct mq_attr {
    long mq_flags;     /* Message queue flags */
    long mq_maxmsg;    /* Maximum number of messages */
    long mq_msgsize;   /* Maximum message size */
    long mq_curmsgs;   /* Number of messages currently queued */
};

mqd_t mq_open(const char *name, int oflag, ...);

int mq_timedsend(mqd_t mqdes,
                 const char *msg_ptr,
                 size_t msg_len,
                 unsigned int msg_prio,
                 const struct timespec *abstime);

ssize_t mq_timedreceive(mqd_t mqdes,
                        char *msg_ptr,
                        size_t msg_len,
                        unsigned int *msg_prio,
                        const struct timespec *abstime);

int mq_send(mqd_t mqdes,
            const char *msg_ptr,
            size_t msg_len,
            unsigned msg_prio);

ssize_t mq_receive(mqd_t mqdes,
                   char *msg_ptr,
                   size_t msg_len,
                   unsigned int *msg_prio);

int mq_close(mqd_t mqdes);
int mq_unlink(const char *name);

int mq_getattr(mqd_t mqdes, struct mq_attr *mqstat);
int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr);

#ifdef __cplusplus
}
#endif

#endif /* _MQUEUE_H_ */
