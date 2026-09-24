#include <common.h>
#include <unistd.h>
#include <soc/watchdog1.h>

void wdt1_test_callback(void)
{
    printf("watchdog1: counter full irq callback\n");
}

void wdt1_test(void)
{
    watchdog1_set_callback(wdt1_test_callback);

    printf("wdt1 start func test\n");

    printf("fisrt start wdt1 5s\n");
    watchdog1_start(5*1000*1000);

    printf("sleep 7s\n");
    udelay(7*1000*1000);

    printf("wdt1 stop func test\n");

    printf("second start wdt1 5s\n");
    watchdog1_start(5*1000*1000);

    printf("sleep 3s\n");
    udelay(3*1000*1000);

    printf("second stop wdt1\n");
    watchdog1_stop();

    printf("sleep 4s\n");
    udelay(4*1000*1000);

    printf("finish wdt1 func test\n");
}

void test_wdt1(void)
{
    watchdog1_init();

    wdt1_test();

    watchdog1_deinit();
}