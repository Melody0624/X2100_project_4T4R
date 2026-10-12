#include "perf_logger.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <os.h>
#include <os/cpux_safe_libc.h>
#include <cpu/cpu.h>

#ifndef PERF_LOG_DISABLE

typedef enum {
    LOG_TYPE_MEASURE = 0,
    LOG_TYPE_PRINT   = 1,
} log_type_t;

typedef struct {
    uint8_t  type;          // 0=测量, 1=打印
    uint8_t  source;        // 显示[CPUx]
    uint8_t  depth;         // 缩进层级，0=顶层
    char     tag[32];       // 测量时存储标签（截断以防溢出）
    float    duration_ms;   // 测量时有效
    char     msg[128];      // 打印时存储格式化字符串
} perf_entry_t;

static perf_entry_t g_ring[PERF_LOG_BUFFER_SIZE];
static volatile uint32_t g_head = 0;
static volatile uint32_t g_tail = 0;
static volatile uint32_t g_discard_cnt = 0;

#include <general_functions.h>
float perf_logger_get_time_ms(void)
{
    return get_time_ns() / 1000000.0f;
}

// ==================== API 实现 ====================
void perf_logger_init(void)
{
    // 仅清空状态，无需创建任务
    g_head = 0;
    g_tail = 0;
    g_discard_cnt = 0;
}

// 记录函数
void perf_logger_record(const char *tag, float duration_ms, uint8_t source, uint8_t depth)
{
    uint32_t next_head = (g_head + 1) % PERF_LOG_BUFFER_SIZE;
    if (next_head != g_tail) {
        g_ring[g_head].type = LOG_TYPE_MEASURE;
        g_ring[g_head].source = source;
        g_ring[g_head].depth = depth;
        strncpy(g_ring[g_head].tag, tag, sizeof(g_ring[g_head].tag) - 1);
        g_ring[g_head].tag[sizeof(g_ring[g_head].tag) - 1] = '\0';
        g_ring[g_head].duration_ms = duration_ms;
        g_head = next_head;
    } else {
        g_discard_cnt++;
    }
}

static void perf_logger_record_print(const char *msg, uint8_t source)
{
    uint32_t next_head = (g_head + 1) % PERF_LOG_BUFFER_SIZE;
    if (next_head != g_tail)
    {
        g_ring[g_head].type = LOG_TYPE_PRINT;
        g_ring[g_head].source = source;
        strncpy(g_ring[g_head].msg, msg, sizeof(g_ring[g_head].msg) - 1);
        g_ring[g_head].msg[sizeof(g_ring[g_head].msg) - 1] = '\0';
        g_head = next_head;
    }
    else
    {
        g_discard_cnt++;
    }
}

void perf_logger_printf(const char *format, ...)
{
    char buf[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    perf_logger_record_print(buf, 0);
}

// flush 打印（分组并按深度缩进）
void perf_logger_flush(void)
{
    uint32_t start = g_tail;
    uint32_t end = g_head;
    uint32_t idx;

    printf("\n******** Performance Log ********\n");

    // 打印 CPU0
    for (idx = start; idx != end; idx = (idx + 1) % PERF_LOG_BUFFER_SIZE) {
        perf_entry_t *entry = &g_ring[idx];
        if (entry->source == 0) {
            if (entry->type == LOG_TYPE_MEASURE) {
                // 根据 depth 添加缩进
                const char *prefix = "---";
                printf("[PERF][CPU0] ");
                for (int i = 0; i < entry->depth; i++) printf("%s", prefix);
                printf("%s: %.2f ms\n", entry->tag, entry->duration_ms);
            } else if (entry->type == LOG_TYPE_PRINT) {
                printf("%s\n", entry->msg);
            }
        }
    }

    // 打印 CPU1
    for (idx = start; idx != end; idx = (idx + 1) % PERF_LOG_BUFFER_SIZE) {
        perf_entry_t *entry = &g_ring[idx];
        if (entry->source == 1) {
            if (entry->type == LOG_TYPE_MEASURE) {
                // 根据 depth 添加缩进
                const char *prefix = "---";
                printf("[PERF][CPU1] ");
                for (int i = 0; i < entry->depth; i++) printf("%s", prefix);
                printf("%s: %.2f ms\n", entry->tag, entry->duration_ms);
            } else if (entry->type == LOG_TYPE_PRINT) {
                printf("%s\n", entry->msg);
            }
        }
    }

    g_tail = g_head;

    if (g_discard_cnt > 0) {
        printf("[PERF] WARNING: %u entries discarded\n", g_discard_cnt);
        g_discard_cnt = 0;
    }
}

uint32_t perf_logger_get_discard_count(void)
{
    return g_discard_cnt;
}

#endif
