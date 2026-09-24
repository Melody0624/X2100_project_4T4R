#include <driver/adc.h>
#include <soc/adc.h>
#include <common.h>
#include <os.h>
#include <driver/systick.h>


#define SEQ0_CHANNEL_CNT 4

static struct adc_seq0_config seq0_adc = {
    .channel_cnt = SEQ0_CHANNEL_CNT,
    .channels = {0, 1, 2, 3},
    .irq_cb = NULL,
};

static void print_seq0_data(void)
{
    unsigned short values[SEQ0_CHANNEL_CNT];
    adc_read_seq0_data(values, SEQ0_CHANNEL_CNT);
    printf("adc data: %d %d %d %d\n", values[0],values[1],values[2],values[3]);
}

static void seq0_irq_cb(void)
{
    print_seq0_data();
}

void adc_test_seq0_irq(void)
{
    seq0_adc.irq_cb = seq0_irq_cb;
    adc_enable_seq0(&seq0_adc);

    adc_start_seq0();
}

void adc_test_seq0_poll(void)
{
    adc_enable_seq0(&seq0_adc);

    adc_start_seq0();

    while (!adc_poll_seq0_data_ready());

    print_seq0_data();
}


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
};

static void print_seq1_data(void)
{
    int len = SEQ1_CHANNEL_CNT;
    unsigned short values[SEQ1_CHANNEL_CNT];

    adc_read_seq1_data(values, len);

    printf("seq1: %08lld", systick_get_time_us());

    int i;
    for (i = 0; i < len; i++)
        printf(" %d:%d", values[i] >> 12, values[i] & 0xfff);
    printf("\n");
}

static void seq1_irq_cb(void)
{
    print_seq1_data();
}

void adc_test_seq1_irq(void)
{
    seq1_adc.irq_cb = seq1_irq_cb;
    adc_enable_seq1(&seq1_adc);

    adc_start_seq1();
}

void adc_test_seq1_poll(void)
{
    adc_enable_seq1(&seq1_adc);

    adc_start_seq1();

    while (1) {
        while (!adc_poll_seq1_data_ready());

        print_seq1_data();
    }
}


void adc_seq_test(void)
{
    adc_init();

    adc_test_seq1_irq();

    while (1);

    adc_deinit();
}
