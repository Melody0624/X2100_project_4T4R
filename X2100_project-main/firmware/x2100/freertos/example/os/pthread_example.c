#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>

#define STACK 8192


void *test_exit(void *arg)
{
    printf("will be exit\n");

    pthread_exit("exit value");

    printf("these words will never be seen\n");
}

void *test_pthread(void *arg)
{
    int ret, state;
    size_t stack;
    pthread_attr_t attr;
    struct sched_param param;
    pthread_t thread_id;

    ret = pthread_attr_init(&attr);
    if (ret) {
        printf("pthread_attr_init failure :%d\n", ret);
        return NULL;
    }

    thread_id = pthread_self();
    printf("thread_id is %u\n", (pthread_t)thread_id);

    ret = pthread_equal(thread_id, pthread_self());
    if (!ret) {
        printf("pthread_equal is failure :%d\n", ret);
        return NULL;
    }

    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (ret)
        printf("setdetachstate is failure : %d\n", ret);

    ret = pthread_attr_getdetachstate(&attr, &state);
    if (state == PTHREAD_CREATE_DETACHED)
        printf("this is a deatch\n");
    else
        printf("pthread_attr_getdetachstate is failure : %d\n", ret);


    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    if (ret)
        printf("setdetachstate is failure : %d\n", ret);

    ret = pthread_attr_getdetachstate(&attr, &state);
    if (state == PTHREAD_CREATE_JOINABLE)
        printf("this is a joinable\n");
    else
        printf("pthread_attr_getdetachstate is failure : %d\n", ret);

    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    ret = pthread_attr_setschedpolicy(&attr, SCHED_RR);
    if (ret)
        printf("SCHED_RR is failure :%d\n", ret);

    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    ret = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    if (ret)
        printf("SCHED_FIFO is failure :%d\n", ret);

    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    ret = pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
    if (ret)
        printf("SCHED_OTHER is failure : %d\n", ret);

    pthread_attr_getschedparam(&attr, &param);
    ret = pthread_attr_getschedparam(&attr, &param);
    if (ret) {
        printf("param set up failure : %d\n", ret);
        return NULL;
    }

    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret)
        printf("pthread_attr_setschedparam is failure : %d\n", ret);

    ret = pthread_attr_setstacksize(&attr, STACK);
    if (ret)
        printf("pthread_attr_setstacksize is failure : %d\n", ret);

    pthread_attr_getstacksize(&attr, &stack);
    ret = pthread_attr_getstacksize(&attr, &stack);
    if (ret) {
        printf("stacksize is failure : %d\n", ret);
        return NULL;
    }
    printf("stacksize is %d\n", stack);

    pthread_attr_destroy(&attr);
    ret = pthread_attr_destroy(&attr);
    if (ret) {
        printf("pthread_attr_destroy failure : %d\n", ret);
        return NULL;
    }

    printf("pthread excution completed\n");

    return NULL;
}

void use_pthread(void)
{
    int ret;
    pthread_t test_thread_id, test_exit_id;
    void *pthread_return;

    ret = pthread_create(&test_thread_id, NULL, test_pthread, NULL);
    if (ret)
        printf("pthread_create is failure : %d\n", ret);

    ret = pthread_join(test_thread_id, NULL);
    if (ret)
        printf("pthread_join is failure : %d\n", ret);

    printf("pthread_join has been finished\n");

    printf("to begin testing pthread exit\n");

    pthread_create(&test_exit_id, NULL, test_exit, NULL);
    if (ret)
        printf("pthread_create is failure : %d\n", ret);

    pthread_join(test_exit_id, &pthread_return);
    if (ret)
        printf("pthread_create is failure : %d\n", ret);

    printf("test exit finished! return value : %s\n", (char *)pthread_return);

    printf("all test has been finished\n");
}