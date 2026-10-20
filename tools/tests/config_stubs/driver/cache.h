#ifndef TEST_CACHE_H
#define TEST_CACHE_H
static inline void flush_dcache_all(void) {}
static inline unsigned int cache_line_size(void) { return 64; }
#endif
