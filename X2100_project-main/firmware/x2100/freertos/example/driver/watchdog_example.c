#include <common.h>
#include <driver/watchdog.h>

void wdt_test0(void *data)
{
    /* 启动看门狗，同时设置最迟喂狗时间 1秒 */
    wdt_start(1000);

    /* 每 500ms 喂狗一次 */
    while (1) {
        mdelay(500);
        wdt_feed();
    }
}

void wdt_test1(void *data)
{
    /* 10秒后强制重启 */
    mdelay(10000);
    reset();
}

void test_wdt(void)
{
    thread_create("wdt_test0", 2048, wdt_test0, NULL);

    thread_create("wdt_test1", 2048, wdt_test1, NULL);
}