#include <common.h>
#include <asm/mipsregs.h>
#include <asm/addrspace.h>
#include <asm/cacheops.h>
#include <driver/cache.h>
#include "__ffs.h"

struct cache_desc cpu_dcache;
struct cache_desc cpu_icache;
struct cache_desc cpu_scache;

#define CONFIG_L2_CACHE_NOT_WRITE_THROUGH

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
#define CONFIG_L2_CACHE_MUST_ALIGNED 1
#endif

#ifdef CONFIG_L2_CACHE_MUST_ALIGNED
#define check_addr_align() \
    assert(!(start_addr % cpu_scache.linesz)); \
    assert(!(size % cpu_scache.linesz));
#else
#define check_addr_align() \
    assert(!(start_addr % lsize)); \
    assert(!(size % lsize));
#endif

unsigned long arch_cache_line_size(void)
{
#ifdef CONFIG_L2_CACHE_MUST_ALIGNED
    return cpu_scache.linesz > cpu_dcache.linesz ? cpu_scache.linesz : cpu_dcache.linesz;
#else
    return cpu_dcache.linesz;
#endif
}

void arch_flush_cache(unsigned long start_addr, unsigned long size)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = start_addr + size;

    check_addr_align();

    for (; addr < aend; addr += lsize) {
        cache_op(HIT_WRITEBACK_INV_D, addr);
        cache_op(HIT_INVALIDATE_I, addr);
    }

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    unsigned long ssize = cpu_scache.linesz;
    addr = start_addr;
    for (; addr < aend; addr += ssize)
        cache_op(HIT_WRITEBACK_INV_SD, addr);
#endif

    unsigned int t = 0;
    /* invalidate btb */
    __asm__ __volatile__(
        "mfc0 %0, $16, 7\n\t"
        "nop\n\t"
        "ori %0,2\n\t"
        "mtc0 %0, $16, 7\n\t"
        :
        : "r" (t));
}

void arch_flush_dcache(unsigned long start_addr, unsigned long size)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = start_addr + size;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_WRITEBACK_INV_D, addr);

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    unsigned long ssize = cpu_scache.linesz;
    addr = start_addr;
    for (; addr < aend; addr += ssize)
        cache_op(HIT_WRITEBACK_INV_SD, addr);
#endif

    fast_iob();
}

void arch_flush_l1_dcache(unsigned long start_addr, unsigned long size)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = start_addr + size;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_WRITEBACK_INV_D, addr);

    fast_iob();
}

void arch_flush_l1_dcache_range(unsigned long start_addr, unsigned long stop)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = stop;
    unsigned long size = stop - start_addr;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_WRITEBACK_INV_D, addr);

    fast_iob();
}

void arch_flush_dcache_range(unsigned long start_addr, unsigned long stop)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = stop;
    unsigned long size = stop - start_addr;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_WRITEBACK_INV_D, addr);

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    unsigned long ssize = cpu_scache.linesz;
    addr = start_addr;
    for (; addr < aend; addr += ssize)
        cache_op(HIT_WRITEBACK_INV_SD, addr);
#endif

    fast_iob();
}

void arch_invalidate_dcache(unsigned long start_addr, unsigned long size)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = start_addr + size;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_INVALIDATE_D, addr);

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    unsigned long ssize = cpu_scache.linesz;
    addr = start_addr;
    for (; addr < aend; addr += ssize)
        cache_op(HIT_INVALIDATE_SD, addr);
#endif

    fast_iob();
}

void arch_invalidate_dcache_range(unsigned long start_addr, unsigned long stop)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = stop;
    unsigned long size = stop - start_addr;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_INVALIDATE_D, addr);

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    unsigned long ssize = cpu_scache.linesz;
    addr = start_addr;
    for (; addr < aend; addr += ssize)
        cache_op(HIT_INVALIDATE_SD, addr);
#endif
}

void arch_flush_icache(unsigned long start_addr, unsigned long size)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = start_addr + size;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_INVALIDATE_I, addr);
}

void arch_flush_icache_range(unsigned long start_addr, unsigned long stop)
{
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long addr = start_addr;
    unsigned long aend = stop;
    unsigned long size = stop - start_addr;

    check_addr_align();

    for (; addr < aend; addr += lsize)
        cache_op(HIT_INVALIDATE_I, addr);
}

