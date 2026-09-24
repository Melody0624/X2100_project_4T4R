#ifndef _CPUX_SAFE_LIBC_H_
#define _CPUX_SAFE_LIBC_H_

#include <stdarg.h>
#include <stddef.h>

/*
 * CPUX libc 使用说明（适用于 arch_startup_cpu 拉起的裸函数场景）
 *
 * [推荐]
 * 1) 格式化/解析：使用本文件提供的 cpux_snprintf/cpux_sscanf 系列。
 * 2) 字符串/内存纯函数：memcpy/memmove/memset/memcmp/strlen/strcmp/
 *    strncmp/strcpy/strncpy/strchr/strrchr/strstr/abs/labs/qsort/bsearch/
 *    strtok_r 一般可安全使用。
 *
 * [避免]
 * 1) newlib 全局状态/锁相关：printf/sprintf/snprintf/vsnprintf/scanf/
 *    sscanf/vsscanf/fopen/fread/fwrite/fclose 等 stdio API。
 * 2) 堆/errno/时区/随机数相关：malloc/free/calloc/realloc、atoi/strtol/
 *    strtoul/strtod、rand/random/srand、ctime/localtime/gmtime/strerror 等。
 *
 * 原因：上述接口通常依赖 _impure_ptr、FILE 全局结构或 newlib 锁，
 * 在 CPUX 非 RTOS 任务上下文中不保证并发安全。
 */

/*
 * CPUX-safe tiny formatter/parser.
 *
 * Design goals:
 * 1) Do not depend on newlib's _impure_ptr / stdio internals.
 * 2) No heap usage.
 * 3) Keep API small for secondary-core bare function usage.
 */
int cpux_sprintf(char *dst, const char *fmt, ...);
int cpux_snprintf(char *dst, size_t size, const char *fmt, ...);
int cpux_vsnprintf(char *dst, size_t size, const char *fmt, va_list ap);


/*
 * Async printf service (CPUX writes ring, CPU0 thread drains to console).
 * init/deinit are expected to be called on RTOS side.
 * cpux_printf() is CPUX-only while this service is enabled.
 */
int cpux_printf_service_init(void);
void cpux_printf_service_deinit(void);
unsigned int cpux_printf_service_drop_bytes(void);

typedef int (*cpux_printf_sink_t)(const char *buf, unsigned int len);
void cpux_set_printf_sink(cpux_printf_sink_t sink);

/* CPUX-only logging API. CPU0 should use printf(). */
int cpux_printf(const char *fmt, ...);
int cpux_vprintf(const char *fmt, va_list ap);

/*
 * tiny sscanf subset:
 *   %d %i %u %x %X %o %c %s %p %n and %%
 *   width / '*' suppression / hh h l ll length modifiers
 *   %p accepts unsigned numeric text only (no leading sign)
 */
int cpux_sscanf(const char *src, const char *fmt, ...);
int cpux_vsscanf(const char *src, const char *fmt, va_list ap);

#endif /* _CPUX_SAFE_LIBC_H_ */
