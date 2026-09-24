#include <stdio.h>
#include <os.h>

static struct mutex mutex;
static struct mutex mutex1;

static void test3(void)
{
        msleep(10);
        mutex_lock(&mutex);
        printf("\r\n%s carry out\r\n", __func__);
        mutex_unlock(&mutex);
}

void thread_mutex_test2(void *data)
{
        mutex_lock(&mutex1);
        test3();
        mutex_unlock(&mutex1);
}

void thread_mutex_test1(void *data)
{
        mutex_lock(&mutex);
        test3();
        mutex_unlock(&mutex);
}

void test_mutex(void)
{
        mutex_init(&mutex1);
        mutex_init_recursive(&mutex);

        thread_create("thread_test1", 8192, thread_mutex_test1, NULL);
        thread_create("thread_test2", 8192, thread_mutex_test2, NULL);
}