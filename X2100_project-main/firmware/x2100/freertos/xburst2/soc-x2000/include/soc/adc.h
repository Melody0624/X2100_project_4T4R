#ifndef _SOC_ADC_H_
#define _SOC_ADC_H_

int  soc_adc_read_data(unsigned int channel);
void soc_adc_init(void);
void soc_adc_deinit(void);

#endif
