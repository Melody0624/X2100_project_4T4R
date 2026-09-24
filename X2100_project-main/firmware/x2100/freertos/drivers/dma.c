#include <driver/dma.h>

/*
 * soc 需要实现
 */
unsigned long soc_dma_read_src_addr(struct dma *dma);

unsigned long soc_dma_read_dst_addr(struct dma *dma);

struct dma *soc_dma_request(
    enum DMA_request_type rq, void (*cb)(void *data), void *data,
    enum DMA_bus_width bus_width, unsigned int unit_size);

void soc_dma_release(struct dma *dma);

void soc_dma_start(struct dma *dma,
    void *src, void *dst, unsigned int len);

void soc_dma_start_linked(struct dma *dma,
    void *src, void *dst, unsigned int len, unsigned int count);

void soc_dma_start_cyclic(struct dma *dma,
    void *src, void *dst, unsigned int len, unsigned int count);

void soc_dma_stop(struct dma *dma);

void soc_dma_init(void);

unsigned long dma_read_src_addr(struct dma *dma)
{
    return soc_dma_read_src_addr(dma);
}

unsigned long dma_read_dst_addr(struct dma *dma)
{
    return soc_dma_read_dst_addr(dma);
}

struct dma *dma_request(
    enum DMA_request_type rq, void (*cb)(void *data), void *data,
    enum DMA_bus_width bus_width, unsigned int unit_size)
{
    return soc_dma_request(rq, cb, data, bus_width, unit_size);
}

void dma_release(struct dma *dma)
{
    soc_dma_release(dma);
}

void dma_start(struct dma *dma,
    void *src, void *dst, unsigned int len)
{
    soc_dma_start(dma, src, dst, len);
}

void dma_start_linked(struct dma *dma,
    void *src, void *dst, unsigned int len, unsigned int count)
{
    soc_dma_start_linked(dma, src, dst, len, count);
}

void dma_start_cyclic(struct dma *dma,
    void *src, void *dst, unsigned int len, unsigned int count)
{
    soc_dma_start_cyclic(dma, src, dst, len, count);
}

void dma_stop(struct dma *dma)
{
    soc_dma_stop(dma);
}

void dma_init(void)
{
    soc_dma_init();
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(dma_read_src_addr);
EXPORT_SYMBOL(dma_read_dst_addr);
EXPORT_SYMBOL(dma_request);
EXPORT_SYMBOL(dma_release);
EXPORT_SYMBOL(dma_start);
EXPORT_SYMBOL(dma_start_linked);
EXPORT_SYMBOL(dma_start_cyclic);
EXPORT_SYMBOL(dma_stop);