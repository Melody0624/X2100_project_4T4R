#include <common.h>
#include <soc/base.h>
#include <string.h>

#include  "dma_regs.h"

struct dma_desc {
    unsigned long dcm; /* dma channel command */
    unsigned long dsa; /* Source Address */
    unsigned long dta; /* Target Address */
    unsigned long dtc; /* Descriptor Offset address, Transfer Counter */
    unsigned long sd;  /* Target Stride Address, Source Stride Address */
    unsigned long drt; /* DMA Request Type */
    unsigned long reserved[2];
};

#define DMA_CHANNELS     32
#define DMA_ADDR(reg)    (io_addr(PDMA_IOBASE + reg))

static inline void dma_write(unsigned int reg, int val)
{
    *DMA_ADDR(reg) = val;
}

static inline unsigned int dma_read(unsigned int reg)
{
    return *DMA_ADDR(reg);
}

static inline void dma_set_bit(unsigned int reg, int start, int end, unsigned int val)
{
    set_bit_field_v(DMA_ADDR(reg), start, end, val);
}

static inline unsigned int dma_get_bit(unsigned int reg, int start, int end)
{
    return get_bit_field_v(DMA_ADDR(reg), start, end);
}

static void hal_dma_global_enable(void)
{
    unsigned long dmac = dma_read(DMAC);
    set_bit_field(&dmac, DMAC_DMAE, 1);
    set_bit_field(&dmac, DMAC_HLT, 0);
    set_bit_field(&dmac, DMAC_AR, 0);
    dma_write(DMAC, dmac);
}

static void hal_dma_global_disable(void)
{
    dma_set_bit(DMAC, DMAC_DMAE, 0);
}

#include <driver/clk.h>
#include <spinlock.h>
#include <driver/irq.h>
#include <driver/cache.h>
#include <driver/dma.h>
#include <stdlib.h>
#include <malloc.h>

struct dma {
    enum DMA_request_type rq;
    enum DMA_bus_width bus_width;
    unsigned int unit_size;
    void (*cb)(void *data);
    void *data;
    int is_started;
    unsigned int ch;
    struct dma_desc *desc;
    unsigned long desc_count;
    unsigned long src_start;
    unsigned long dst_start;
    unsigned long src_end;
    unsigned long dst_end;
};

static struct dma *datas[DMA_CHANNELS];
static unsigned long is_enabled;

static struct clk *dma_clk;
static DEFINE_SPINLOCK(lock);

static int is_init = 0;

static inline int get_dma_channel(void)
{
    int i;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    for (i = 0; i < DMA_CHANNELS; i++) {
        if (!(is_enabled & (1 << i))) {
            /*
            * 确保dma是使能的
            */
            if (!is_enabled && !is_init) {
                is_init = 1;
                clk_enable(dma_clk);
                hal_dma_global_enable();
            }
            is_enabled |= (1 << i);
            break;
        }
    }

    spin_unlock_irqrestore(&lock, flags);

    return i < DMA_CHANNELS ? i : -1;
}

static inline void put_dma_channel(unsigned int ch)
{
    if (is_enabled)
        dma_write(DCS(ch), 0);

    if (is_enabled & (1 << ch)) {
        is_enabled &= ~(1 << ch);
        /*
        * 不动态关闭dma了,比较耗时
        */
        if (!is_enabled && 0) {
            hal_dma_global_disable();
            clk_disable(dma_clk);
        }
    }
}

static inline int src_is_mem(enum DMA_request_type rq)
{
    return (rq == DMA_RQ_MEM) || ((rq != DMA_RQ_SADC_RX) && !(rq & 0x1));
}

static inline int dst_is_mem(enum DMA_request_type rq)
{
    return (rq == DMA_RQ_MEM) || (rq & 0x1) || (rq == DMA_RQ_SADC_RX);
}

