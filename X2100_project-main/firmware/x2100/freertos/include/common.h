#ifndef _COMMON_H_
#define _COMMON_H_

#include "bit_field.h"
#include "bits_opt.h"
#include "assert.h"
#include "dump_mem.h"
#include "io.h"
#include "types.h"
#include "compiler_gcc.h"
#include <error-base.h>
#include <stdio.h>
#include <stdlib.h>
#include <delay.h>
#include <driver/systick.h>
#include <string.h>
#include <dump_stack.h>
#include <err_ptr.h>
#include <sort.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x)  (int)( sizeof(x) / sizeof(x)[0] )
#endif

#ifdef DEBUG
#define debug(x...) \
    do { \
        printf(x); \
    } while (0)
#else
#define debug(x...) \
    do { \
        if (0) \
            printf(x); \
    } while (0)
#endif

void *memcpy(void *dest, const void *src, size_t n);

#define assert_range(x, start, end) assert(((x) >= (start)) && ((x) <= (end)))

#define assert_bool(x) assert((x) >= 0 && (x) <= 1)

#ifndef ALIGN
#define ALIGN(x, n) (((x) + (n) - 1) - ((x) + (n) - 1) % (n))
#endif

#endif /* _COMMON_H_ */

