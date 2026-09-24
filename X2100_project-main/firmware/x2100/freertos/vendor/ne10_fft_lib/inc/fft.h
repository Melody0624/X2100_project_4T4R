#ifndef __FFT_H__
#define __FFT_H__
#include <NE10_dsp.h>
#include <NE10_types.h>
#ifdef __cplusplus
class realfft{
  ne10_fft_r2c_cfg_float32_t cfg;
  int32_t nfft;
 public:

  realfft(int32_t nfft_):nfft(nfft_) {
    cfg = ne10_fft_alloc_r2c_float32 ((ne10_int32_t)nfft);
  }
  ~realfft(){
    ne10_fft_destroy_r2c_float32(cfg);
  }
  void fft(float *f,float* out) {
    ne10_fft_r2c_1d_float32_c ((ne10_fft_cpx_float32_t *)out,(ne10_float32_t*)f,cfg);
  }
  void ifft(float *f,float* out) {
    ne10_fft_c2r_1d_float32_c((ne10_float32_t *)out,(ne10_fft_cpx_float32_t*)f,cfg);
  }

  void fft_msa(float *f,float* out) {
    ne10_fft_r2c_1d_float32_mxu((ne10_fft_cpx_float32_t *)out,(ne10_float32_t*)f,cfg);
  }
  void ifft_msa(float *f,float* out) {
    ne10_fft_c2r_1d_float32_mxu((ne10_float32_t *)out,(ne10_fft_cpx_float32_t*)f,cfg);
  }
};

#else //CXX API
static inline void fft(float *f,float* out, ne10_fft_r2c_cfg_float32_t cfg) {
  ne10_fft_r2c_1d_float32_c ((ne10_fft_cpx_float32_t *)out,(ne10_float32_t*)f,cfg);
}
static inline void ifft(float *f,float* out, ne10_fft_r2c_cfg_float32_t cfg) {
  ne10_fft_c2r_1d_float32_c((ne10_float32_t *)out,(ne10_fft_cpx_float32_t*)f,cfg);
}

static inline void fft_msa(float *f,float* out, ne10_fft_r2c_cfg_float32_t cfg) {
    ne10_fft_r2c_1d_float32_mxu((ne10_fft_cpx_float32_t *)out,(ne10_float32_t*)f,cfg);
}
static inline void ifft_msa(float *f,float* out, ne10_fft_r2c_cfg_float32_t cfg) {
  ne10_fft_c2r_1d_float32_mxu((ne10_float32_t *)out,(ne10_fft_cpx_float32_t*)f,cfg);
}

void ne10_fft_c2c_1d_float32_mxu_ai(ne10_fft_cpx_float32_t *fout, ne10_fft_cpx_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg);
void ne10_fft_c2c_1d_inverse_float32_mxu_ai(ne10_fft_cpx_float32_t *fout, ne10_fft_cpx_float32_t *fin, ne10_fft_r2c_cfg_float32_t cfg);

#endif
#endif /* FFT_H */
