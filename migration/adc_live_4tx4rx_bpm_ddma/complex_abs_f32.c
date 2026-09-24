#include "complex_abs_f32.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__mips_msa)
#include <msa.h>
#endif

void complex_abs_f32_scalar(float *dst, const float _Complex *src,
                            size_t count)
{
    if (!dst || !src)
        return;

    for (size_t i = 0; i < count; i++) {
        const float re = crealf(src[i]);
        const float im = cimagf(src[i]);
        dst[i] = sqrtf(re * re + im * im);
    }
}

static inline void complex_abs_f32_scalar_one(
    float *dst, const float *src_interleaved, size_t idx)
{
    const float re = src_interleaved[idx * 2u];
    const float im = src_interleaved[idx * 2u + 1u];
    dst[idx] = sqrtf(re * re + im * im);
}

void complex_abs_f32_simd(float *dst, const float _Complex *src,
                          size_t count)
{
    if (!dst || !src || count == 0)
        return;

    typedef char complex_float_must_be_2xf32[
        (sizeof(float _Complex) == 2u * sizeof(float)) ? 1 : -1];
    (void)sizeof(complex_float_must_be_2xf32);

    float *restrict dst_r = dst;
    const float *restrict src_f = (const float *)(const void *)src;

#if defined(__mips_msa)
    const size_t vec_width = 4;
    const size_t vec_blocks = count / vec_width;

    for (size_t blk = 0; blk < vec_blocks; blk++) {
        const size_t i = blk * vec_width;
        const float *p = src_f + i * 2u;
        const v4i32 v0 = __msa_ld_w((void *)p, 0);
        const v4i32 v1 = __msa_ld_w((void *)(p + 4), 0);
        const v4f32 re = (v4f32)__msa_pckev_w(v1, v0);
        const v4f32 im = (v4f32)__msa_pckod_w(v1, v0);
        const v4f32 sum = __msa_fadd_w(
            __msa_fmul_w(re, re), __msa_fmul_w(im, im));
        const v4f32 mag = __msa_fsqrt_w(sum);
        __msa_st_w((v4i32)mag, (void *)(dst_r + i), 0);
    }

    for (size_t i = vec_blocks * vec_width; i < count; i++)
        complex_abs_f32_scalar_one(dst_r, src_f, i);
#else
    for (size_t i = 0; i < count; i++)
        complex_abs_f32_scalar_one(dst_r, src_f, i);
#endif
}
