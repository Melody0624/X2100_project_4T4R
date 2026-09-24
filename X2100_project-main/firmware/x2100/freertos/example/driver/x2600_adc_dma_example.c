#include <driver/adc.h>
#include <soc/adc.h>
#include <common.h>
#include <os.h>
#include <driver/systick.h>
#include <malloc.h>
#include <driver/cache.h>

static unsigned short *seq1_dma_buf;

#define SEQ1_CHANNEL_CNT 12

static struct adc_seq1_config seq1_adc = {
    .continus_clk_div = 30000,
    .delay_clk_div = 30000,
    .trigger = adc_trigger_software,
    .enable_channel_num = 1,
    .channel_cnt = SEQ1_CHANNEL_CNT,
    .channels = { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15},
    .channel_delays = {10,10,10,10,10,10,10,10,10,10,10,10,},
    .group_cnt = 1,
    .groups = {SEQ1_CHANNEL_CNT},
    .group_delays = {1000, },
    .irq_cb = NULL,
    .dma_mode = 1,
};

static void print_seq1_data(void)
{
    unsigned short values[seq1_adc.dma_size];
    int ssize = adc_dma_seq1_read_data(&seq1_adc, values);
    if (ssize <= 0)
        return ;

    int i;
    printf("seq1: %08lld=====", systick_get_time_us());
    for (i = 0; i < ssize / 2; i++)
        printf(" %02d:%04d", values[i] >> 12, values[i] & 0xfff);
    printf("\n");

    return ;
}

static void seq1_irq_cb(void)
{
    print_seq1_data();
}

void adc_dma_test_seq1_irq(void)
{
    seq1_adc.dma_size = 256;
    seq1_dma_buf = cache_align_malloc(seq1_adc.dma_size);
    assert(seq1_dma_buf);
    seq1_adc.dma_buf = seq1_dma_buf;

    seq1_adc.irq_cb = seq1_irq_cb;
    adc_enable_seq1(&seq1_adc);

    adc_start_seq1();

    while (1);

    adc_disable_seq1(&seq1_adc);
}

void adc_dma_test_seq1_poll(void)
{
    seq1_adc.dma_size = 256;
    seq1_dma_buf = cache_align_malloc(seq1_adc.dma_size);
    assert(seq1_dma_buf);
    seq1_adc.dma_buf = seq1_dma_buf;

    adc_enable_seq1(&seq1_adc);

    adc_start_seq1();

    while (1) {
        while (1) {
            if (adc_dma_seq1_get_readable_size(&seq1_adc) >= sizeof(unsigned short) * SEQ1_CHANNEL_CNT)
                break;
        }

        print_seq1_data();
    }

    adc_disable_seq1(&seq1_adc);
}

void adc_dma_test(void)
{
    adc_init();

    adc_dma_test_seq1_irq();
    while (1);

    adc_deinit();
}
