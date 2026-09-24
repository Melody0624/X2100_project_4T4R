#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265356
#define SQRT2 1.41421356237

#define MAX_RANK 10

struct notch_filter
{
    int coef_size;  //项式系数大小
    int pos_x;  // 输入数据缓冲区当前位置
    int pos_y;  // 输出数据缓冲区当前位置
    float data_x[MAX_RANK];  // 输入数据缓冲区
    float data_y[MAX_RANK];  // 输出数据缓冲区
    float *poly_coef_pos;  // 正多项式系数
    float *poly_coef_neg;  // 负多项式系数
};

void gen_notch_filter(float freq, float Q, float fs, float* a, float* b)
{
    float w0, bw;
    float gb, beta, gain;

    w0 = 2 * freq / fs;
    if (w0 > 1 || w0 < 0)
        return;

    bw = w0 / Q;

    bw = bw * PI;
    w0 *= PI;

    /* 计算-3dB衰减 */
    gb = 1 / SQRT2;
    beta = (sqrt(1.0 - gb * gb) / gb) * tan(bw / 2.0);
    gain = 1.0 / (1 + beta);

    a[0] = 1;
    a[1] = -2.0 * gain * cos(w0);
    a[2] = 2.0 * gain - 1.0;

    b[0] = gain;
    b[1] = gain * (-2.0 * cos(w0));
    b[2] = gain;
}

int polymul(float* poly_coef, float *y2, float *out, float* coef, int size)
{
    int tmp;
    int i, j;
    int new_size = size + 2;

    for(i = 0; i < 3; i++)
        y2[i]= coef[i];

    for(i = 3; i < new_size; i++)
        y2[i]=0;

    for(i = 0; i < new_size; i++) {
        if(i < size)
            tmp = i;
        else
            tmp = size - 1;

        out[i] = 0;
        for(j = 0; j <= tmp; j++)
            out[i] += poly_coef[j] * y2[i - j];
    }

    for (i = 0; i < new_size; i++)
        poly_coef[i] = out[i];

    return new_size;
}

struct notch_filter *notch_filter_init(int rate, float Q, float *freq, int freq_size)
{
    int i;
    float a[3], b[3];
    int a_size, b_size;
    float *coef_y2 = NULL;
    float *coef_out = NULL;
    struct notch_filter *nf;

    if (!freq || !freq_size)
        return NULL;

    nf = malloc(sizeof(struct notch_filter));
    if (!nf)
        return NULL;
    memset(nf, 0, sizeof(struct notch_filter));

    nf->coef_size = freq_size * 2 + 1;
    nf->pos_x = MAX_RANK - 1;
    nf->pos_y = MAX_RANK - 1;

    nf->poly_coef_neg = malloc(sizeof(float) * nf->coef_size);
    if (!nf->poly_coef_neg)
        goto err_free_mem;
    nf->poly_coef_neg[0] = 1.0;
    a_size = 1;

    nf->poly_coef_pos = malloc(sizeof(float) * nf->coef_size);
    if (!nf->poly_coef_pos)
        goto err_free_mem;
    nf->poly_coef_pos[0] = 1.0;
    b_size = 1;

    coef_y2 = malloc(sizeof(float) * nf->coef_size);
    if (!coef_y2)
        goto err_free_mem;

    coef_out = malloc(sizeof(float) * nf->coef_size);
    if (!coef_out)
        goto err_free_mem;

    for (i = 0; i < freq_size; i++) {
        gen_notch_filter(freq[i], Q, rate, a, b);
        a_size = polymul(nf->poly_coef_neg, coef_y2, coef_out, a, a_size);
        b_size = polymul(nf->poly_coef_pos, coef_y2, coef_out, b, b_size);
    }

    free(coef_y2);
    free(coef_out);

    return nf;

err_free_mem:
    if (coef_out)
        free(coef_out);

    if (coef_y2)
        free(coef_y2);

    if (nf->poly_coef_neg) {
        free(nf->poly_coef_neg);
        nf->poly_coef_neg = NULL;
    }

    if (nf->poly_coef_pos) {
        free(nf->poly_coef_pos);
        nf->poly_coef_pos = NULL;
    }

    free(nf);

    return NULL;
}

void notch_filter_destroy(struct notch_filter *nf)
{
    if (!nf)
        return;

    free(nf->poly_coef_neg);
    free(nf->poly_coef_pos);
    free(nf);
}

short notch_filter_process(struct notch_filter *nf, short sample)
{
    int i;
    short ret;
    float dot1=0;
    float dot2=0;

    if (!nf)
        return 0;

    nf->pos_x = (nf->pos_x - 1 + MAX_RANK) % MAX_RANK;
    nf->data_x[nf->pos_x] = sample;

    for (i = 0; i < nf->coef_size; i++)
        dot1 += nf->data_x[(i + nf->pos_x) % MAX_RANK] * nf->poly_coef_pos[i];
    for (i = 0; i < nf->coef_size - 1; i++)
        dot2 += nf->data_y[(i + nf->pos_y) % MAX_RANK] * nf->poly_coef_neg[i + 1];

    nf->pos_y = (nf->pos_y - 1 + MAX_RANK) % MAX_RANK;
    nf->data_y[nf->pos_y] = dot1 - dot2;

    if (nf->data_y[nf->pos_y] > 32767)
        ret = 32767;
    else if (nf->data_y[nf->pos_y] < -32768)
        ret = -32768;
    else
        ret = (short)nf->data_y[nf->pos_y];

    return ret;
}
