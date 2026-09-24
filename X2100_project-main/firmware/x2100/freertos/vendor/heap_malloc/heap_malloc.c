#include "heap_malloc.h"
#include <string.h>
#include <stdio.h>
#include "os/freertos/include/task.h"

/* 分配记录结构体 */
typedef struct alloc_record {
    void *user_ptr;                 // 用户内存指针
    size_t size;                    // 请求大小
    // TaskHandle_t task;              // 分配时的任务句柄（中断中为 NULL）
    struct alloc_record *next;      // 链表指针
} alloc_record_t;

/* 全局链表与统计变量 */
static alloc_record_t *alloc_list = NULL;
static size_t total_allocated = 0;      // 累计分配总字节数
static size_t current_allocated = 0;    // 当前已分配字节数
static size_t peak_allocated = 0;       // 历史峰值字节数
static size_t total_alloc_count = 0;    // 累计分配次数
static size_t valid_alloc_count = 0;    // 当前有效分配次数
static size_t total_free_count = 0;     // 累计释放次数
static size_t total_freed_bytes = 0;    // 累计释放字节数

/* 添加记录（需在临界区内调用） */
// static void add_record(void *ptr, size_t size, TaskHandle_t task)
static void add_record(void *ptr, size_t size)
{
    alloc_record_t *rec = pvPortMalloc(sizeof(alloc_record_t));
    if (rec == NULL)
    {
        /* 记录分配失败，仍允许用户内存分配成功，但统计将不完整 */
        return;
    }

    // printf("record: 0x%08p %ld, add_record: 0x%08p %ld\n", rec, sizeof(alloc_record_t), ptr, size);

    // (void)task;

    rec->user_ptr = ptr;
    rec->size = size;
    // rec->task = task;
    rec->next = alloc_list;
    alloc_list = rec;
}

