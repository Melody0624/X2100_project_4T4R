#include <os.h>

static void thread_func1(void *data)
{
    (void) data;

    /* 优先级很高, 仅次于 OS_priority_realtime */
    thread_set_priority(NULL, OS_priority_high);

    while (1) {
        printf("%s run\n", __func__);
        mdelay(500);
        printf("%s ...\n", __func__);
        msleep(500);
    }

    thread_delete(NULL);
}

static void thread_func2(void *data)
{
    (void) data;

    /* 优先级最高 */
    thread_set_priority(NULL, OS_priority_realtime);

    while (1) {
        printf("%s run\n", __func__);
        mdelay(500);
        printf("%s ...\n", __func__);
        msleep(500);
    }

    thread_delete(NULL);
}

void thread_priority_test(void)
{
    thread_create("thread1", 8192, thread_func1, NULL);
    thread_create("thread2", 8192, thread_func2, NULL);
}
