#include <os.h>
#include <sched.h>

int sched_get_priority_max(int policy)
{
    (void) policy;

    return  OS_priority_realtime;
}

int sched_get_priority_min(int policy)
{
    (void) policy;

    return OS_priority_idle;
}

int sched_yield(void)
{
    thread_yield();
    return 0;
}