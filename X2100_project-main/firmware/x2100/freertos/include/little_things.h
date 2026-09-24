#ifndef _LITTLE_THINGS_H_
#define _LITTLE_THINGS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <types.h>
#include <ffs.h>

#undef UNUSED
#define UNUSED(x)               ((void)(x))

#undef offsetof
#define offsetof(TYPE, MEMBER)  ((size_t) &((TYPE *)0)->MEMBER)

/**
 * @brief cast a member of a structure out to the containing structure
 * @param ptr:    the pointer to the member.
 * @param type:   the type of the container struct this is embedded in.
 * @param member: the name of the member within the struct.
 *
 */
#undef container_of
#define container_of(ptr, type, member) ({                          \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);            \
    (type *)( (char *)__mptr - offsetof(type,member) );})

#undef address_offset
#define address_offset(void_ptr, offset)                            \
    ((void *)((uint8_t *)(void_ptr) + (offset)))

#undef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof (a) / sizeof ((a)[0]))

/**
 * @brief calculate the bit mask of a bit filed
 * @param start: start index of bit filed
 * @param end:   end index of bit filed
 * @details e.g BIT_MASK(0, 7) --> 0xff
 *              BIT_MASK(0, 0) --> 0x01
 *              BIT_MASK(1, 1) --> 0x02
 */
#define BIT_MASK(start, end) (((1 << ((end) - (start) + 1)) - 1) << (start))

/**
 * @brief modify the value, clear the bits_to_clr then set the bits_to_set
 * @param bits_to_clr: bits to clear, generally clear the mask of a bits filed
 * @param bits_to_set: bits to set, generally the val of a bits filed
 */
#define modify_val(val, bits_to_clr, bits_to_set)                   \
    do {                                                            \
        (val) = ((val) & ~(bits_to_clr)) | (bits_to_set);           \
    } while (0)

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x, y) ({                \
    typeof(x) _min1 = (x);          \
    typeof(y) _min2 = (y);          \
    (void) (&_min1 == &_min2);      \
    _min1 < _min2 ? _min1 : _min2; })

#define min_t(type, a ,b) ((type)a > (type)b ? (type)b : (type)a)

#define max(x, y) ({                \
    typeof(x) _max1 = (x);          \
    typeof(y) _max2 = (y);          \
    (void) (&_max1 == &_max2);      \
    _max1 > _max2 ? _max1 : _max2; })

#define max_t(type, a ,b) ((type)a < (type)b ? (type)b : (type)a)

#define clamp(val, lo, hi) min((typeof(val))max(val, lo), hi)

#ifndef TRUE
#define TRUE                            1
#endif

#ifndef FALSE
#define FALSE                           0
#endif

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
/**
 * round_up - round up to next specified power of 2
 * @x: the value to round
 * @y: multiple to round up to (must be a power of 2)
 *
 * Rounds @x up to next multiple of @y (which must be a power of 2).
 * To perform arbitrary rounding up, use roundup() below.
 */
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
/**
 * round_down - round down to next specified power of 2
 * @x: the value to round
 * @y: multiple to round down to (must be a power of 2)
 *
 * Rounds @x down to next multiple of @y (which must be a power of 2).
 * To perform arbitrary rounding down, use rounddown() below.
 */
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/**
 * roundup - round up to the next specified multiple
 * @x: the value to up
 * @y: multiple to round up to
 *
 * Rounds @x up to next multiple of @y. If @y will always be a power
 * of 2, consider using the faster round_up().
 */
#define roundup(x, y) (					\
{							\
	typeof(y) __y = y;				\
	(((x) + (__y - 1)) / __y) * __y;		\
}							\
)
/**
 * rounddown - round down to next specified multiple
 * @x: the value to round
 * @y: multiple to round down to
 *
 * Rounds @x down to next multiple of @y. If @y will always be a power
 * of 2, consider using the faster round_down().
 */
#define rounddown(x, y) (				\
{							\
	typeof(x) __x = (x);				\
	__x - (__x % (y));				\
}							\
)

/**
 * roundup_pow_of_two() - round up to nearest power of two
 * @n: value to round up
 */
static inline unsigned long roundup_pow_of_two(unsigned long n)
{
	return 1UL << cpu_fls(n - 1);
}

/**
 * rounddown_pow_of_two() - round down to nearest power of two
 * @n: value to round down
 */
static inline unsigned long rounddown_pow_of_two(unsigned long n)
{
	return 1UL << (cpu_fls(n) - 1);
}

/// @return 写入的字符数（不含 '\0'）；若 dst 太小返回 -1
static inline int bytes_to_hex(const unsigned char *src, int n, char *dst, int dstsz, int uppercase) {
    static const char HEX_L[] = "0123456789abcdef";
    static const char HEX_U[] = "0123456789ABCDEF";
    const char *HEX = uppercase ? HEX_U : HEX_L;

    if (dstsz < n * 2 + 1) return -1;
    for (int i = 0; i < n; ++i) {
        unsigned char b = src[i];
        dst[i * 2 + 0] = HEX[b >> 4];
        dst[i * 2 + 1] = HEX[b & 0x0F];
    }
    dst[n * 2] = '\0';
    return (int)(n * 2);
}

#ifdef __cplusplus
}
#endif

#endif /* end of _LITTLE_THINGS_H_ */