static void init_dma_desc(struct dma_desc *desc,
        void *src, void *dst, unsigned int len,
        enum DMA_request_type rq,
        enum DMA_bus_width bus_width, unsigned int unit_size)
{
    unsigned int width = 0;
    if (bus_width == DMA_bus_32bit)
        width = 0;
    else if (bus_width == DMA_bus_16bit)
        width = 2;
    else if (bus_width == DMA_bus_8bit)
        width = 1;
    else
        panic("invalid bus_width: %d\n", bus_width);

    unsigned int tsz = 0;
    if (unit_size == 1)
        tsz = 1;
    else if (unit_size == 2)
        tsz = 2;
    else if (unit_size == 4)
        tsz = 0;
    else if (unit_size == 8) {
        tsz = 0;
        unit_size = 4;
    } else if (unit_size == 16)
        tsz = 3;
    else if (unit_size == 32)
        tsz = 4;
    else if (unit_size == 64)
        tsz = 5;
    else if (unit_size >= 128) {
        tsz = 6;
        unit_size = 128;
    } else
        panic("invalid unit_size: %d\n", unit_size);

    assert((len / unit_size) < (16 * 1024 * 1024));

    unsigned long dcm = 0;
    /* 如果src/dst是mem, 那么地址自增
     * 如果src/dst是device, 那么地址不自增
     */
    set_bit_field(&dcm, DCM_SAI, src_is_mem(rq));
    set_bit_field(&dcm, DCM_DAI, dst_is_mem(rq));
    set_bit_field(&dcm, DCM_SP, width);
    set_bit_field(&dcm, DCM_DP, width);
    set_bit_field(&dcm, DCM_STDE, 0); /* 没有 stride */
    set_bit_field(&dcm, DCM_TSZ, tsz); /* 设置unit size */
    set_bit_field(&dcm, DCM_TIE, 0); /* 暂不开中断 */
    set_bit_field(&dcm, DCM_LINK, 0); /* 暂不设置link */

    desc->dsa = virt_to_phys(src);
    desc->dta = virt_to_phys(dst);
    desc->dtc = 0;
    set_bit_field(&desc->dtc, DTC_DOA, (virt_to_phys(&desc[1]) >> 4) & 0xff);
    set_bit_field(&desc->dtc, DTC_DTC, len / unit_size);
    desc->sd = 0; /* 没有 stride */
    desc->drt = rq;
    desc->dcm = dcm;
}

static inline unsigned long read_src_addr(struct dma *dma)
{
    unsigned long addr;

    while (1) {
        addr = dma_read(DSA(dma->ch));
        if (addr >= dma->src_start && addr <= dma->src_end)
            return addr;
    }
}

static inline unsigned long read_dst_addr(struct dma *dma)
{
    unsigned long addr;

    while (1) {
        addr = dma_read(DTA(dma->ch));
        if (addr >= dma->dst_start && addr <= dma->dst_end)
            return addr;
    }
}

unsigned long soc_dma_read_src_addr(struct dma *dma)
{
    unsigned long flags;
    unsigned long addr;

    spin_lock_irqsave(&lock, flags);

    if (dma->is_started == -1)
        addr = 0;
    else if (dma->is_started == 1)
        addr = read_src_addr(dma);
    else
        addr = dma->src_end;

    spin_unlock_irqrestore(&lock, flags);

    return addr;
}

unsigned long soc_dma_read_dst_addr(struct dma *dma)
{
    unsigned long flags;
    unsigned long addr;

    spin_lock_irqsave(&lock, flags);

    if (dma->is_started == -1)
        addr = 0;
    else if (dma->is_started == 1)
        addr = read_dst_addr(dma);
    else
        addr = dma->dst_end;

    spin_unlock_irqrestore(&lock, flags);

    return addr;
}

struct dma *soc_dma_request(
        enum DMA_request_type rq, void (*cb)(void *data), void *data,
        enum DMA_bus_width bus_width, unsigned int unit_size)
{
    struct dma *dma = malloc(sizeof(*dma));

