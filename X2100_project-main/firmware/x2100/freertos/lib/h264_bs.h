#ifndef _H264_BS_H_
#define _H264_BS_H_

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *start;
    uint8_t *p;
    uint8_t *end;
    int bits_left;
} bs_t;

static inline bs_t *bs_init(bs_t *b, uint8_t *buf, size_t size)
{
    b->start = buf;
    b->p = buf;
    b->end = buf + size;
    b->bits_left = 8;
    return b;
}

static inline int bs_eof(bs_t *b)
{
    if (b->p >= b->end) {
        return 1;
    } else {
        return 0;
    }
}

static inline uint32_t bs_read_u1(bs_t *b)
{
    uint32_t r = 0;

    b->bits_left--;

    if (! bs_eof(b)) {
        r = ((*(b->p)) >> b->bits_left) & 0x01;
    }

    if (b->bits_left == 0) {
        b->p ++;
        b->bits_left = 8;
    }

    return r;
}

static inline uint32_t bs_read_u(bs_t *b, int n)
{
        uint32_t r = 0;
        int i;
        for (i = 0; i < n; i++) {
                r |= (bs_read_u1(b) << (n - i - 1));
        }
        return r;
}

static inline uint32_t bs_read_ue(bs_t *b)
{
    int32_t r = 0;
    int i = 0;

    while ((bs_read_u1(b) == 0) && (i < 32) && (!bs_eof(b))) {
        i++;
    }
    r = bs_read_u(b, i);
    r += (1 << i) - 1;
    return r;
}

static inline int32_t bs_read_se(bs_t *b)
{
    int32_t r = bs_read_ue(b);
    if (r & 0x01) {
        r = (r + 1) / 2;
    } else {
        r = -(r / 2);
    }
    return r;
}

#endif /* _H264_BS_H_ */
