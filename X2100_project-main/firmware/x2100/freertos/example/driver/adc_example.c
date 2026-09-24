#include <driver/adc.h>
#include <common.h>
#include <os.h>

/*
 * unit: mV
 * x1830  vref voltage [ 900 - 1800 ]
 * x1520  vref voltage [ 3300 ]
 */
#define VREF_VOLTAGE        1800

void adc_sample_data(void *data)
{
    int i;
    unsigned int val;

    adc_init();

    for (i = 0; i < 5; i++) {
        val = adc_read_data(0);
        printf("ADC sample voltage: %dmV\n", val * VREF_VOLTAGE / 1024);
    }

    adc_deinit();
}

void adc_test(void)
{
    thread_create("adc_sample_data", 2048, adc_sample_data, NULL);
}
