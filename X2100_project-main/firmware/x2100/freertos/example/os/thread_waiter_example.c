
#include <os.h>

static thread_waiter_t waiter;
static struct mutex lock;

void wait_something(void *data)
{
    mutex_lock(&lock);

    printf("wait\n");

    thread_waiter_wait(&waiter);

    printf("wakeuped\n");

    mutex_unlock(&lock);
}

void wakeup_something(void *data)
{
    msleep(10);
    printf("wakeing up\n");
    thread_waiter_wakeup(&waiter);
}

void waiter_test_init(void)
{
    thread_waiter_init(&waiter);
    mutex_init(&lock);

    thread_create("waiter", 8192, wait_something, NULL);
    thread_create("wakeup", 8192, wakeup_something, NULL);
}