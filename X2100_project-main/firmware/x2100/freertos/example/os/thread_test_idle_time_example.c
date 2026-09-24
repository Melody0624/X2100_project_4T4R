#include <os.h>
#include <stdio.h>

void sleep_thread_1(void *data)
{
    unsigned int count = 0;
    while (1) {
        msleep(1);
    }
}

void sleep_thread_2(void *data)
{
    unsigned int count = 0;
    while (1) {
        if (!(++count % 10000)) {
            float idle = (float)os_get_total_idle_time() / systick_get_time_us() * 100;

            printf("idle: %6.3f%% schedule: %5.3f%%\n", idle, 100 - idle);
        }

        msleep(1);
    }
}

/*
 * 1 只运行本例程可以测出 schedule 切换与 idle，thread1, thread2 之间的时间占比
 * 2 运行其它线程的情况下，可以大致测试出其它线程的占用率（包括中断）
 */
void thread_idle_time_test(void)
{
    thread_create("sleep thread 1", 8192, sleep_thread_1, NULL);
    thread_create("sleep thread 2", 8192, sleep_thread_2, NULL);
}