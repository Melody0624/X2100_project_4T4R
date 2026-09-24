#include "common.h"
#include <os.h>



/*
 * thread create
 */
static void thread_create_test1(void *data)
{
    int count = 0;

    printf("data is %d\n", (int)data);

    while (1) {
        count++;
        msleep(200);
        printf("%s running count %u\r\n", __func__, count);
    }

}

static void thread_create_test2(void *data)
{
    (void) data;
    int count = 0;

    while (count < 10) {
        count++;
        msleep(500);
        printf("%s running count %u\r\n", __func__, count);
    }

    printf("now quit from thread 2\n");

    /*
     * thread_delete(NULL);
     * NULL 表示删除当前线程
     * thread 执行函数返回时会自动调用 thread_delete 函数进行销毁
     */
}


void thread_create_test(void)
{

    thread_create("thread create1", 8192, thread_create_test1, (void *)1);
    thread_create("thread create2", 8192, thread_create_test2, NULL);

}
