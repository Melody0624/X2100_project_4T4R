#include <os.h>
#include <stdio.h>

static unsigned int count_cond = 0;
static struct mutex m_mutex;
static thread_cond_t m_cond;

void thread_consumer1(void *data)
{
    mutex_lock(&m_mutex);
    while (count_cond < 20) {
        /*
         * 首先 mutex_unlock()
         * 然后 wait 在 thread_cond 上
         * 从此函数退出时再次 mutex_lock()
         */
        thread_cond_wait(&m_cond, &m_mutex);
        printf("%s is wake up %u\r\n", __func__, count_cond);
    }
    mutex_unlock(&m_mutex);
}

void thread_consumer2(void *data)
{
    mutex_lock(&m_mutex);
    while (count_cond < 40) {
        thread_cond_wait(&m_cond, &m_mutex);
        printf("%s is wake up %u\r\n", __func__, count_cond);
    }
    mutex_unlock(&m_mutex);
}

void thread_producer(void *data)
{
    while (1) {
        msleep(3000);
        mutex_lock(&m_mutex);
        count_cond += 1;

        printf("\n");
        printf("%s is broadcast signal\r\n", __func__);
        thread_cond_broadcast(&m_cond);
        mutex_unlock(&m_mutex);
    }
}

void test_thread_cond(void)
{
    mutex_init(&m_mutex);
    thread_cond_init(&m_cond);
    thread_create("consumer1", 8192, thread_consumer1, NULL);
    thread_create("consumer2", 8192, thread_consumer2, NULL);
    thread_create("producer", 8192, thread_producer, NULL);
}
