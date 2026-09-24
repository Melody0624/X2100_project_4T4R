#include <common.h>
#include <driver/cache.h>

#define BUF_SIZE 64

static char __attribute__((aligned(64))) buf[BUF_SIZE];

static void cache_test(void)
{
    int i;
    char *kseg1 = (void *)KSEG1ADDR(buf);

    printf("test flush_dcache       : ");
    for (i = 0; i < BUF_SIZE; i++)
        buf[i] = i+0;

    flush_dcache((unsigned long)buf, BUF_SIZE);
    dump_mem32(kseg1, BUF_SIZE, 8);

    printf("test flush_dcache_range : ");
    for (i = 0; i < BUF_SIZE; i++)
        buf[i] = i+1;

    flush_dcache_range((unsigned long)buf, (unsigned long)buf + BUF_SIZE);
    dump_mem32(kseg1, BUF_SIZE, 8);

    printf("test flush_dcache_all   : ");
    for (i = 0; i < BUF_SIZE; i++)
        buf[i] = i+2;

    flush_dcache_all();
    dump_mem32(kseg1, BUF_SIZE, 8);

    printf("test invalidate_dcache  : ");
    for (i = 0; i < BUF_SIZE; i++)
        kseg1[i] = i+3;

    invalidate_dcache((unsigned long)buf, BUF_SIZE);
    dump_mem32(buf, BUF_SIZE, 8);
}