/* 移除记录并返回其大小（需在临界区内调用） */
static size_t remove_record(void *ptr)
{
    alloc_record_t *prev = NULL;
    alloc_record_t *curr = alloc_list;
    while (curr != NULL)
    {
        if (curr->user_ptr == ptr)
        {
            if (prev == NULL)
            {
                alloc_list = curr->next;
            } 
            else
            {
                prev->next = curr->next;
            }
            size_t size = curr->size;
            vPortFree(curr);
            // printf("record: 0x%08p %ld, remove_record: 0x%08p %ld\n", curr, sizeof(alloc_record_t), ptr, size);
            return size;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;   // 未找到记录
}

/* ================= 标准库函数替换 ================= */
void *heap_mem_malloc(size_t size)
{
    if (size == 0) return NULL;

    /* 分配用户内存 */
    void *user_ptr = pvPortMalloc(size);
    if (user_ptr == NULL) return NULL;

    /* 获取当前任务句柄（中断中返回 NULL） */
    // TaskHandle_t task = xTaskGetCurrentTaskHandle();

    /* 分配并添加记录 */
    taskENTER_CRITICAL();
    // add_record(user_ptr, size, task);
    add_record(user_ptr, size);
    /* 更新统计 */
    total_allocated += size;
    current_allocated += size;
    if (current_allocated > peak_allocated) peak_allocated = current_allocated;
    total_alloc_count++;
    valid_alloc_count++;
    taskEXIT_CRITICAL();

    return user_ptr;
}

void heap_mem_free(void *ptr)
{
    if (ptr == NULL) return;

    taskENTER_CRITICAL();
    size_t size = remove_record(ptr);
    if (size > 0)
    {
        current_allocated -= size;
        valid_alloc_count--;
        total_free_count++;
        total_freed_bytes += size;
    }
    taskEXIT_CRITICAL();

    /* 释放用户内存 */
    vPortFree(ptr);
}

void *heap_mem_calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (total == 0) return NULL;

    void *ptr = heap_mem_malloc(total);
    if (ptr != NULL)
    {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *heap_mem_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL)
    {
        return heap_mem_malloc(new_size);
    }
    if (new_size == 0)
    {
        heap_mem_free(ptr);
        return NULL;
    }

    /* 获取原块大小（需遍历链表，可能较慢） */
    size_t old_size = 0;
    taskENTER_CRITICAL();
    alloc_record_t *curr = alloc_list;
    while (curr != NULL)
    {
        if (curr->user_ptr == ptr)
        {
            old_size = curr->size;
            break;
        }
        curr = curr->next;
    }
    taskEXIT_CRITICAL();

    if (old_size == 0)
    {
        void *new_ptr = heap_mem_malloc(new_size);
        if (new_ptr != NULL && ptr != NULL)
        {
            memcpy(new_ptr, ptr, new_size);
        }
        heap_mem_free(ptr);
        return new_ptr;
    }

    void *new_ptr = heap_mem_malloc(new_size);
    if (new_ptr != NULL)
    {
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
        heap_mem_free(ptr);
    }
    return new_ptr;
}

/* ================= 统计查询接口 ================= */
size_t mem_get_total_allocated(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = total_allocated;
    taskEXIT_CRITICAL();
    return val;
}

size_t mem_get_current_allocated(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = current_allocated;
    taskEXIT_CRITICAL();
    return val;
}

size_t mem_get_peak_allocated(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = peak_allocated;
    taskEXIT_CRITICAL();
    return val;
}

size_t mem_get_total_alloc_count(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = total_alloc_count;
    taskEXIT_CRITICAL();
    return val;
}

size_t mem_get_valid_alloc_count(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = valid_alloc_count;
    taskEXIT_CRITICAL();
    return val;
}

size_t mem_get_total_free_count(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = total_free_count;
    taskEXIT_CRITICAL();
    return val;
}

size_t mem_get_total_freed_bytes(void)
{
    size_t val;
    taskENTER_CRITICAL();
    val = total_freed_bytes;
    taskEXIT_CRITICAL();
    return val;
}

void mem_print_stats(void)
{
    taskENTER_CRITICAL();
    printf("========== Application Heap Statistics ==========\n");
    printf("Total allocated:       %.2f KiB (%.2f MiB)\n",
           total_allocated / 1024.0, total_allocated / 1024.0 / 1024.0);
    printf("Current allocated:     %.2f KiB (%.2f MiB)\n",
           current_allocated / 1024.0, current_allocated / 1024.0 / 1024.0);
    printf("Peak allocated:        %.2f KiB (%.2f MiB)\n",
           peak_allocated / 1024.0, peak_allocated / 1024.0 / 1024.0);
    printf("Total allocation count: %zu\n", total_alloc_count);
    printf("Valid allocation count: %zu\n", valid_alloc_count);
    printf("Total free count:       %zu\n", total_free_count);
    printf("Total freed:            %.2f KiB (%.2f MiB)\n",
           total_freed_bytes / 1024.0, total_freed_bytes / 1024.0 / 1024.0);
    printf("==================================================\n");
    taskEXIT_CRITICAL();
}

void mem_print_all_allocations(void)
{
    taskENTER_CRITICAL();
    printf("========== Active Allocations ==========\n");
    alloc_record_t *curr = alloc_list;
    while (curr != NULL)
    {
        // const char *task_name = pcTaskGetName(curr->task);
        // if (task_name == NULL) task_name = "ISR/Unknown";
        printf("ptr: 0x%p, size: %zu\n", curr->user_ptr, curr->size);
        curr = curr->next;
    }
    printf("========================================\n");
    taskEXIT_CRITICAL();
}

#include <lds_symbol.h>

extern char *sbrk(int nbytes);

/**
 * @brief 获取系统总堆大小（由 linker 脚本定义）
 */
size_t mem_get_system_total(void)
{
    return (size_t)((unsigned long)&_user_heap_end -
                    (unsigned long)&_user_heap_start);
}

/**
 * @brief 获取系统当前已使用堆大小（通过 sbrk 获取当前 break）
 *
 * 当 sbrk 返回 -1 时返回 0，由调用者自行判断。
 */
size_t mem_get_system_used(void)
{
    char *brk = sbrk(0);
    if (brk == (char *)(-1))
        return 0;
    return (size_t)((unsigned long)brk -
                    (unsigned long)&_user_heap_start);
}

/**
 * @brief 获取系统剩余可用堆大小
 */
size_t mem_get_system_free(void)
{
    return mem_get_system_total() - mem_get_system_used();
}

void mem_print_system_heap(const char *tag)
{
    taskENTER_CRITICAL();
    size_t total = mem_get_system_total();
    size_t used  = mem_get_system_used();
    size_t freed = mem_get_system_free();
    size_t track = mem_get_current_allocated();

    if (tag && tag[0])
        printf("[%s]\n", tag);

    printf("========== System Heap Statistics ==========\n");
    printf("All Freed             = %.2f KiB (%.2f MiB)\n", freed / 1024.0, freed / 1024.0 / 1024.0);
    printf("All Used              = %.2f KiB (%.2f MiB)\n", used / 1024.0, used / 1024.0 / 1024.0);
    printf("Total System Heap     = %.2f KiB (%.2f MiB)\n", total / 1024.0, total / 1024.0 / 1024.0);
    printf("Tracked (Application) = %.2f KiB (%.2f MiB)\n", track / 1024.0, track / 1024.0 / 1024.0);
    printf("========================================\n");
    taskEXIT_CRITICAL();
}
