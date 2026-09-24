#include <mqueue.h>
#include <os.h>
#include <stdlib.h>
#include <string.h>
#include <list.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <common.h>

#include "time_utils.h"
#include <semaphore.h>

#define DEFAULT_MQ_MAX_MESSAGES 10
#define DEFAULT_MQ_MSG_SIZE 10


struct m_msg {
    struct list_head link;
    int priority;
    int len;
    char *data;
};

struct m_queue {
    struct list_head link;
    struct list_head pending_list;
    struct list_head msg_list;
    struct mq_attr attr;
    sem_t send_sem;
    sem_t rece_sem;
    int ref_count;
    int unlink_flag;
    char *name;
    mutex_t lock;
    unsigned int queue_busy;
};

struct handle {
    struct m_queue *queue;
    unsigned int busy;
};

static DEFINE_MUTEX(lock);
static LIST_HEAD(queue_list);

static void lock_queue_list(void)
{
    mutex_lock(&lock);
}

static void unlock_queue_list(void)
{
    mutex_unlock(&lock);
}

static void lock_queue(struct m_queue *queue)
{
    mutex_lock(&queue->lock);
}

static void unlock_queue(struct m_queue *queue)
{
    mutex_unlock(&queue->lock);
}

static struct m_queue *find_queue(const char *name)
{
    struct list_head *pos;

    list_for_each(pos, &queue_list) {
        struct m_queue *queue = list_entry(pos, struct m_queue, link);
        if (!strcmp(queue->name, name))
            return queue;
    }

    return NULL;
}

static struct m_queue *find_queue2(struct m_queue *p)
{
    struct list_head *pos;

    list_for_each(pos, &queue_list) {
        struct m_queue *queue = list_entry(pos, struct m_queue, link);
        if (queue == p)
            return queue;
    }

    return NULL;
}

static struct m_queue *mqueue_init(const char *name, struct mq_attr *attr)
{
    int name_len = strlen(name) + 1;
    int msg_len = sizeof(struct m_msg) * attr->mq_maxmsg;
    struct m_queue *queue;

    queue = malloc(sizeof(*queue) + msg_len + name_len);

    if (queue == NULL)
        return NULL;

    struct m_msg *msgs = (void *)&queue[1];
    queue->name = (void *)msgs + msg_len;
    memcpy((char *)queue->name, name, name_len);

    queue->attr = *attr;
    queue->unlink_flag  = 0;
    queue->ref_count = 0;
    queue->queue_busy = 0;

    INIT_LIST_HEAD(&queue->msg_list);
    INIT_LIST_HEAD(&queue->pending_list);

    sem_init(&queue->send_sem, 0, queue->attr.mq_maxmsg);
    sem_init(&queue->rece_sem, 0, 0);

    mutex_init(&queue->lock);

    int i;
    for (i = 0; i < attr->mq_maxmsg; i++) {
        list_add_tail(&msgs[i].link, &queue->msg_list);
        msgs[i].data = NULL;
    }

    list_add_tail(&queue->link, &queue_list);

    return queue;

}

mqd_t mq_open(const char *name, int oflag, ...)
{
    mode_t mode;
    struct mq_attr *attr;

    va_list args;

    struct handle *handle = NULL;

    struct mq_attr default_attr = {
        .mq_flags   = 0,
        .mq_maxmsg  = DEFAULT_MQ_MAX_MESSAGES,
        .mq_msgsize = DEFAULT_MQ_MSG_SIZE,
        .mq_curmsgs = 0
    };

    if (oflag & O_CREAT) {
        va_start(args, oflag);
        mode = (mode_t) va_arg(args, mode_t);
        attr = (struct mq_attr *) va_arg(args, struct mq_attr *);
        if (!attr)
            attr = &default_attr;
        va_end(args);
    }

    (void) mode;

    if (name[0] != '/') {
        errno = EINVAL;
        return (mqd_t)-1;
    }

    struct m_queue *queue;

    lock_queue_list();

    queue = find_queue(name);

    if (queue) {
        if ((oflag & O_EXCL) && (oflag & O_CREAT)) {
            errno = EEXIST;
            handle = (void*)-1;
            goto unlock;
        }

    } else {
        if (!(oflag & O_CREAT)) {
            errno = ENOENT;
            handle = (void*)-1;
            goto unlock;
        }

        if (attr->mq_maxmsg <= 0 || attr->mq_msgsize <= 0) {
            errno = EINVAL;
            handle = (void*)-1;
            goto unlock;
        }

        queue = mqueue_init(name, attr);
        if (!queue) {
            errno = ENOSPC;
            handle = (void*)-1;
            goto unlock;
        }
    }

    queue->ref_count++;

    handle = malloc(sizeof(struct handle));
    handle->queue = queue;
    handle->busy = 0;

unlock:
    unlock_queue_list();
    return (mqd_t) handle;
}

