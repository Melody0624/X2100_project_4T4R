/**
 * @file        cfar_detection.c
 * @brief       CFAR 检测包装函数。调用 cfar_func_v2 执行 2D CFAR 检测，
 *              并将检测结果（距离/速度索引、SNR、峰值标志）填充至 DetInfoRD 结构体。
 * @author      Kan Fu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "radar_functions.h"
#include "memory_pool.h"

// 包装函数，用于CFARDetection
void cfar_detection(GlbCtx* ctx, const float* RDMap, int numRngBins, int numDoppBinsPerSubBand, unsigned int* maxMetricIndices, int pool_index) {
    // 分配输出矩阵
    // 使用全局内存池中的缓冲区
    int* det_flag_mat = g_memoryPool[pool_index].cfar_det_flag_mat;
    float* range_cfar_th_mat = g_memoryPool[pool_index].cfar_range_th_mat;
    float* doppler_cfar_th_mat = g_memoryPool[pool_index].cfar_doppler_th_mat;
    int* isPeak_flag_mat = g_memoryPool[pool_index].cfar_isPeak_flag_mat;
    int* is_peak_mat_rng = g_memoryPool[pool_index].cfar_is_peak_mat_rng;
    
    // 检查内存池是否已初始化
    if (!g_memoryPool[pool_index].initialized)
    {
        fprintf(stderr, "cfar_detection: 内存池未初始化\n");
        return;
    }

    // call cfar function
    cfar_func_v2(ctx, RDMap, numRngBins, numDoppBinsPerSubBand, det_flag_mat, range_cfar_th_mat, doppler_cfar_th_mat,
                 isPeak_flag_mat, is_peak_mat_rng, pool_index);

    // 将检测结果填充到glbCtx.detInfoRD
    unsigned int numTotalSubBands = ctx->basic_params.numTxs + ctx->wave_params.numEmptyBands;
    int NFFT_doppler = numDoppBinsPerSubBand * numTotalSubBands;
    int detCount = 0;
    for (int rngIdx = 0; rngIdx < numRngBins; rngIdx++) {
        for (int dopIdx = 0; dopIdx < numDoppBinsPerSubBand; dopIdx ++) {
            if (det_flag_mat[dopIdx * numRngBins + rngIdx]) {
                // 计算SNR
                float snr = RDMap[dopIdx * numRngBins + rngIdx] - doppler_cfar_th_mat[dopIdx * numRngBins + rngIdx] + ctx->dopplerCfarCfg.thold;

                // DDMA多普勒解模糊
                int dopBinIdx_ddmaDec = dopIdx + numDoppBinsPerSubBand * (maxMetricIndices[dopIdx * numRngBins + rngIdx]);
                // 处理速度模糊的临时方案，分配150kph给远离目标，剩下120kph给靠近目标
                if ((float)dopBinIdx_ddmaDec * ctx->wave_params.dopRes > (160.0f / 3.6f)) { 
                    dopBinIdx_ddmaDec = dopBinIdx_ddmaDec - NFFT_doppler;
                }

                // 径向距离插值
                // 确定最大值相邻索引
                int rngIdxLeftIdx, rngIdxRightIdx;
                float rngIdxInterp;
                if (is_peak_mat_rng[dopIdx * numRngBins + rngIdx] && rngIdx > 0 && rngIdx < numRngBins-1) {
                    rngIdxLeftIdx = rngIdx - 1;
                    rngIdxRightIdx = rngIdx + 1;
                    // 计算准确的最大值
                    float y1 = RDMap[dopIdx * numRngBins + rngIdxLeftIdx];
                    float y2 = RDMap[dopIdx * numRngBins + rngIdx];
                    float y3 = RDMap[dopIdx * numRngBins + rngIdxRightIdx];
                    float denom = (y1 - 2 * y2 + y3);
                    if (fabsf(denom) < 1e-6f)
                        rngIdxInterp = (float)rngIdx;
                    else
                        rngIdxInterp = (float)rngIdx + 0.5f * (y1 - y3) / denom;
                }
                else {
                    rngIdxInterp = (float)rngIdx;
                }
					

                // 记录检测点
                if (detCount < MAX_DETECTIONS) {
                    ctx->detInfoRD.detObjRD[detCount].rngIdx = rngIdx;
                    ctx->detInfoRD.detObjRD[detCount].dopIdx = dopIdx;
                    ctx->detInfoRD.detObjRD[detCount].rngBinIdx = rngIdxInterp;
                    ctx->detInfoRD.detObjRD[detCount].dopBinIdx_dec = (float)dopBinIdx_ddmaDec;
                    ctx->detInfoRD.detObjRD[detCount].subbandIdx = maxMetricIndices[dopIdx * numRngBins + rngIdx];
                    ctx->detInfoRD.detObjRD[detCount].pwr = RDMap[dopIdx * numRngBins + rngIdx];
                    ctx->detInfoRD.detObjRD[detCount].snr = snr;
                    ctx->detInfoRD.detObjRD[detCount].isPeak = isPeak_flag_mat[dopIdx * numRngBins + rngIdx];
                    detCount++;
                }
            }
        }
    }
    ctx->detInfoRD.numDets = detCount;

}