    dma->bus_width = bus_width;
    dma->rq = rq;
    dma->cb = cb;
    dma->data = data;
    dma->unit_size = unit_size;
    dma->is_started = -1;
    dma->desc = NULL;

    return dma;
}

void soc_dma_release(struct dma *dma)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    assert(dma->is_started != 1);

    spin_unlock_irqrestore(&lock, flags);

    if (dma->desc)
        free(dma->desc);
    free(dma);
}

void soc_dma_start(struct dma *dma,
        void *src, void *dst, unsigned int len)
{
    struct dma_desc *desc;
    enum DMA_request_type rq = dma->rq;
    enum DMA_bus_width bus_width = dma->bus_width;
    unsigned int unit_size = dma->unit_size;

    assert(len);
    assert(unit_size);
    assert(len >= unit_size);
    assert(unit_size >= bus_width);
    assert(!(len % unit_size));
    assert(!(unit_size % bus_width));
    assert(dma->is_started != 1);

    /* 开启全局dma, 并且获得一个通道
     */
    int ch = get_dma_channel();
    assert(ch >= 0);

    if (!dma->desc) {
        desc = cache_align_malloc(sizeof(*desc));
        dma->desc = desc;
        dma->desc_count = 1;
    } else {
        desc = dma->desc;
    }

    dma->ch = ch;
    dma->is_started = 1;
    dma->src_start = virt_to_phys(src);
    dma->dst_start = virt_to_phys(dst);
    dma->src_end = dma->src_start + (src_is_mem(rq) ? len : 0);
    dma->dst_end = dma->dst_start + (dst_is_mem(rq) ? len : 0);
    datas[ch] = dma;

    /* 初始化 dma desc
     */
    init_dma_desc(desc, src, dst, len, rq, bus_width, unit_size);
    set_bit_field(&desc->dcm, DCM_TIE, 1); /* 开启中断 */

    flush_dcache((unsigned long)desc, ALIGN(sizeof(*desc), cache_line_size()));

    /* 设置desc 地址
     */
    dma_write(DDA(ch), virt_to_phys(desc));

    /* 载入desc到寄存器
     */
    dma_write(DDS, 1 << ch);

    /* 使用 8 word descriptor,
     * 清 AR, HLT, 开启传输
     */
    unsigned long dcs = 0;
    set_bit_field(&dcs, DCS_NDES, 0);
    set_bit_field(&dcs, DCS_DES8, 1);
    set_bit_field(&dcs, DCS_CTE, 1);
    dma_write(DCS(ch), dcs);

    // 这里就不读了,影响单次dma启动时间
    // soc_dma_read_src_addr(dma);
    // soc_dma_read_dst_addr(dma);
}

