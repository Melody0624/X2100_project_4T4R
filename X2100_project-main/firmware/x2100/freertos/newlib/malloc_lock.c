
#include <os.h>

/*
 * malloc 的锁,可以在中断中进行malloc
 */
void __malloc_lock(void)
{
    os_enter_critical();
}

void __malloc_unlock(void)
{
    os_exit_critical();
}