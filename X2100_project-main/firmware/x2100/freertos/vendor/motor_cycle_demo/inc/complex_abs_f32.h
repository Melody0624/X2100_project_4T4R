#ifndef COMPLEX_ABS_F32_H
#define COMPLEX_ABS_F32_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compute magnitude for an array of complex float values:
 *   dst[i] = sqrt(re^2 + im^2)
 *
 * - complex_abs_f32_scalar: portable reference implementation.
 * - complex_abs_f32_simd:   SIMD when available (falls back to scalar).
 *
 * Note: The SIMD implementation assumes `float _Complex` is stored as two
 * consecutive `float` values {re, im}, which is the common ABI on GCC/Clang.
 */
#ifdef __cplusplus
void complex_abs_f32_scalar(float *dst, const void *src, size_t count);
void complex_abs_f32_simd(float *dst, const void *src, size_t count);
#else
#include <complex.h>
void complex_abs_f32_scalar(float *dst, const float _Complex *src, size_t count);
void complex_abs_f32_simd(float *dst, const float _Complex *src, size_t count);
#endif

#ifdef __cplusplus
}
#endif

#endif /* COMPLEX_ABS_F32_H */
