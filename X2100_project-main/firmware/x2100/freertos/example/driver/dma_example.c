#include <common.h>
#include <driver/dma.h>

/**
 * align 32 byte 主要是为了cache 操作
 */
__align(32) short test_src[20480];
__align(32) short test_dst[20480];

void dma_test_cb(void *data)
{
    printf("dma transfer end\n");
}

void dma_test(void)
{
    static int base = 0;
    int i;

    for (i = 0; i < 1024; i++)
        test_src[i] = i + base;

    base += 1;

    flush_dcache((unsigned long)test_src, sizeof(test_src));
    invalidate_dcache((unsigned long)test_dst, sizeof(test_src));

    struct dma *dma;
    dma = dma_request(DMA_RQ_MEM, dma_test_cb, NULL, DMA_bus_32bit, 8);

    /* 循环的 dma 传输 */
    // dma_start_cyclic(dma, test_src, test_dst, sizeof(test_src), 128);

    /* 多段dma 链式传输,用处不大,除非传输大小超过16M */
    // dma_start_linked(dma, test_src, test_dst, sizeof(test_src), 128);

    /* 普通的一段dma 传输 */
    dma_start(dma, test_src, test_dst, sizeof(test_src));

    udelay(100);

    printf("dma src: %08lx\n", dma_read_src_addr(dma));
    printf("dma dst: %08lx\n", dma_read_dst_addr(dma));

    dma_stop(dma);

    dump_mem32(test_dst + 900, 124 * 2, 8);

    printf("dma src: %08lx\n", dma_read_src_addr(dma));
    printf("dma dst: %08lx\n", dma_read_dst_addr(dma));

    dma_release(dma);
}
