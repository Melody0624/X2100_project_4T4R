#include <stdio.h>
#include <driver/systick.h>
#include <common.h>
#include "xformatc.h"
#include <module.h>

static int printf_time = 1;

static int printf_no_time = 0;

extern int __io_putchar(int ch);

void printf_enable_time_stamp(void)
{
    printf_no_time = 0;
}

void printf_disable_time_stamp(void)
{
    printf_no_time = 1;
}

static void m_printf_putchar(void *arg,char c)
{
    (void) arg;

    if (c == '\n')
        printf_time = 1;

    __io_putchar(c);
}

int printf(const char *__restrict fmt, ...)
{
    va_list list;
    unsigned int count;

    if (printf_time && !printf_no_time) {
        uint64_t now = systick_get_time_us();
        printf_time = 0;
        printf("[%lld.%06lld] ", now / USEC_PER_SEC, now % USEC_PER_SEC);
    }

    va_start(list, fmt);
    count = xvformat(m_printf_putchar, 0, fmt, list);
    va_end(list);

    return count;
}
EXPORT_SYMBOL(printf);

static void m_sprintf_putchar(void *arg,char c)
{
    char *sp = *(char **)arg;

    *sp = c;
    (*(char **)arg)++;
}

int sprintf(char *__restrict s, const char *__restrict fmt, ...)
{
    va_list list;
    unsigned count;

    assert(s);
    assert(fmt);

    va_start(list, fmt);
    count = xvformat(m_sprintf_putchar, s, fmt, list);
    va_end(list);

    s[count] = 0;

    return count;
}

struct snprintf_t {
    size_t n;
    size_t index;
    char *s;
};

static void m_snprintf_putchar(void *arg,char c)
{
    struct snprintf_t *sp = *(struct snprintf_t **)arg;

    if (sp->index >= sp->n)
        return;

    sp->index++;

    *(sp->s++) = c;
}

int snprintf(char *__restrict s, size_t n, const char *__restrict fmt, ...)
{
    va_list list;
    unsigned count;
    struct snprintf_t sp;

    sp.s = s;
    sp.n = n;
    sp.index = 0;

    assert(s);
    assert(fmt);

    va_start(list, fmt);
    count = xvformat(m_snprintf_putchar, &sp, fmt, list);
    va_end(list);

    s[count] = 0;

    return count;
}

int vsnprintf(char *__restrict s, size_t n, const char * __restrict fmt, va_list ap)
{
    unsigned count;
    struct snprintf_t sp;

    sp.s = s;
    sp.n = n;
    sp.index = 0;

    assert(s);
    assert(fmt);

    count = xvformat(m_snprintf_putchar, &sp, fmt, ap);

    s[count] = 0;

    return count;
}
