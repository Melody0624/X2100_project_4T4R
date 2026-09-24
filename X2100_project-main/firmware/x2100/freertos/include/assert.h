#ifndef _ASSERT_H_
#define _ASSERT_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void hang(void) __attribute__ ((__noreturn__));

void __assert (const char *, int, const char *)
	    _ATTRIBUTE ((__noreturn__));

void __assert_func (const char *, int, const char *, const char *)
	    _ATTRIBUTE ((__noreturn__));

void __assert_no_file(const char *func, int line, const char *expr)
	    _ATTRIBUTE ((__noreturn__));

#ifdef CONFIG_ASSERT_WITH_NO_FILENAME
#define assert(_expr) \
    ((_expr) ? (void) 0 : __assert_no_file(__func__, __LINE__, #_expr));
#else
#define assert(_expr) \
        ((_expr) ? (void) 0 : __assert_func(__FILE__, __LINE__, __func__, #_expr))
#endif

#ifndef panic
#define panic(x...) \
    do { \
        printf(x); \
        hang(); \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASSERT_H_ */