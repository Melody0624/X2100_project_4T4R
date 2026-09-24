#include <os.h>
#include <stdio.h>

static unsigned int count_cond = 0;
static critical_thread_cond_t m_cond;

void thread_consumer1(void *data)
{
    os_enter_critical();
    while (count_cond < 20) {
        /*
         * os_exit_critical()
         * 然后wait 在 thread_cond 上
         * 函数返回时自动 os_enter_critical()
         */
        critical_thread_cond_wait(&m_cond);
        printf("%s is wake up %u\r\n", __func__, count_cond);
    }
    os_exit_critical();
}
void thread_consumer2(void *data)
{
    os_enter_critical();
    while (count_cond < 40) {
        critical_thread_cond_wait(&m_cond);
        printf("%s is wake up %u\r\n", __func__, count_cond);
    }
    os_exit_critical();

    thread_delete(NULL);
}
void thread_producer(void *data)
{
    while (1) {
        msleep(100);
        os_enter_critical();
        count_cond += 1;
        printf("\r\n%s is broadcast signal\r\n", __func__);
        critical_thread_cond_broadcast(&m_cond);
        os_exit_critical();
    }
}
void test_thread_cond(void)
{
    critical_thread_cond_init(&m_cond);
    thread_create("consumer1", 8192, thread_consumer1, NULL);
    thread_create("consumer2", 8192, thread_consumer2, NULL);
    thread_create("producer", 8192, thread_producer, NULL);
}