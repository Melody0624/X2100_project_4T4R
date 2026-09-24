#ifndef NE10_H
#define NE10_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <NE10_dsp.h>
#include <NE10_types.h>
        void ne10_realfft(ne10_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg);
        void ne10_realifft(ne10_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg);
        ne10_fft_r2c_cfg_float32_t ne10_fft_alloc_r2c_float32 (ne10_int32_t nfft);
        void ne10_fft_destroy_r2c_float32 (ne10_fft_r2c_cfg_float32_t cfg);
#ifdef __cplusplus
}
#endif

#endif
