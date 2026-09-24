#ifndef _NOTCH_FILTER_H_
#define _NOTCH_FILTER_H_

#ifdef __cplusplus
extern "C" {
#endif

struct notch_filter;

struct notch_filter *notch_filter_init(int rate, float Q, float *freq, int freq_size);
short notch_filter_process(struct notch_filter *nf, short sample);
void notch_filter_destroy(struct notch_filter *nf);

#ifdef __cplusplus
}
#endif

#endif




