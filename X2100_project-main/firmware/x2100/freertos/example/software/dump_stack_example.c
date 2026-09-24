#include <common.h>

void test_dump_stack(void)
{
    dump_stack();

#if 0
    printf("This will be a exception\n");
    *(volatile unsigned int *)0 = 3;
#else
    panic("This will be a exception\n");
#endif
}