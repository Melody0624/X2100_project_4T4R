#include <common.h>
#include <os.h>
#include <driver/conn.h>

#define CUR_CPU_ID 1

static void conn_test_thread(void *data)
{
    int ret;
    char buf[128];
    char *s = (char *)data;

    struct conn_node *conn = conn_request(s, 256);
    if (conn == NULL)
        printf("CPU%d conn request %s ok.\n", CUR_CPU_ID, s);

    ret = conn_write(conn, s, strlen(s) + 1, 3000);
    if (ret > 0)
        printf("CPU%d len:%d, send:%s\n", CUR_CPU_ID, ret, s);

    while (1) {
        ret = conn_read(conn, buf, strlen(s) + 1, 3000);
        if (ret > 0) {
            printf("CPU%d len:%d, recv:%s\n", CUR_CPU_ID, ret, buf);

            ret = conn_write(conn, s, strlen(s) + 1, 3000);
            printf("CPU%d len:%d, send:%s\n", CUR_CPU_ID, ret, s);
        }
        usleep(100);
    }

    conn_release(conn);
}

void conn_test(void)
{
    thread_create("conn test thread", 4096, conn_test_thread, "conn_1");

    thread_create("conn test thread", 4096, conn_test_thread, "conn_2");
}