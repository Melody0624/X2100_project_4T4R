#ifndef _INGENIC_AEC_H_
#define _INGENIC_AEC_H_

#ifdef __cplusplus
extern "C" {
#endif

int ingenic_apm_init(int sample_rate);
int ingenic_apm_set_far_frame(short *buf);
int ingenic_apm_set_near_frame(short *input, short *output);
void ingenic_apm_destroy(void);

#ifdef __cplusplus
}
#endif

#endif /* _INGENIC_AEC_H_ */
