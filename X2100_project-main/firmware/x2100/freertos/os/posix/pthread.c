#include <string.h>
#include <pthread.h>
#include <os.h>
#include <errno.h>
#include <stdlib.h>
#include <list.h>
#include <assert.h>

/* Define PTHREAD_STACK_MIN if not defined by pthread.h */
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 4096  /* Minimum stack size for a thread (4KB) */
#endif

struct m_key {
    struct list_head list;
    void (*destructor)(void *);
};

struct m_key_data {
    struct list_head link; // link 到每个线程
    struct list_head link2; // link 到对应的 key
    struct m_key *key; // 当 key == NULL 时,表示key删除了,但是线程的私有数据还在,实际上此时已经内存泄露了
    void *data;
};

struct m_thread {
    int detachstate;
    int stacksize;
    void *(*startroutine)(void *);
    thread_ptr_t thread;
    void *ret_value;

    thread_waiter_t waiter;
    thread_ptr_t join_thread;
    struct list_head keylist;
    int is_stop;
};

#define M_DEFAULT_STACK_SIZE 8192

static pthread_attr_t default_attr = {
    .schedpolicy = SCHED_OTHER,
    .schedparam.sched_priority = OS_priority_normal,
    .detachstate = PTHREAD_CREATE_JOINABLE,
    .stacksize = M_DEFAULT_STACK_SIZE,
};

/* Forward declaration */
static __attribute__((__noreturn__)) void pthread_do_exit(struct m_thread *thread_data);

int pthread_attr_init(pthread_attr_t *attr)
{
    *attr = default_attr;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    (void) attr;

    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
    if (attr->detachstate & PTHREAD_CREATE_JOINABLE)
        *detachstate = PTHREAD_CREATE_JOINABLE;
    else
        *detachstate = PTHREAD_CREATE_DETACHED;

    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
   if(detachstate != PTHREAD_CREATE_DETACHED &&
      detachstate != PTHREAD_CREATE_JOINABLE)
        return EINVAL;

    attr->detachstate = detachstate;

    return 0;
}

int pthread_attr_getschedparam(
    const pthread_attr_t *attr, struct sched_param *param)
{
    *param = attr->schedparam;

    return 0;
}

int pthread_attr_setschedparam(
    pthread_attr_t *attr, const struct sched_param *param)
{
    if (param == NULL)
        return EINVAL;

    if (param->sched_priority > OS_priority_realtime ||
        param->sched_priority < OS_priority_idle)
        return ENOTSUP;

    attr->schedparam = *param;

    return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy)
{
    ( void ) attr;
    ( void ) policy;

    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
    *stacksize = attr->stacksize;

    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
    if (stacksize < PTHREAD_STACK_MIN)
        return EINVAL;

    attr->stacksize = stacksize;

    return 0;
}

pthread_t pthread_self(void)
{
    return (pthread_t) thread_get_user_data(NULL);
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
    return t1 == t2;
}

int pthread_getschedparam(
    pthread_t thread, int *policy, struct sched_param *param)
{
    struct m_thread *thread_data = (void *)thread;

    if (policy)
        *policy = SCHED_OTHER;
    if (param)
        param->sched_priority = thread_get_priority(thread_data->thread);

    return 0;
}

int pthread_setschedparam(
    pthread_t thread, int policy, const struct sched_param *param)
{
    struct m_thread *thread_data = (void *)thread;

    if (param == NULL)
        return EINVAL;

    if (param->sched_priority > OS_priority_realtime ||
        param->sched_priority < OS_priority_idle)
        return ENOTSUP;

    thread_set_priority(thread_data->thread, param->sched_priority);

    return 0;
}


static __attribute__((__noreturn__)) void pthread_do_exit(struct m_thread *thread_data)
{
    /* 删除线程私有数据
     */
    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &thread_data->keylist) {
        struct m_key_data *data = list_entry(pos, struct m_key_data, link);
        void (*destructor)(void *) = NULL;
        os_enter_critical();
        list_del(&data->link);
        if (data->key) {
            list_del(&data->link2);
            destructor = data->key->destructor;
        }
        os_exit_critical();

        if (destructor)
            destructor(data->data);

        free(data);
    }

    if (thread_data->detachstate == PTHREAD_CREATE_JOINABLE) {
        thread_waiter_wait(&thread_data->waiter);
        thread_data->is_stop = 1;
        thread_wakeup(thread_data->join_thread);
        while (1)
            thread_suspend(NULL);
    } else {
        free(thread_data);
        thread_delete(NULL);
        /* thread_delete(NULL) deletes current thread and never returns, but compiler doesn't know */
        while (1);
    }
}


