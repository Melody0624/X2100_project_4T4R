#ifndef _SOC_DTRNG_H_
#define _SOC_DTRNG_H_

void soc_dtrng_init(void);
unsigned int soc_dtrng_read_random_data(void);
void soc_dtrng_deinit(void);

#endif