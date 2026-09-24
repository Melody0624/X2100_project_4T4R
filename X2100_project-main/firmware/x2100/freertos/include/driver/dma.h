#ifndef _DMA_H_
#define _DMA_H_

#include <soc/dma.h>

/**
 * soc 实现 enum DMA_bus_width
 *     DMA_RQ_MEM, // SOC 必须实现
 */
enum DMA_request_type;

enum DMA_bus_width {
    DMA_bus_8bit = 1,
    DMA_bus_16bit = 2,
    DMA_bus_32bit = 4,
    DMA_bus_64bit = 8,
};

struct dma;

unsigned long dma_read_src_addr(struct dma *dma);

unsigned long dma_read_dst_addr(struct dma *dma);

struct dma *dma_request(
    enum DMA_request_type rq, void (*cb)(void *data), void *data,
    enum DMA_bus_width bus_width, unsigned int unit_size);

void dma_release(struct dma *dma);

void dma_start(struct dma *dma,
    void *src, void *dst, unsigned int len
    );

void dma_start_linked(struct dma *dma,
    void *src, void *dst, unsigned int len, unsigned int count
    );

void dma_start_cyclic(struct dma *dma,
    void *src, void *dst, unsigned int len, unsigned int count
    );

void dma_stop(struct dma *dma);

void dma_init(void);

#endif