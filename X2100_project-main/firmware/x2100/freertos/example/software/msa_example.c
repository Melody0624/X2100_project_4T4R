#include <msa.h>
#include <stdio.h>

/*
 * 这里加 volatile 是为了避免编译器直接在编译时计算出来,但不到运行时测试的效果
 */

static volatile v16i8 value0 = {0, 1, 2, 3};
static volatile v16i8 value1 = {0, 1, 2, 3};
static volatile v16i8 value;

void mas_test(void *data)
{
    value = __msa_addv_b(value0, value1);
    unsigned char *buf = (void *) &value;

    printf("TTTTT: %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3]);
}

