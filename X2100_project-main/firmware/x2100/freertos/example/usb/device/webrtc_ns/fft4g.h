#ifndef _FFT4G_H_
#define _FFT4G_H_

#if defined(__cplusplus)
extern "C" {
#endif

void WebRtc_rdft(size_t n, int isgn, float *a, size_t *ip, float *w);

#if defined(__cplusplus)
}
#endif

#endif  // _FFT4G_H_
