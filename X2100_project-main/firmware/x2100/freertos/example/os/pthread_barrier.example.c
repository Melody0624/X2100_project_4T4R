#include <stdio.h>
#include <pthread.h>
#include <errno.h>


static pthread_barrier_t barrier;


void *task1(void *arg)
{
    printf("this is a task1\n");
    pthread_barrier_wait(&barrier);
    printf("after wait task1\n");

    return NULL;
}

void *task2(void *arg)
{
    printf("this is a task2\n");
    pthread_barrier_wait(&barrier);
    printf("after wait task2\n");

    return NULL;
}

void test_barrier(void)
{
    int ret;
    pthread_attr_t attr;
    pthread_t test_barrier1, test_barrier2;

    pthread_attr_init(&attr);

    ret = pthread_barrier_init(&barrier, NULL, 3);
    if (ret) {
        printf("pthread_barrier_init is failure : %d\n", ret);
        return;
    }

    pthread_create(&test_barrier1, NULL, task1, NULL);
    sleep(1);
    pthread_create(&test_barrier2, NULL, task2, NULL);
    sleep(1);

    printf("wait the last barrier wait\n");
    sleep(1);

    ret = pthread_barrier_wait(&barrier);
    if (ret)
        printf("pthread_barrier_wait is failure : %d\n", ret);

    ret = pthread_barrier_destroy(&barrier);
    if (ret) {
        printf("pthread_barrier_destory is failure : %d\n", ret);
        return;
    }

    pthread_join(test_barrier1, NULL);
    pthread_join(test_barrier2, NULL);
}