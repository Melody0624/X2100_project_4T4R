#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>

static sem_t sem;
static pthread_t task1, task2;
static pthread_mutex_t mutex;
static pthread_mutexattr_t mutexattr;

void *sem1(void *arg)
{
    int ret;
    struct timespec t;

    pthread_mutex_lock(&mutex);
    printf("sem1 is running\n");
    pthread_mutex_unlock(&mutex);

    ret = sem_wait(&sem);
    if (ret)
        printf("sem_wait is failure : %d\n", ret);

    pthread_mutex_lock(&mutex);
    printf("sem1 after wait\n");
    pthread_mutex_unlock(&mutex);

    ret = sem_trywait(&sem);
    if (ret) {
        pthread_mutex_lock(&mutex);
        printf("sem_trywait is failure : %d\n", errno);
        pthread_mutex_unlock(&mutex);
    }

    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += 2;

    ret = sem_timedwait(&sem, &t);
    if (ret) {
        pthread_mutex_lock(&mutex);
        printf("sem_timedwait is failure : %d\n", errno);
        pthread_mutex_unlock(&mutex);
    } else {
        pthread_mutex_lock(&mutex);
        printf("sem_timedwait is success\n");
        pthread_mutex_unlock(&mutex);
    }

    return NULL;
}

void *sem2(void *arg)
{
    int ret;

    pthread_mutex_lock(&mutex);
    printf("sem2 is running\n");
    pthread_mutex_unlock(&mutex);

    ret = sem_post(&sem);
    if (ret)
        printf("sem_post is failure : %d\n", ret);

    pthread_mutex_lock(&mutex);
    printf("sem2 after post\n");
    pthread_mutex_unlock(&mutex);

    return NULL;
}

void test_sem(void)
{
    int num, ret;


    ret = sem_init(&sem, 0, 1);
    if (ret) {
        printf("sem_init is failure : %d\n", errno);
        return;
    }

    sem_getvalue(&sem, &num);
    printf("sem value = %d\n", num);

    pthread_mutex_init(&mutex, &mutexattr);

    pthread_create(&task1, NULL, sem1, NULL);
    sleep(1);

    pthread_create(&task2, NULL, sem2, NULL);
    sleep(1);

    pthread_join(task1, NULL);
    pthread_join(task2, NULL);

    pthread_mutex_destroy(&mutex);

    ret = sem_destroy(&sem);
    if (ret)
        printf("sem_destroy is failure : %d\n", ret);
}