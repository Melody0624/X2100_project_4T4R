#include <stdio.h>
#include <stdlib.h>
#include <af_resample/af_resample.h>
#include <include_bin.h>

static short *output2;

/* 单声道440-160000hz的扫频文件,文件采样率48000hz
 * 可以用audacity 生成
 */
INCBIN(input, "example/resource/扫频440-16000hz.wav");

void af_resample_test(void)
{
    printf("begain af_resample_test\n");

    af_resample_t *resample =
        af_resample_init(AF_S16LE, 1, 48000, 44100, 0);

    int ret;

    int outsize = af_resample_get_out_buf_size(resample, inputSize);
    output2 = malloc(outsize);

    ret = af_resample(resample, (char *)inputData, inputSize, (char *)output2, outsize);

    printf("end test: %d\n", ret);

    af_resample_uninit(resample);

    free(output2);
}
