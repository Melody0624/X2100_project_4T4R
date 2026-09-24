#include <os/cpux_safe_libc.h>

#include <stdint.h>
#include <driver/console.h>
#ifdef CONFIG_OS
#include <assert.h>
#include <cpu/cpu.h>
#include <os.h>
#include <ring_mem.h>
#endif
#include "xformatc.h"

struct cpux_print_ctx {
    char *dst;
    size_t size;
    size_t index;
};

static void cpux_snprintf_putchar(void *arg, char c)
{
    struct cpux_print_ctx *ctx = *(struct cpux_print_ctx **)arg;

    if (ctx->size && ctx->index + 1 < ctx->size)
        ctx->dst[ctx->index] = c;

    ctx->index++;
}

int cpux_vsnprintf(char *dst, size_t size, const char *fmt, va_list ap)
{
    unsigned count;
    struct cpux_print_ctx ctx;
    size_t end;

    if (!fmt)
        return -1;

    if (!dst && size)
        return -1;

    ctx.dst = dst;
    ctx.size = size;
    ctx.index = 0;

    count = xvformat(cpux_snprintf_putchar, &ctx, fmt, ap);

    if (size) {
        end = (ctx.index < size) ? ctx.index : (size - 1);
        dst[end] = '\0';
    }

    return (int)count;
}

int cpux_snprintf(char *dst, size_t size, const char *fmt, ...)
{
    int count;
    va_list ap;

    va_start(ap, fmt);
    count = cpux_vsnprintf(dst, size, fmt, ap);
    va_end(ap);

    return count;
}

int cpux_sprintf(char *dst, const char *fmt, ...)
{
    int count;
    va_list ap;

    va_start(ap, fmt);
    count = cpux_vsnprintf(dst, (size_t)-1, fmt, ap);
    va_end(ap);

    return count;
}

static int cpux_default_printf_sink(const char *buf, unsigned int len)
{
    unsigned int i;

    if (!buf || !len)
        return 0;

    for (i = 0; i < len; i++)
        console_put_char(buf[i]);

    return (int)len;
}

static cpux_printf_sink_t cpux_printf_sink = cpux_default_printf_sink;

void cpux_set_printf_sink(cpux_printf_sink_t sink)
{
    if (sink)
        cpux_printf_sink = sink;
    else
        cpux_printf_sink = cpux_default_printf_sink;
}

#define CPUX_PRINTF_BUFSIZE 256
int cpux_vprintf(const char *fmt, va_list ap)
{
    int len;
    unsigned int out_len;
    char buf[CPUX_PRINTF_BUFSIZE];

    len = cpux_vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len <= 0)
        return len;

    if ((unsigned int)len >= sizeof(buf))
        out_len = sizeof(buf) - 1;
    else
        out_len = (unsigned int)len;

    cpux_printf_sink(buf, out_len);

    return len;
}

int cpux_printf(const char *fmt, ...)
{
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = cpux_vprintf(fmt, ap);
    va_end(ap);

    return ret;
}

#ifdef CONFIG_OS
#define CPUX_PRINTF_RING_SIZE      4096
#define CPUX_PRINTF_DRAIN_CHUNK    128
#define CPUX_PRINTF_POLL_MS        2

static volatile int cpux_printf_service_started;
static volatile int cpux_printf_service_exit;
static volatile int cpux_printf_service_done;
static volatile unsigned int cpux_printf_drop_bytes;
static thread_ptr_t cpux_printf_drain_thread;

static char cpux_printf_ring_mem[CPUX_PRINTF_RING_SIZE];
static struct ring_mem cpux_printf_ring;

static void cpux_printf_ring_reset(void)
{
    ring_mem_init(&cpux_printf_ring, cpux_printf_ring_mem, sizeof(cpux_printf_ring_mem));
    cpux_printf_drop_bytes = 0;
}

static int cpux_printf_ring_sink(const char *buf, unsigned int len)
{
    int written;

    if (!buf || !len)
        return 0;

    written = ring_mem_write(&cpux_printf_ring, buf, len);
    if (written < 0)
        written = 0;
    if ((unsigned int)written < len)
        cpux_printf_drop_bytes += len - (unsigned int)written;

    return written;
}