static void dma_start_linked_inner(struct dma *dma,
        void *src, void *dst, unsigned int len, unsigned int count, int cyclic)
{
    enum DMA_request_type rq = dma->rq;
    enum DMA_bus_width bus_width = dma->bus_width;
    unsigned int unit_size = dma->unit_size;
    unsigned int len2 = len / count;

    assert(len);
    assert(unit_size);
    assert(!(len % count));
    assert(len2 >= unit_size);
    assert(unit_size >= bus_width);
    assert(!(len2 % unit_size));
    assert(!(unit_size % bus_width));
    assert(dma->is_started != 1);
    assert(count <= 128);

    /* 开启全局dma, 并且获得一个通道
     */
    int ch = get_dma_channel();
    assert(ch >= 0);

    struct dma_desc *desc;

    if (!dma->desc || dma->desc_count < count) {
        if (dma->desc)
            free(dma->desc);

        unsigned int align = 1;

        while (align < count) {
            align = align * 2;
        }

        align = align * sizeof(*desc);

        /*
         * 链式的dma是用offset来描述下一个描述符的地址的
         * 必须做到dma0描述符数组不能跨页
         **/
        desc = memalign(ALIGN(align, cache_line_size()), ALIGN(count * sizeof(*desc), cache_line_size()));
        dma->desc = desc;
        dma->desc_count = count;
    } else {
        desc = dma->desc;
    }

    dma->ch = ch;
    dma->is_started = 1;
    dma->src_start = virt_to_phys(src);
    dma->dst_start = virt_to_phys(dst);
    dma->src_end = dma->src_start + (src_is_mem(rq) ? len : 0);
    dma->dst_end = dma->dst_start + (dst_is_mem(rq) ? len : 0);
    datas[ch] = dma;

    int i;
    for (i = 0; i < count; i++) {
        /* 初始化 dma desc
        */
        init_dma_desc(&desc[i], src, dst, len2, rq, bus_width, unit_size);
        set_bit_field(&desc[i].dcm, DCM_LINK, 1); /* 开启LINK */
        if (src_is_mem(rq))
            src += len2;
        if (dst_is_mem(rq))
            dst += len2;
    }

    i = count -1;
    if (cyclic) {
        /* 设置循环dma link */
        set_bit_field(&desc[i].dtc, DTC_DOA, (virt_to_phys(desc) >> 4) & 0xff);
        set_bit_field(&desc[i].dcm, DCM_TIE, 1); /* 开启中断 */
    } else {
        set_bit_field(&desc[i].dcm, DCM_TIE, 1); /* 开启中断 */
        set_bit_field(&desc[i].dcm, DCM_LINK, 0); /* 关闭LINK */
    }

    flush_dcache((unsigned long)desc, ALIGN(count * sizeof(*desc), cache_line_size()));

    /* 设置desc 地址
     */
    dma_write(DDA(ch), virt_to_phys(desc));

    /* 载入desc到寄存器
     */
    dma_write(DDS, 1 << ch);

    /* 使用 8 word descriptor,
     * 清 AR, HLT, 开启传输
     */
    unsigned long dcs = 0;
    set_bit_field(&dcs, DCS_NDES, 0);
    set_bit_field(&dcs, DCS_DES8, 1);
    set_bit_field(&dcs, DCS_CTE, 1);
    dma_write(DCS(ch), dcs);

    soc_dma_read_src_addr(dma);//先读一次无效值，使得 uart dma 模式读的值有效
    soc_dma_read_dst_addr(dma);
}

void soc_dma_start_linked(struct dma *dma,
        void *src, void *dst, unsigned int len, unsigned int count)
{
    dma_start_linked_inner(dma, src, dst, len, count, 0);
}

void soc_dma_start_cyclic(struct dma *dma,
        void *src, void *dst, unsigned int len, unsigned int count)
{
    dma_start_linked_inner(dma, src, dst, len, count, 1);
}

void soc_dma_stop(struct dma *dma)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    if (dma->is_started == 1) {
        dma->src_end = read_src_addr(dma);
        dma->dst_end = read_dst_addr(dma);
        put_dma_channel(dma->ch);
        dma->is_started = 0;
    }

    spin_unlock_irqrestore(&lock, flags);
}

void soc_dma_irq_handler(int irq, void *data)
{
    int i;
    unsigned long pending;

    pending = dma_read(DIRQP);

    while (1) {
        i = ffs(pending);
        if (i == 0)
            break;
        i = i - 1;

        set_bit_field(&pending, i, i, 0);

        unsigned long dcs = dma_read(DCS(i));

        if (get_bit_field(&dcs, DCS_AR))
            printf("warning: dma address error, %lx\n", dcs);

        if (get_bit_field(&dcs, DCS_HLT))
            printf("warning: dma Halt, %lx\n", dcs);

        struct dma *dma = datas[i];
        dma->is_started = 0;

        put_dma_channel(i);

        if (datas[i] && datas[i]->cb)
            datas[i]->cb(datas[i]->data);
    }
}

void soc_dma_init(void)
{
    assert(!dma_clk);
    dma_clk = clk_get("gate_pdma");
    assert(dma_clk);

    request_irq(IRQ_PDMA, 0, soc_dma_irq_handler, "dma", NULL);
}
