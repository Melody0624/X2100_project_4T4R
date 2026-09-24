#ifndef COMPLEX_ABS_F32_H
#define COMPLEX_ABS_F32_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
void complex_abs_f32_scalar(float *dst, const void *src, size_t count);
void complex_abs_f32_simd(float *dst, const void *src, size_t count);
#else
#include <complex.h>
void complex_abs_f32_scalar(float *dst, const float _Complex *src,
                            size_t count);
void complex_abs_f32_simd(float *dst, const float _Complex *src,
                          size_t count);
#endif

#ifdef __cplusplus
}
#endif

#endif