static unsigned int cpux_printf_ring_used(void)
{
    return ring_mem_readable_size(&cpux_printf_ring);
}

static unsigned int cpux_printf_ring_read(char *buf, unsigned int max_len)
{
    if (!buf || !max_len)
        return 0;

    return ring_mem_read(&cpux_printf_ring, buf, max_len);
}

static void cpux_printf_console_send(const char *buf, unsigned int len)
{
    unsigned int i;

    if (!buf || !len)
        return;

    for (i = 0; i < len; i++)
        console_put_char(buf[i]);
}

static void cpux_printf_drain_entry(void *arg)
{
    unsigned int len;
    char msg[CPUX_PRINTF_DRAIN_CHUNK];

    (void)arg;

    cpux_printf_service_done = 0;

    while (1) {
        len = cpux_printf_ring_read(msg, sizeof(msg));
        if (len) {
            cpux_printf_console_send(msg, len);
            continue;
        }

        if (cpux_printf_service_exit)
            break;

        msleep(CPUX_PRINTF_POLL_MS);
    }

    cpux_printf_service_done = 1;
}
#endif

int cpux_printf_service_init(void)
{
#ifdef CONFIG_OS
    if (cpux_printf_service_started)
        return 0;

    cpux_printf_service_exit = 0;
    cpux_printf_service_done = 0;
    cpux_printf_ring_reset();

    cpux_set_printf_sink(cpux_printf_ring_sink);
    cpux_printf_drain_thread = thread_create("cpux-log", 2048, cpux_printf_drain_entry, NULL);
    if (!cpux_printf_drain_thread) {
        cpux_set_printf_sink(NULL);
        return -1;
    }

    cpux_printf_service_started = 1;
    return 0;
#else
    return -1;
#endif
}

void cpux_printf_service_deinit(void)
{
#ifdef CONFIG_OS
    int i;

    if (!cpux_printf_service_started)
        return;

    /*
     * Stop publishing to the ring first so drain thread can eventually
     * observe an empty queue and exit.
     */
    cpux_set_printf_sink(NULL);

    for (i = 0; i < 500; i++) {
        if (!cpux_printf_ring_used())
            break;
        msleep(10);
    }

    cpux_printf_service_exit = 1;
    for (i = 0; i < 200; i++) {
        if (cpux_printf_service_done)
            break;
        msleep(10);
    }

    if (cpux_printf_drain_thread) {
        thread_join(cpux_printf_drain_thread, NULL);
        cpux_printf_drain_thread = NULL;
    }

    cpux_printf_service_started = 0;
#endif
}

unsigned int cpux_printf_service_drop_bytes(void)
{
#ifdef CONFIG_OS
    return cpux_printf_drop_bytes;
#else
    return 0;
#endif
}

enum cpux_scan_len {
    CPUX_SCAN_LEN_DEF = 0,
    CPUX_SCAN_LEN_HH,
    CPUX_SCAN_LEN_H,
    CPUX_SCAN_LEN_L,
    CPUX_SCAN_LEN_LL,
};

enum cpux_scan_fail {
    CPUX_SCAN_FAIL_NONE = 0,
    CPUX_SCAN_FAIL_MATCH,
    CPUX_SCAN_FAIL_INPUT,
};

static int cpux_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

static int cpux_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int cpux_digit_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

static void cpux_skip_input_spaces(const char **src)
{
    while (cpux_is_space(**src))
        (*src)++;
}

static int cpux_parse_unsigned(const char **src, int base, int width,
                               int allow_sign, int *negative,
                               unsigned long long *value,
                               enum cpux_scan_fail *fail_kind)
{
    const char *p = *src;
    int max = (width > 0) ? width : 0x7fffffff;
    int digit_count = 0;
    int d;
    int had_char = 0;
    unsigned long long v = 0;

    *negative = 0;
    *fail_kind = CPUX_SCAN_FAIL_MATCH;