static void pthread_thread_func(void *data)
{
    struct m_thread *thread_data = thread_get_user_data(NULL);

    thread_data->ret_value = thread_data->startroutine(data);

    pthread_do_exit(thread_data);
}


void pthread_exit(void *retval)
{
    struct m_thread *thread_data = thread_get_user_data(NULL);

    thread_data->ret_value = retval;

    pthread_do_exit(thread_data);
    /* pthread_do_exit() never returns - either suspends forever or deletes the thread */
}


int pthread_join(pthread_t pthread, void **retval)
{
    struct m_thread *thread_data = (void *)pthread;

    if (thread_data->detachstate != PTHREAD_CREATE_JOINABLE)
        return EDEADLK;

    thread_ptr_t thread = thread_get_current();

    if (thread == thread_data->thread)
        return EDEADLK;

    os_enter_critical();

    if (thread_data->join_thread) {
        os_exit_critical();
        return EDEADLK;
    }

    thread_data->join_thread = thread;
    thread_waiter_wakeup(&thread_data->waiter);

    os_exit_critical();

    while (!thread_data->is_stop)
        thread_wait();

    if (retval)
        *retval = thread_data->ret_value;

    thread_delete(thread_data->thread);
    free(thread_data);

    return 0;
}

int pthread_create(pthread_t *thread,
    const pthread_attr_t *attr, void *(*startroutine)(void *), void *arg)
{
    struct m_thread *thread_data = malloc(sizeof(*thread_data));
    int priority;

    if (!attr)
        attr = &default_attr;

    thread_data->stacksize = attr->stacksize;
    thread_data->detachstate = attr->detachstate;
    thread_data->startroutine = startroutine;
    thread_data->join_thread = NULL;
    thread_data->is_stop = 0;
    thread_waiter_init(&thread_data->waiter);

    priority = attr->schedparam.sched_priority;

    INIT_LIST_HEAD(&thread_data->keylist);

    os_enter_critical();
    thread_data->thread = thread_create(
        "pthread", thread_data->stacksize, pthread_thread_func, arg);
    thread_set_priority(thread_data->thread, priority);
    thread_set_user_data(thread_data->thread, thread_data);
    *thread = (pthread_t) thread_data;
    os_exit_critical();

    return 0;
}

int pthread_detach(pthread_t pthread)
{
    struct m_thread *thread_data = (void *)pthread;

    thread_data->detachstate = PTHREAD_CREATE_DETACHED;

    return 0;
}

int pthread_key_create(pthread_key_t *__key,
                void (*destructor)(void *))
{
    struct m_key *key = malloc(sizeof(*key));

    assert(key);

    key->destructor = destructor;
    INIT_LIST_HEAD(&key->list);

    *__key = (long) key;

    return 0;
}

int pthread_setspecific(pthread_key_t __key, const void *value)
{
    struct m_key *key = (void *) __key;

    struct m_thread *thread_data = thread_get_user_data(NULL);

    os_enter_critical();

    struct m_key_data *m_data = NULL;

    struct list_head *pos;
    list_for_each(pos, &thread_data->keylist) {
        struct m_key_data *data = list_entry(pos, struct m_key_data, link);
        if ((void *)data->key == key) {
            m_data = data;
            break;
        }
    }

    if (!m_data) {
        m_data = malloc(sizeof(*m_data));
        assert(m_data);
        m_data->key = key;
        list_add_tail(&m_data->link, &thread_data->keylist);
        list_add_tail(&m_data->link2, &key->list);
    }

    m_data->data = (void *)value;

    os_exit_critical();

    return 0;
}

void *pthread_getspecific(pthread_key_t __key)
{
    struct m_key *key = (void *) __key;

    struct m_thread *thread_data = thread_get_user_data(NULL);

    os_enter_critical();

    struct m_key_data *m_data = NULL;

    struct list_head *pos;
    list_for_each(pos, &thread_data->keylist) {
        struct m_key_data *data = list_entry(pos, struct m_key_data, link);
        if ((void *)data->key == key) {
            m_data = data;
            break;
        }
    }

    os_exit_critical();

    return m_data ? m_data->data : NULL;
}

/* Thread-Specific Data Key Deletion, P1003.1c/Draft 10, p. 167 */

int pthread_key_delete(pthread_key_t __key)
{
    struct m_key *key = (void *) __key;

    os_enter_critical();

    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &key->list) {
        struct m_key_data *data = list_entry(pos, struct m_key_data, link2);
        list_del(&data->link2);
        data->key = NULL;
        if (key->destructor)
            printf("pthread key: key %p has data %p not free!\n", key, data);
    }

    free(key);

    os_exit_critical();

    return 0;
}