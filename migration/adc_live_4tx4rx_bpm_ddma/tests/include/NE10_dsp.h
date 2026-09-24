#ifndef HOST_NE10_DSP_H
#define HOST_NE10_DSP_H
#include <stdint.h>
typedef int32_t ne10_int32_t;
typedef struct { float r, i; } ne10_fft_cpx_float32_t;
typedef struct host_fft_plan { unsigned int n; } *ne10_fft_r2c_cfg_float32_t;
ne10_fft_r2c_cfg_float32_t ne10_fft_alloc_r2c_float32(ne10_int32_t n);
void ne10_fft_c2c_1d_float32_mxu_ai(ne10_fft_cpx_float32_t *out,
    ne10_fft_cpx_float32_t *in, ne10_fft_r2c_cfg_float32_t cfg);
void fft_msa(float *in, float *out, ne10_fft_r2c_cfg_float32_t cfg);
#endif
