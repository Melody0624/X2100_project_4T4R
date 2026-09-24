#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#define NOR PTHREAD_MUTEX_NORMAL
#define REC PTHREAD_MUTEX_RECURSIVE

static pthread_mutex_t mutex;


static void *task1(void *arg)
{
    int ret;

    ret = pthread_mutex_lock(&mutex);
    if (ret)
        printf("pthread_mutex_lock is failure : %d\n", ret);

    ret = pthread_mutex_trylock(&mutex);
    if (ret) {
        printf("the taks1 is already lock : %d\n", ret);
    }
    else {
        printf("this is a PTHREAD_MUTEX_RECURSIVE\n");
        pthread_mutex_unlock(&mutex);
    }

    printf("task1 has been locked\n");

    ret = pthread_mutex_unlock(&mutex);
    if (ret)
        printf("pthread_mutex_unlock is failure : %d\n", ret);

    return NULL;
}

static void *task2(void *arg)
{
    int ret;

    ret = pthread_mutex_lock(&mutex);
    if (ret)
        printf("pthread_mutex_lock is failure : %d\n", ret);

    ret = pthread_mutex_trylock(&mutex);
    if (ret) {
        printf("the taks2 is already lock : %d\n", ret);
    }
    else {
        printf("this is a PTHREAD_MUTEX_RECURSIVE\n");
        pthread_mutex_unlock(&mutex);
    }

    printf("task2 has been locked\n");

    ret = pthread_mutex_unlock(&mutex);
    if (ret)
        printf("pthread_mutex_unlock is failure : %d\n", ret);

    return NULL;
}

void test_mutex(void)
{
    int ret, type;
    pthread_attr_t attr;
    pthread_mutexattr_t mutexattr;
    pthread_t task_1, task_2;

    ret = pthread_attr_init(&attr);
    if (ret) {
        printf("pthread_attr_init is failure : %d\n", ret);
        return;
    }

    ret = pthread_mutexattr_init(&mutexattr);
    if (ret) {
        printf("pthread_mutexattr_init is failure : %d\n", ret);
        return;
    }

    ret = pthread_mutexattr_settype(&mutexattr, NOR);
    if (ret) {
        printf("pthread_mutexattr_settype is failure : %d\n", ret);
        return;
    }

    ret = pthread_mutexattr_gettype(&mutexattr, &type);
    if (ret) {
        printf("pthread_mutexattr_gettype is failure : %d\n", ret);
        return;
    }
    printf("pthread_mutexattr type is %d\n", type);

    ret = pthread_mutex_init(&mutex, &mutexattr);
    if (ret) {
        printf("pthread_mutex_init is failure : %d\n", ret);
        return;
    }

    pthread_create(&task_1, NULL, task1, NULL);
    pthread_create(&task_2, NULL, task2, NULL);

    pthread_join(task_1, NULL);
    pthread_join(task_2, NULL);

    ret = pthread_mutex_destroy(&mutex);
    if (ret) {
        printf("pthread_mutex_init is failure : %d\n", ret);
        return;
    }

    ret = pthread_mutexattr_destroy(&mutexattr);
    if (ret) {
        printf("pthread_mutexattr_destroy is failure : %d\n", ret);
        return;
    }

    ret = pthread_attr_destroy(&attr);
    if (ret) {
        printf("pthread_attr_destroy is failure : %d\n", ret);
        return;
    }
}