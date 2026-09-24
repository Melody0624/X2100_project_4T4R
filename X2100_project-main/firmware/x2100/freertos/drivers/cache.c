#include <driver/cache.h>
#include <common.h>
#include <malloc.h>

/*
 * arch 需要实现
 */
void arch_cache_init(void);

unsigned long arch_cache_line_size(void);

void arch_flush_icache_all(void);

void arch_flush_dcache_all(void);

void arch_flush_cache_all(void);

void arch_flush_cache(unsigned long start_addr, unsigned long size);

void arch_flush_dcache(unsigned long start_addr, unsigned long size);

void arch_flush_dcache_range(unsigned long start, unsigned long stop);

void arch_flush_icache(unsigned long start_addr, unsigned long size);

void arch_flush_icache_range(unsigned long start, unsigned long stop);

void arch_invalidate_dcache(unsigned long start, unsigned long size);

void arch_invalidate_dcache_range(unsigned long start, unsigned long stop);


void cache_init(void)
{
    arch_cache_init();
}

unsigned long cache_line_size(void)
{
    return arch_cache_line_size();
}

void flush_icache_all(void)
{
    arch_flush_icache_all();
}

void flush_dcache_all(void)
{
    arch_flush_dcache_all();
}

void flush_cache_all(void)
{
    arch_flush_cache_all();
}

void flush_cache(unsigned long start_addr, unsigned long size)
{
    arch_flush_cache(start_addr, size);
}

void flush_dcache(unsigned long start_addr, unsigned long size)
{
    arch_flush_dcache(start_addr, size);
}

void flush_dcache_range(unsigned long start, unsigned long stop)
{
    arch_flush_dcache_range(start, stop);
}

void flush_icache(unsigned long start_addr, unsigned long size)
{
    arch_flush_icache(start_addr, size);
}

void flush_icache_range(unsigned long start, unsigned long stop)
{
    arch_flush_icache_range(start, stop);
}

void invalidate_dcache(unsigned long start, unsigned long size)
{
    arch_invalidate_dcache(start, size);
}

void invalidate_dcache_range(unsigned long start, unsigned long stop)
{
    arch_invalidate_dcache_range(start, stop);
}

void flush_dcache_force(unsigned long start_addr, unsigned long size)
{
    if (size == 0)
        return;

    unsigned int lsize = cache_line_size();
    unsigned int delta = start_addr % lsize;
    unsigned long start = start_addr - delta;

    if (delta + size <= lsize)
        flush_dcache(start, lsize);
    else
        flush_dcache(start, ALIGN(delta + size, lsize));
}

void invalidate_dcache_force(unsigned long start_addr, unsigned long size)
{
    if (size == 0)
        return;

    unsigned int lsize = cache_line_size();
    unsigned int delta = start_addr % lsize;
    unsigned long start = start_addr - delta;

    if (delta + size <= lsize)
        invalidate_dcache(start, lsize);
    else
        invalidate_dcache(start, ALIGN(delta + size, lsize));
}

void *cache_align_malloc(unsigned int size)
{
    return memalign(arch_cache_line_size(), ALIGN(size, arch_cache_line_size()));
}

#include <kernel_symbol.h>

EXPORT_SYMBOL(cache_line_size);
EXPORT_SYMBOL(flush_icache_all);
EXPORT_SYMBOL(flush_dcache_all);
EXPORT_SYMBOL(flush_cache_all);
EXPORT_SYMBOL(flush_cache);
EXPORT_SYMBOL(flush_dcache);
EXPORT_SYMBOL(flush_dcache_range);
EXPORT_SYMBOL(flush_icache);
EXPORT_SYMBOL(flush_icache_range);
EXPORT_SYMBOL(invalidate_dcache);
EXPORT_SYMBOL(invalidate_dcache_range);
EXPORT_SYMBOL(flush_dcache_force);
EXPORT_SYMBOL(invalidate_dcache_force);
EXPORT_SYMBOL(cache_align_malloc);