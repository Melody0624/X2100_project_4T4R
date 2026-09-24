/**
 * @file        angle_estimation_v1.c
 * @brief       角度估计模块。对 CFAR 检测点执行方位 FFT 加抛物线插值，
 *              估计到达角(AoA)，支持 DDMA 解码后的虚拟天线阵列处理。
 * @author      Kan Fu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "radar_functions.h"
#include "general_functions.h"
#include "complex_abs_f32.h"
#include "memory_pool.h"

static float parabolic_interp(const float *x, int n, int k)
{
    int km1 = (k - 1 + n) % n;
    int kp1 = (k + 1) % n;

    float y1 = x[km1];
    float y2 = x[k];
    float y3 = x[kp1];

    float denom = (y1 - 2*y2 + y3);
    if (fabsf(denom) < 1e-6f)
        return (float)k;

    return k + 0.5f * (y1 - y3) / denom;
}

static void azim_fft_ne10(fft_cpx_f32* bv, const float* winAzimuth, int numVirtualAnts, int NFFT_azim, float* azimFFTMag, int pool_index) {
    // 使用全局内存池中的FFT配置结构体
    ne10_fft_r2c_cfg_float32_t cfg = g_memoryPool[pool_index].azim_fft_cfg;
    if (cfg == NULL) {
        fprintf(stderr, "azim FFT: NE10 FFT配置未初始化\n");
        return;
    }

    // 使用全局内存池中的输入输出缓冲区
    fft_cpx_f32 *in = g_memoryPool[pool_index].azim_fft_in;
    fft_cpx_f32 *out = g_memoryPool[pool_index].azim_fft_out;
    
    // 检查内存池是否已初始化
    if (!g_memoryPool[pool_index].initialized) {
        fprintf(stderr, "azim_fft_ne10: 内存池未初始化\n");
        return;
    }
    
    // 加窗
    for (int bvIdx = 0; bvIdx < numVirtualAnts; bvIdx ++) {
        in[bvIdx].r = (float)(bv[bvIdx].r * winAzimuth[bvIdx]);  // real part
        in[bvIdx].i = (float)(bv[bvIdx].i * winAzimuth[bvIdx]);  // imag part
    }

    // zero padding
    for (int bvIdx = numVirtualAnts; bvIdx < NFFT_azim; bvIdx ++) {
        in[bvIdx].r = 0.0f;
        in[bvIdx].i = 0.0f;
    }

    // 执行C2C FFT（正变换）
    ne10_fft_c2c_1d_float32_mxu_ai(out, in, cfg);

    // 存储幅度
    // 版本1（原型）
    // for (int d = 0; d < NFFT_azim; d ++) {
    //     float mag = (float)sqrtf(out[d].r * out[d].r + out[d].i * out[d].i);
    //     azimFFTMag[d] = mag;
    // }
    // 版本2（使用MSA批量优化）
    complex_abs_f32_simd(azimFFTMag, (complex float *)out, NFFT_azim);
}

void angle_estimation_v1(
    GlbCtx* glbCtx,
    const fft_cpx_f32* angCalib,   // calibration vector
    const float* winAzimuth,      // FFT window
    const fft_cpx_f32* dopplerFFT_res,  // 2dFFT result
    unsigned int* maxMetricIndices,
    int numRngBins,
    int numDoppBinsPerSubBand,
    int numVirtualAnts,
    int pool_index)
{
    int numDets = glbCtx->detInfoRD.numDets;
    // float d_azm = glbCtx->basic_params.minElemSpacing;
    unsigned int numTxs = glbCtx->basic_params.numTxs;
    unsigned int numRxs = glbCtx->basic_params.numRxs;
    unsigned int numTotalSubBands = numTxs + glbCtx->wave_params.numEmptyBands;
    unsigned int NFFT_doppler = numDoppBinsPerSubBand * numTotalSubBands;
    for (int detIdx = 0; detIdx < numDets; detIdx ++) 
    {

        DetObjRD *rd = &glbCtx->detInfoRD.detObjRD[detIdx];
        DetObj *out = &glbCtx->detInfo.detObj[detIdx];

        int rngIdx = rd->rngIdx;
        int dopIdx = rd->dopIdx;
        /* ---------- 取 BV ---------- */
		fft_cpx_f32* bv = g_memoryPool[pool_index].bv;
        unsigned int subBandIdx, dopBinIdx_ddmaDec_tx, idx, bv_idx, bv_logIdx;
        for (unsigned int rxIdx = 0; rxIdx < numRxs; rxIdx ++)
        {
            for (unsigned int txIdx = 0; txIdx < numTxs; txIdx++)
            {
                subBandIdx = maxMetricIndices[dopIdx * numRngBins + rngIdx] + txIdx;
                if (subBandIdx >= numTotalSubBands)
					subBandIdx -= numTotalSubBands;
                dopBinIdx_ddmaDec_tx = dopIdx + subBandIdx * numDoppBinsPerSubBand;
                idx = rxIdx * numRngBins * NFFT_doppler + dopBinIdx_ddmaDec_tx * numRngBins + rngIdx;
                bv_idx = txIdx * numRxs + rxIdx;
                bv_logIdx = glbCtx->radarInfo.logicIdx_reorder[bv_idx]; // 调整逻辑顺序
                bv[bv_idx].r = (float)(dopplerFFT_res[idx].r * angCalib[bv_idx].r - dopplerFFT_res[idx].i * angCalib[bv_idx].i); // calibration real
                bv[bv_idx].i = (float)(dopplerFFT_res[idx].r * angCalib[bv_idx].i + dopplerFFT_res[idx].i * angCalib[bv_idx].r); // calibration imag
            }
        }

        (void)bv_logIdx;

        /* ---------- FFT（占位） ---------- */
        float *azmFFT_abs = g_memoryPool[pool_index].azimFFTMag;

        /* TODO: FFT use library */
        memset(azmFFT_abs, 0, sizeof(float)*AZM_FFT_SIZE);

        azim_fft_ne10(bv, winAzimuth, numVirtualAnts, AZM_FFT_SIZE, azmFFT_abs, pool_index);

        /* ---------- Peak Search ---------- */
        int peakIdx = find_max_peak(azmFFT_abs, AZM_FFT_SIZE);
        // float peakAng = glbCtx->algInfo.angFFTvec[peakIdx];

        /* ---------- Interpolation ---------- */
        float azm_deg = parab_peak(glbCtx->algInfo.angFFTvec, azmFFT_abs, AZM_FFT_SIZE, peakIdx, 1);

        ///* ---------- Interpolation ---------- */
        // float peakInterp = parabolic_interp(azmFFT_abs, AZM_FFT_SIZE, peakIdx);

        // if (peakInterp > AZM_FFT_SIZE/2)
        //     peakInterp -= AZM_FFT_SIZE;

        ///* ---------- Angle Calculation ---------- */
        // float sin_theta = peakInterp / (AZM_FFT_SIZE * d_azm);
        // if (sin_theta > 1)
        //     sin_theta = 1;
        // if (sin_theta < -1)
        //     sin_theta = -1;
        // float azm_deg = rad2deg(asinf(sin_theta));

        /* ---------- Output ---------- */
        float cur_rng = glbCtx->wave_params.rngRes * rd->rngBinIdx;
        if (glbCtx->radarInfo.isInHighAlt)
            cur_rng = sqrtf(cur_rng * cur_rng - glbCtx->radarInfo.mountingHeightSqr);

        out->pwr = rd->pwr;
        out->snr = rd->snr;
        out->isPeak = rd->isPeak;

        out->rng = cur_rng;

        // azm_deg = azm_deg - 4.5f; // 适配零行的两层板偏移校正
        out->azm_deg = azm_deg;
        if (glbCtx->radarInfo.isFlipInstall_azm)
            out->azm_deg = -out->azm_deg;
            
        out->x_rcs = cur_rng * cosf(deg2rad(out->azm_deg));
        out->y_rcs = cur_rng * sinf(deg2rad(out->azm_deg));
        out->vlc_amb = glbCtx->wave_params.dopRes * rd->dopBinIdx_dec;
        out->vlc = out->vlc_amb;
        out->relRDIdx = detIdx;
    }

    glbCtx->detInfo.numDets = numDets;
}
