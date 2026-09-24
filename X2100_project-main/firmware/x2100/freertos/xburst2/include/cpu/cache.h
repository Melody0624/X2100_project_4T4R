#ifndef _CPU_CACHE_H_
#define _CPU_CACHE_H_

#include <asm/cacheops.h>
#include <asm/barrier.h>

#define cache_op(op, addr)       \
    __asm__ __volatile__(        \
        ".set    push\n"         \
        ".set    noreorder\n"    \
        ".set    mips32r2\n"     \
        "cache    %0, %1\n"      \
        ".set    pop\n"          \
        :            \
        : "i" (op), "R" (*(unsigned char *)(addr)))

#define fast_iob()          \
    do {                    \
        __sync();           \
        __fast_iob();       \
    } while (0)

#define cache_prefetch(label,size)                     \
do{                                                    \
    unsigned long addr,end;                            \
    /* Prefetch codes from label */                    \
    addr = (unsigned long)(&&label) & ~(32 - 1);       \
    end = (unsigned long)(&&label + size) & ~(32 - 1); \
    end += 32;                                         \
    for (; addr < end; addr += 32) {                   \
        __asm__ volatile (                             \
                ".set push\n\t"                        \
                ".set mips32r2\n\t"                    \
                " cache %0, 0(%1)\n\t"                 \
                ".set pop\n\t"                         \
                :                                      \
                : "I" (Index_Prefetch_I), "r"(addr));  \
    }                                                  \
}                                                      \
while(0)

#endif