    if (max > 0 && *p)
        had_char = 1;

    if (max > 0 && allow_sign && (*p == '+' || *p == '-')) {
        *negative = (*p == '-');
        p++;
        max--;
    }

    if (base == 0) {
        if (max >= 3 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
            cpux_digit_val(p[2]) >= 0) {
            base = 16;
            p += 2;
            max -= 2;
        } else if (max >= 1 && p[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    }

    if (max >= 3 && base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') &&
        cpux_digit_val(p[2]) >= 0) {
        p += 2;
        max -= 2;
    }

    while (max > 0) {
        d = cpux_digit_val(*p);
        if (d < 0 || d >= base)
            break;

        v = v * (unsigned long long)base + (unsigned long long)d;
        p++;
        max--;
        digit_count++;
    }

    if (!digit_count)
    {
        if (!had_char)
            *fail_kind = CPUX_SCAN_FAIL_INPUT;
        return 0;
    }

    *value = v;
    *src = p;

    return 1;
}

static void cpux_store_signed_ptr(void *ptr, enum cpux_scan_len len, long long value)
{
    switch (len) {
    case CPUX_SCAN_LEN_HH:
        *(signed char *)ptr = (signed char)value;
        break;
    case CPUX_SCAN_LEN_H:
        *(short *)ptr = (short)value;
        break;
    case CPUX_SCAN_LEN_L:
        *(long *)ptr = (long)value;
        break;
    case CPUX_SCAN_LEN_LL:
        *(long long *)ptr = value;
        break;
    default:
        *(int *)ptr = (int)value;
        break;
    }
}

static void cpux_store_unsigned_ptr(void *ptr, enum cpux_scan_len len,
                                    unsigned long long value)
{
    switch (len) {
    case CPUX_SCAN_LEN_HH:
        *(unsigned char *)ptr = (unsigned char)value;
        break;
    case CPUX_SCAN_LEN_H:
        *(unsigned short *)ptr = (unsigned short)value;
        break;
    case CPUX_SCAN_LEN_L:
        *(unsigned long *)ptr = (unsigned long)value;
        break;
    case CPUX_SCAN_LEN_LL:
        *(unsigned long long *)ptr = value;
        break;
    default:
        *(unsigned int *)ptr = (unsigned int)value;
        break;
    }
}

int cpux_vsscanf(const char *src, const char *fmt, va_list ap)
{
    const char *s = src;
    const char *f = fmt;
    int assigned = 0;
    enum cpux_scan_fail fail_kind = CPUX_SCAN_FAIL_NONE;

    if (!src || !fmt)
        return -1;

    while (*f) {
        int suppress = 0;
        int width = 0;
        int has_width = 0;
        enum cpux_scan_len len = CPUX_SCAN_LEN_DEF;
        char spec;

        if (cpux_is_space(*f)) {
            while (cpux_is_space(*f))
                f++;
            cpux_skip_input_spaces(&s);
            continue;
        }

        if (*f != '%') {
            if (*s != *f) {
                fail_kind = *s ? CPUX_SCAN_FAIL_MATCH : CPUX_SCAN_FAIL_INPUT;
                break;
            }
            s++;
            f++;
            continue;
        }

        f++;

        if (*f == '%') {
            if (*s != '%') {
                fail_kind = *s ? CPUX_SCAN_FAIL_MATCH : CPUX_SCAN_FAIL_INPUT;
                break;
            }
            s++;
            f++;
            continue;
        }

        if (*f == '*') {
            suppress = 1;
            f++;
        }

        while (cpux_is_digit(*f)) {
            has_width = 1;
            width = width * 10 + (*f - '0');
            f++;
        }

        if (*f == 'h') {
            f++;
            if (*f == 'h') {
                len = CPUX_SCAN_LEN_HH;
                f++;
            } else {
                len = CPUX_SCAN_LEN_H;
            }
        } else if (*f == 'l') {
            f++;
            if (*f == 'l') {
                len = CPUX_SCAN_LEN_LL;
                f++;
            } else {
                len = CPUX_SCAN_LEN_L;
            }
        }

        spec = *f++;
        if (!spec)
            break;

        if (spec != 'c' && spec != 'n')
            cpux_skip_input_spaces(&s);

        if (spec == 'n') {
            if (!suppress) {
                void *ptr = va_arg(ap, void *);
                cpux_store_signed_ptr(ptr, len, (long long)(s - src));
            }
            continue;
        }

        if (spec == 'c') {
            int i;
            int n = has_width ? width : 1;
            const char *tmp;
            char *out;

            if (!n)
            {
                fail_kind = CPUX_SCAN_FAIL_MATCH;
                break;
            }

            if (!suppress)
                out = va_arg(ap, char *);
            else
                out = NULL;

            tmp = s;
            for (i = 0; i < n; i++) {
                if (!*tmp) {
                    fail_kind = CPUX_SCAN_FAIL_INPUT;
                    goto out;
                }
                tmp++;
            }

            for (i = 0; i < n; i++) {
                if (!suppress)
                    out[i] = *s;
                s++;
            }

            if (!suppress)
                assigned++;

            continue;
        }

        if (spec == 's') {
            int n = 0;
            int max = has_width ? width : 0x7fffffff;
            char *out;

            if (!suppress)
                out = va_arg(ap, char *);
            else
                out = NULL;

            while (*s && !cpux_is_space(*s) && n < max) {
                if (!suppress)
                    out[n] = *s;
                s++;
                n++;
            }

            if (!n) {
                fail_kind = *s ? CPUX_SCAN_FAIL_MATCH : CPUX_SCAN_FAIL_INPUT;
                break;
            }

            if (!suppress) {
                out[n] = '\0';
                assigned++;
            }

            continue;
        }

        if (spec == 'd' || spec == 'i' || spec == 'u' ||
            spec == 'x' || spec == 'X' || spec == 'o' || spec == 'p') {
            unsigned long long uv;
            long long sv;
            int allow_sign;
            int negative;
            int base;
            int ok;
            int max = has_width ? width : 0;
            int signed_conv = (spec == 'd' || spec == 'i');
            enum cpux_scan_fail parse_fail;

            switch (spec) {
            case 'o':
                base = 8;
                break;
            case 'x':
            case 'X':
            case 'p':
                base = 16;
                break;
            case 'i':
                base = 0;
                break;
            default:
                base = 10;
                break;
            }

            /*
             * Keep %p strict: pointer text must be unsigned numeric form
             * (optional 0x prefix), no leading '+'/'-'.
             */
            allow_sign = (spec == 'p') ? 0 : 1;
            ok = cpux_parse_unsigned(&s, base, max, allow_sign, &negative, &uv, &parse_fail);
            if (!ok) {
                fail_kind = parse_fail;
                break;
            }

            if (spec == 'p') {
                if (!suppress) {
                    void **pp = va_arg(ap, void **);
                    *pp = (void *)(uintptr_t)uv;
                    assigned++;
                }
                continue;
            }

            if (signed_conv) {
                sv = negative ? -(long long)uv : (long long)uv;
                if (!suppress) {
                    void *ptr = va_arg(ap, void *);
                    cpux_store_signed_ptr(ptr, len, sv);
                    assigned++;
                }
            } else {
                if (negative)
                    uv = (unsigned long long)(0ULL - uv);
                if (!suppress) {
                    void *ptr = va_arg(ap, void *);
                    cpux_store_unsigned_ptr(ptr, len, uv);
                    assigned++;
                }
            }

            continue;
        }

        /* unsupported token in tiny parser */
        fail_kind = CPUX_SCAN_FAIL_MATCH;
        break;
    }

out:
    if (!assigned && fail_kind == CPUX_SCAN_FAIL_INPUT)
        return -1;

    return assigned;
}

int cpux_sscanf(const char *src, const char *fmt, ...)
{
    int ret;
    va_list ap;

    va_start(ap, fmt);
    ret = cpux_vsscanf(src, fmt, ap);
    va_end(ap);

    return ret;
}
