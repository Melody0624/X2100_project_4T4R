#ifndef HEAP_MALLOC_H
#define HEAP_MALLOC_H

#include <stddef.h>
#include "os/freertos/include/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 替换标准库函数 */
void *heap_mem_malloc(size_t size);
void heap_mem_free(void *ptr);
void *heap_mem_calloc(size_t nmemb, size_t size);
void *heap_mem_realloc(void *ptr, size_t size);

/* 统计查询接口 */
size_t mem_get_total_allocated(void);      // 历史总分配字节数
size_t mem_get_current_allocated(void);    // 当前已分配字节数
size_t mem_get_peak_allocated(void);       // 历史峰值字节数
size_t mem_get_total_alloc_count(void);    // 历史总分配次数
size_t mem_get_valid_alloc_count(void);    // 当前有效分配次数
size_t mem_get_total_free_count(void);     // 历史总释放次数
size_t mem_get_total_freed_bytes(void);    // 历史总释放字节数

/* 系统堆统计（通过 linker 符号 + sbrk 获取实际堆使用） */
size_t mem_get_system_total(void);   /* 总堆大小 */
size_t mem_get_system_used(void);    /* 已使用堆大小 */
size_t mem_get_system_free(void);    /* 剩余堆大小 */
void   mem_print_system_heap(const char *tag);  /* 打印系统堆分配情况，tag 为可选的上下文标签 */

/* 打印统计摘要和所有分配详情（需要 printf 支持） */
void mem_print_stats(void);
void mem_print_all_allocations(void);

#define malloc heap_mem_malloc
#define free heap_mem_free
#define calloc heap_mem_calloc
#define realloc heap_mem_realloc

#ifdef __cplusplus
}
#endif

#endif /* HEAP_MALLOC_H */
