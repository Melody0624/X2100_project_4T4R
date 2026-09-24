#include <stdio.h>
#include <driver/adc.h>

static int channels = (1<<0) | (1<<1) | (1<<2) | (1<<3);

void adc_cb(void)
{
    static int count = 100000;
    if (count++ == 100000) {
        printf("adc: %d %d %d %d\n",
                adc_read_raw_channel_data(0), adc_read_raw_channel_data(1),
                adc_read_raw_channel_data(2), adc_read_raw_channel_data(3));
        count = 0;
    }

    adc_start_channels_sampling(channels);
}

void adc_channels_sampling_test(void)
{
    adc_init();

    adc_set_irq_cb(adc_cb);

    adc_start_channels_sampling(channels);
}