int mq_timedsend(
    mqd_t mqdes, const char *msg_ptr, size_t msg_len,
    unsigned int msg_prio, const struct timespec *abstime)
{
    struct handle *handle = (void *)mqdes;
    struct m_queue *queue = handle->queue;
    int ret = 0;

    lock_queue_list();

    if (!find_queue2(queue)) {
        errno = EBADF;
        unlock_queue_list();
        return -1;
    }
    handle->busy++;

    unlock_queue_list();

    if (msg_len > queue->attr.mq_msgsize) {
        errno = EMSGSIZE;
        ret = -1;
        goto out;
    }

    ret = sem_timedwait(&queue->send_sem, abstime);

    if (ret < 0) {
        errno = ETIMEDOUT;

        if (queue->attr.mq_flags & O_NONBLOCK)
            errno = EAGAIN;

        ret = -1;
        goto out;
    }

    lock_queue(queue);

    struct list_head *link = &queue->msg_list;
    struct m_msg *msg = list_first_entry(link, struct m_msg, link);
    msg->priority = msg_prio;

    if (!msg->data) {
        msg->data = malloc(queue->attr.mq_msgsize);
        if (!msg->data) {
            errno = ENOMEM;
            unlock_queue(queue);
            ret = -1;
            goto out;
        }
    }

    list_del(&msg->link);

    if (msg_len)
        memcpy(msg->data, msg_ptr, msg_len);

    msg->len = msg_len;

    struct list_head *pos;
    list_for_each(pos, &queue->pending_list) {
        struct m_msg *m = list_entry(pos, struct m_msg, link);
        if (msg_prio > m->priority)
            break;
    }

    list_add_tail(&msg->link, pos);
    queue->attr.mq_curmsgs++;

    sem_post(&queue->rece_sem);

    unlock_queue(queue);

out:
    lock_queue_list();
    handle->busy--;
    unlock_queue_list();

    return ret;
}

ssize_t mq_timedreceive(
    mqd_t mqdes, char *msg_ptr, size_t msg_len,
    unsigned int *msg_prio, const struct timespec *abstime)
{
    struct handle *handle = (void *)mqdes;
    struct m_queue *queue = handle->queue;
    int ret = 0;

    lock_queue_list();

    if (!find_queue2(queue)) {
        errno = EBADF;
        unlock_queue_list();
        ret = -1;
        goto out;
    }

    handle->busy++;

    unlock_queue_list();

    if (msg_len < queue->attr.mq_msgsize) {
        errno = EMSGSIZE;
        ret = -1;
        goto out;
    }

    ret = sem_timedwait(&queue->rece_sem, abstime);

    if (ret < 0) {
        errno = ETIMEDOUT;

        if (queue->attr.mq_flags & O_NONBLOCK)
            errno = EAGAIN;

        ret = -1;
        goto out;
    }

    lock_queue(queue);

    struct list_head *link = &queue->pending_list;
    struct m_msg *msg = list_first_entry(link, struct m_msg, link);

    list_del(&msg->link);
    queue->attr.mq_curmsgs--;

    if (msg_prio)
        *msg_prio = msg->priority;

    if (msg->len)
        memcpy(msg_ptr, msg->data, msg->len);

    list_add_tail(&msg->link, &queue->msg_list);

    sem_post(&queue->send_sem);

    ret = msg->len;

    unlock_queue(queue);

out:
    lock_queue_list();
    handle->busy--;
    unlock_queue_list();

    return ret;
}

ssize_t mq_receive( mqd_t mqdes,
                    char * msg_ptr,
                    size_t msg_len,
                    unsigned int * msg_prio )
{
    return mq_timedreceive( mqdes, msg_ptr, msg_len, msg_prio, NULL );
}

/*-----------------------------------------------------------*/

int mq_send( mqd_t mqdes,
             const char * msg_ptr,
             size_t msg_len,
             unsigned msg_prio )
{
    return mq_timedsend( mqdes, msg_ptr, msg_len, msg_prio, NULL );
}

static void check_del_queue(struct m_queue *queue)
{
    if(queue->ref_count == 0 && queue->unlink_flag == 1) {
        list_del(&queue->link);
        free(queue);
    }
}

int mq_close( mqd_t mqdes )
{
    struct handle *handle = (void *)mqdes;
    struct m_queue *queue = handle->queue;
    int ret = 0;

    assert(queue->ref_count);

    lock_queue_list();

    if (!find_queue2(queue)) {
        errno = EBADF;
        ret  = -1;
        goto unlock;
    }

    if (handle->busy != 0) {
        errno = EBUSY;
        ret  = -1;
        goto unlock;
    }

    queue->ref_count--;

    check_del_queue(queue);

    handle->queue = NULL;
    free(handle);

unlock:
    unlock_queue_list();

    return ret;
}

int mq_unlink( const char * name )
{
    int ret = 0;

    if (name[0] != '/') {
        errno = EINVAL;
        return -1;
    }

    lock_queue_list();
    struct m_queue *queue = find_queue(name);

    if (!queue) {
        errno = EINVAL;
        ret = -1;
        goto unlock;
    }

    if (queue->ref_count != 0)
        queue->unlink_flag = 1;

    check_del_queue(queue);

unlock:
    unlock_queue_list();
    return ret;
}

int mq_getattr( mqd_t mqdes, struct mq_attr * mqstat )
{
    struct m_queue *queue = (void*) *((unsigned int *)(mqdes));

    lock_queue_list();

    if (!find_queue2(queue)) {
        errno = EBADF;
        unlock_queue_list();
        return -1;
    }

    unlock_queue_list();

    lock_queue(queue);

    *mqstat = queue->attr;

    unlock_queue(queue);

    return 0;
}

int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr)
{
    struct m_queue *queue = (void*) *((unsigned int *)(mqdes));

    lock_queue_list();

    if (!find_queue2(queue)) {
        errno = EBADF;
        unlock_queue_list();
        return -1;
    }

    unlock_queue_list();

    if (!newattr) {
        errno = EFAULT;
        return -1;
    }

    lock_queue(queue);

    if (oldattr)
        *oldattr = queue->attr;

    queue->attr.mq_flags = newattr->mq_flags;

    unlock_queue(queue);

    return 0;
}