void arch_flush_icache_all(void)
{
    unsigned int addr, t = 0;
    unsigned long lsize = cpu_icache.linesz;
    unsigned long cache_size = cpu_icache.size;

    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += lsize) {
        cache_op(INDEX_INVALIDATE_I, addr);
    }

    /* invalidate btb */
    __asm__ __volatile__(
        "mfc0 %0, $16, 7\n\t"
        "nop\n\t"
        "ori %0,2\n\t"
        "mtc0 %0, $16, 7\n\t"
        :
        : "r" (t));
}

void arch_flush_l1_dcache_all(void)
{
    unsigned int addr;
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long cache_size = cpu_dcache.size;

    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += lsize) {
        cache_op(INDEX_WRITEBACK_INV_D, addr);
    }

    fast_iob();
}

void arch_flush_dcache_all(void)
{
    unsigned int addr;
    unsigned long lsize = cpu_dcache.linesz;
    unsigned long cache_size = cpu_dcache.size;

    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += lsize) {
        cache_op(INDEX_WRITEBACK_INV_D, addr);
    }

#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    unsigned long ssize = cpu_scache.linesz;
    cache_size = cpu_scache.size;
    for (addr = CKSEG0; addr < CKSEG0 + cache_size;
         addr += ssize) {
        cache_op(INDEX_WRITEBACK_INV_SD, addr);
    }
#endif

    fast_iob();
}

void arch_flush_cache_all(void)
{
    flush_dcache_all();
    flush_icache_all();
}

static void cache_size_init(void)
{
    unsigned int config1 = read_c0_config1();
    unsigned int config2 = read_c0_config2();
    unsigned int tmp, lsize;

    if ((lsize = ((config1 >> 19) & 7)))
        cpu_icache.linesz = 2 << lsize;
    else
        cpu_icache.linesz = lsize;
    cpu_icache.sets = 32 << (((config1 >> 22) + 1) & 7);
    cpu_icache.ways = 1 + ((config1 >> 16) & 7);

    cpu_icache.size = cpu_icache.sets * cpu_icache.ways * cpu_icache.linesz;
    cpu_icache.waybit = __ffs(cpu_icache.size/cpu_icache.ways);

    if ((lsize = ((config1 >> 10) & 7)))
        cpu_dcache.linesz = 2 << lsize;
    else
        cpu_dcache.linesz= lsize;
    cpu_dcache.sets = 32 << (((config1 >> 13) + 1) & 7);
    cpu_dcache.ways = 1 + ((config1 >> 7) & 7);

    cpu_dcache.size = cpu_dcache.sets * cpu_dcache.ways * cpu_dcache.linesz;
    cpu_dcache.waybit = __ffs(cpu_dcache.size/cpu_dcache.ways);

    tmp = (config2 >> 4) & 0x0f;
    if (0 < tmp && tmp <= 7)
        cpu_scache.linesz = 2 << tmp;
    else
        assert(0);

    tmp = (config2 >> 8) & 0x0f;
    if (0 <= tmp && tmp <= 7)
        cpu_scache.sets = 64 << tmp;
    else
        assert(0);

    tmp = (config2 >> 0) & 0x0f;
    if (0 <= tmp && tmp <= 15)
        cpu_scache.ways = tmp + 1;
    else
        assert(0);

    cpu_scache.waysize = cpu_scache.sets * cpu_scache.linesz;
    cpu_scache.waybit = __ffs(cpu_scache.waysize);
    cpu_scache.size = cpu_scache.ways * cpu_scache.sets * cpu_scache.linesz;

    debug("icache: %d %d\n", cpu_icache.size, cpu_icache.linesz);
    debug("dcache: %d %d\n", cpu_dcache.size, cpu_dcache.linesz);
    debug("scache: %d %d\n", cpu_scache.size, cpu_scache.linesz);

    /*
     * 当前的代码需要满足如下条件才能保证运行
     */
    assert(cpu_dcache.linesz == cpu_icache.linesz);
    assert(cpu_dcache.size == cpu_icache.size);
    assert(!(cpu_scache.linesz % cpu_dcache.linesz));
}

void arch_cache_init(void)
{
    cache_size_init();
#ifdef CONFIG_L2_CACHE_NOT_WRITE_THROUGH
    write_c0_ecc(read_c0_ecc() & ~ECCF_WST);
#else
    write_c0_ecc(read_c0_ecc() | ECCF_WST);
#endif
}
