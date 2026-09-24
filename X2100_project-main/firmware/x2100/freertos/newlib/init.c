
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <common.h>

/*
 * newlib 4.3.0 初始化/清理函数
 * 提供自己的实现，支持 __attribute__((constructor)) 和 C++ 全局对象
 */

/* _init() 和 _fini() - newlib 需要的符号 */
void _init(void)
{
    /* 空实现 - 不需要额外的初始化 */
}

void _fini(void)
{
    /* 空实现 - 不需要额外的清理 */
}

/*
 * __libc_init_array() - 初始化 C 库的构造函数数组
 *
 * 处理以下内容：
 * 1. _init() - 平台特定初始化
 * 2. .preinit_array - 预初始化函数（通常为空）
 * 3. .init_array - 构造函数数组（__attribute__((constructor)) 和 C++ 全局对象）
 */
void __libc_init_array(void)
{
    extern void (*__preinit_array_start[])(void);
    extern void (*__preinit_array_end[])(void);
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);

    size_t count;
    size_t i;

    /* 1. 调用 _init() */
    _init();

    /* 2. 遍历 .preinit_array 段（通常为空） */
    count = __preinit_array_end - __preinit_array_start;
    for (i = 0; i < count; i++) {
        if (__preinit_array_start[i]) {
            __preinit_array_start[i]();
        }
    }

    /* 3. 遍历 .init_array 段
     * 包含：
     * - __attribute__((constructor)) 标记的函数
     * - C++ 全局对象的构造函数
     */
    count = __init_array_end - __init_array_start;
    for (i = 0; i < count; i++) {
        if (__init_array_start[i]) {
            __init_array_start[i]();
        }
    }
}

/*
 * __libc_fini_array() - 清理 C 库的析构函数数组
 *
 * 处理以下内容：
 * 1. .fini_array - 析构函数数组（__attribute__((destructor)) 和 C++ 全局对象析构）
 * 2. _fini() - 平台特定清理
 *
 * 注意：析构函数按反向顺序调用
 */
void __libc_fini_array(void)
{
    extern void (*__fini_array_start[])(void);
    extern void (*__fini_array_end[])(void);

    size_t count;
    size_t i;

    /* 1. 反向遍历 .fini_array 段 */
    count = __fini_array_end - __fini_array_start;
    for (i = count; i > 0; i--) {
        if (__fini_array_start[i - 1]) {
            __fini_array_start[i - 1]();
        }
    }

    /* 2. 调用 _fini() */
    _fini();
}

int kill(pid_t pid, int sig)
{
    hang();
    return 0;
}

pid_t getpid(void)
{
    return 0;
}

void abort(void)
{
    hang();
}

int getpagesize(void)
{
    return 4096;
}
