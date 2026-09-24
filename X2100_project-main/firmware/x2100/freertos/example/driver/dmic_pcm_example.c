#include <stdio.h>
#include <driver/pcm.h>
#include <common.h>

static struct pcm_params capture_params = {
    .channels = 4,
    .pcm_data_fmt = pcm_fmt_S16LE,
    .pcm_sample_rate = pcm_rate_16000,
    .pcm_interface = pcm_interface_dmic,
};

#define LEN 8000

void dmic_pcm_test(void)
{
    struct pcm_device *dai = pcm_get("dmic-capture");

    capture_params.channels = 4;
    pcm_enable(dai, &capture_params);
    pcm_start(dai);

    while (1) {
        
        static unsigned short data[LEN][4];
        unsigned int value[4] = {0, 0, 0, 0};

        pcm_read_frame(dai, data, LEN);
        
        int i;
        for (i = 0; i < LEN; i++) {
            value[0] += data[i][0];
            value[1] += data[i][1];
            value[2] += data[i][2];
            value[3] += data[i][3];
        }

        /*
         * 按住 dmic0 dmic1 dmic2 dmic3 中的一个，
         * 可以看到其中一个通道的值在减少，这样就可以测试dmic和通道的对应关系
         * 以及dmic是否有焊接不良的问题
         */
        printf("%08d %08d %08d %08d\n", value[0]/LEN, value[1]/LEN, value[2]/LEN, value[3]/LEN);
    }

    pcm_stop(dai);
    pcm_disable(dai);
}
