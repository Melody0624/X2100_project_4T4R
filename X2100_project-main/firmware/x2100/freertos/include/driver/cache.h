#ifndef _CACHE_H_
#define _CACHE_H_

#include <cpu/cache.h>
#include <malloc.h>

/*
 * Descriptor for a cache
 */
struct cache_desc {
    unsigned int size;      /* total size of this cache */
    unsigned int waysize;   /* Bytes per way */
    unsigned short sets;    /* Number of lines per set */
    unsigned char ways;     /* Number of ways */
    unsigned char linesz;   /* Size of line in bytes */
    unsigned char waybit;   /* Bits to select in a cache set */
    unsigned char flags;    /* Flags describing cache properties */
};

extern struct cache_desc cpu_dcache;
extern struct cache_desc cpu_icache;
extern struct cache_desc cpu_scache;

/**
 * 关键词解释:
 * dcache : 数据 cache, 可以写回或作废
 * icache : 指令 cache, 只可以作废
 * flush :
 *   对 icache 作废
 *   对 dcache 写回并作废
 * invalidate : 作废 icache or dcahce
 */

/**
 * 初始化 cache
 */
void cache_init(void);

/**
 * 返回cache line 的大小,方便用户做cache line 对齐
 */
extern unsigned long cache_line_size(void);

/**
 * Flush all instruction caches
 */
extern void flush_icache_all(void);

/**
 * Flush all data caches
 */
extern void flush_dcache_all(void);

/**
 * Flush all data and instruction caches
 */
extern void flush_cache_all(void);

/**
 * Flush the specified data and instruction caches
 *
 * @param start_addr   specified start addres
 * @param size         pecified length
 */
extern void flush_cache(unsigned long start_addr, unsigned long size);

/**
 * Flush the specified data caches
 *
 * @param start_addr   specified start addres
 * @param size         specified length
 */
extern void flush_dcache(unsigned long start_addr, unsigned long size);

/**
 * Flush the specified data caches
 *
 * @param start_addr   specified start addres
 * @param size         specified length
 * @note you can use the unaligned start_addr or size,
 * the start_addr and size will be fixed to cache line size align,
 * it may cause some data bug!
 */
void flush_dcache_force(unsigned long start_addr, unsigned long size);

/**
 * Flush the specified data caches in writeback_invalite way
 *
 * @param start        specified start addres
 * @param stop         specified length
 */
extern void flush_dcache_range(unsigned long start, unsigned long stop);

/**
 * Flush the specified instruction caches
 *
 * @param start_addr   specified start addres
 * @param size         specified length
 */
extern void flush_icache(unsigned long start_addr, unsigned long size);

/**
 * Flush the specified instruction caches in invalite way
 *
 * @param start        specified start addres
 * @param stop         specified length
 */
extern void flush_icache_range(unsigned long start, unsigned long stop);

/**
 * Flush the specified data caches in invalite way
 *
 * @param start        specified start addres
 * @param stop         specified length
 */
extern void invalidate_dcache(unsigned long start, unsigned long size);

/**
 * Flush the specified data caches in invalite way
 *
 * @param start        specified start addres
 * @param stop         specified length
 * @note you can use the unaligned start_addr or size,
 * the start_addr and size will be fixed to cache line size align,
 * it may cause some data bug!
 */
extern void invalidate_dcache_force(unsigned long start, unsigned long size);

/**
 * Flush the specified data caches in invalite way
 *
 * @param start        specified start addres
 * @param stop         specified length
 */
extern void invalidate_dcache_range(unsigned long start, unsigned long stop);

/**
 * malloc memory with cache_line_size aligned, both start and size
 *
 * @param size        how many bytes you want to allocate
 * @return NULL if failed, other is ok
 */
void *cache_align_malloc(unsigned int size);

#endif /*  */