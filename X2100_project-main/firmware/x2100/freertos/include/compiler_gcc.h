#ifndef COMPILER_GCC_H
#define COMPILER_GCC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WEAK
    #define WEAK   __attribute__((weak))
#endif

#ifndef __weak
    #define __weak   __attribute__((weak))
#endif

#ifndef __packed
    #define __packed __attribute__((__packed__))
#endif

#ifndef __align
    #define __align(x) __attribute__((aligned (x)))
#endif

#ifndef __aligned
    #define __aligned(x) __attribute__((aligned(x)))
#endif

#ifndef __always_inline
    #define __always_inline __attribute__((__always_inline__))
#endif

#ifndef likely
    #define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
    #define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

#ifndef noinline
#define  noinline            __attribute__((noinline))
#endif

#define barrier() __asm__ __volatile__("": : :"memory")

#ifndef GCC_OFF_SMALL_ASM
#define GCC_OFF_SMALL_ASM() "ZC"
#endif

#ifndef ACCESS_ONCE
#define __ACCESS_ONCE(x) ({ \
	__attribute__((unused)) typeof(x) __var = (typeof(x)) 0; \
	(volatile typeof(x) *)&(x); })
#define ACCESS_ONCE(x) (*__ACCESS_ONCE(x))
#endif

#ifndef WRITE_ONCE
static inline void __write_once_size(volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(volatile unsigned char *)p = *(unsigned char *)res; break;
	case 2: *(volatile unsigned short *)p = *(unsigned short *)res; break;
	case 4: *(volatile unsigned int *)p = *(unsigned int *)res; break;
	case 8: *(volatile unsigned long long *)p = *(unsigned long long *)res; break;
	default:
		barrier();
		__builtin_memcpy((void *)p, (const void *)res, size);
		barrier();
	}
}

#define WRITE_ONCE(x, val) \
({							\
	union { typeof(x) __val; char __c[1]; } __u =	\
		{ .__val = (__force typeof(x)) (val) }; \
	__write_once_size(&(x), __u.__c, sizeof(x));	\
	__u.__val;					\
})
#endif

#ifdef __cplusplus
}
#endif

#endif

