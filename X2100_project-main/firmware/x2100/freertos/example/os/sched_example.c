#include <stdio.h>
#include <pthread.h>
#include <sched.h>


static pthread_mutex_t mutex;

static void *task1(void *arg)
{
    int ret;

    pthread_mutex_lock(&mutex);
    printf("test sched yield task1 before\n");
    pthread_mutex_unlock(&mutex);

    ret = sched_yield();
    if (ret) {
        printf("sched_yield1 is failure : %d\n", ret);
        return NULL;
    }

    pthread_mutex_lock(&mutex);
    printf("test sched yield task1 after\n");
    pthread_mutex_unlock(&mutex);

    return NULL;
}

static void *task2(void *arg)
{
    int ret;

    pthread_mutex_lock(&mutex);
    printf("test sched yield task2 before\n");
    pthread_mutex_unlock(&mutex);

    ret = sched_yield();
    if (ret) {
        printf("sched_yield2 is failure : %d\n", ret);
        return NULL;
    }

    pthread_mutex_lock(&mutex);
    printf("test sched yield task2 after\n");
    pthread_mutex_unlock(&mutex);

    return NULL;
}

void test_sched(void)
{
    pthread_t test_yield1, test_yield2;
    pthread_mutexattr_t mutexattr;

    pthread_mutex_init(&mutex, &mutexattr);

    printf("SCHED_FIFO min:%d max:%d\n", sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(SCHED_FIFO));
    printf("SCHED_RR  min:%d max:%d\n", sched_get_priority_min(SCHED_RR), sched_get_priority_max(SCHED_RR));
    printf("SCHED_OTHER min:%d max:%d\n", sched_get_priority_min(SCHED_OTHER), sched_get_priority_max(SCHED_OTHER));

    pthread_create(&test_yield1, NULL, task1, NULL);
    pthread_create(&test_yield2, NULL, task2, NULL);

    pthread_join(test_yield1, NULL);
    pthread_join(test_yield2, NULL);
}