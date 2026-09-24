/**
 * @file        cfar_func_v2.c
 * @brief       2D CFAR 检测算法实现。支持 CA-CFAR/GO-CFAR 模式，在距离维和多普勒维
 *              分别进行 CFAR 检测，结合峰值检测输出检测标志矩阵。
 * @author      Kan Fu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include "radar_types.h"
#include "memory_pool.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// 辅助函数：获取循环窗口索引
static void get_window_indices(int start, int winlen, int numBins, int* indices) {
    for (int i = 0; i < winlen; i++) {
        indices[i] = (start + i) % numBins;
        if (indices[i] < 0) indices[i] += numBins;
    }
}

/// @brief Perform CFAR detection on the range-Doppler heatmap.
/// @param glbCtx  The pointer to global context structure.
/// @param rdmapDb The range-Doppler heatmap in dB. rdmapDb[numDopplerBin][numRangeBin]
/// @param numRangeBin The number of range bins.
/// @param numDopplerBin The number of Doppler bins.
/// @param det_flag_mat Output detection flags for each range-Doppler bin.
/// @param range_cfar_th_mat Output range threshold values.
/// @param doppler_cfar_th_mat Output Doppler threshold values.
/// @param isPeak_flag_mat Output peak flag for each range-Doppler bin.
/// @param is_peak_mat_rng Output peak flag in range dimension for each range-Doppler bin.
int cfar_func_v2(GlbCtx* glbCtx, const float *rdmapDb, int numRangeBins, int numDopplerBins,
    int*det_flag_mat, float *range_cfar_th_mat, float *doppler_cfar_th_mat, int*isPeak_flag_mat,
    int*is_peak_mat_rng, int pool_index)
{
    int status = 0;
    int *is_peak_mat_dop;
    int *doppler_detlines_vec;
    int *doppler_det_flag_mat;
    float noise_pwr;
    int* left_win = NULL;
    int* right_win = NULL;
    int* upper_win = NULL;
    int* lower_win = NULL;

    int rngCFARStartBinIdx;

    // 使用全局内存池中的缓冲区
    doppler_det_flag_mat = g_memoryPool[pool_index].cfar_doppler_det_flag_mat;
    is_peak_mat_dop = g_memoryPool[pool_index].cfar_is_peak_mat_dop;
    doppler_detlines_vec = g_memoryPool[pool_index].cfar_doppler_detlines_vec;
    // 检查内存池是否已初始化
    if (!g_memoryPool[pool_index].initialized)
    {
        status = -1;
        goto exit;
    }
    if ((det_flag_mat == NULL) || (range_cfar_th_mat == NULL) || (doppler_cfar_th_mat == NULL) || 
         (isPeak_flag_mat == NULL) || (is_peak_mat_rng == NULL) || (doppler_det_flag_mat == NULL) ||
        (is_peak_mat_dop == NULL) || (doppler_detlines_vec == NULL))
    {
        status = -1;
        goto exit;
    }

    left_win = g_memoryPool[pool_index].cfar_left_win;
    right_win = g_memoryPool[pool_index].cfar_right_win;
    upper_win = g_memoryPool[pool_index].cfar_upper_win;
    lower_win = g_memoryPool[pool_index].cfar_lower_win;
    if (left_win == NULL || right_win == NULL || upper_win == NULL || lower_win == NULL)
    {
        status = -1;
        goto exit;
    }
    
    memset((void *)det_flag_mat, 0, numRangeBins * numDopplerBins * sizeof(int));
    memset((void *)range_cfar_th_mat, 0, numRangeBins * numDopplerBins * sizeof(float));
    memset((void *)doppler_cfar_th_mat, 0, numRangeBins * numDopplerBins * sizeof(float));
    memset((void *)isPeak_flag_mat, 0, numRangeBins * numDopplerBins * sizeof(int));
    memset((void *)is_peak_mat_rng, 0, numRangeBins * numDopplerBins * sizeof(int));
    memset((void *)doppler_det_flag_mat, 0, numRangeBins * numDopplerBins * sizeof(int));
    memset((void *)is_peak_mat_dop, 0, numRangeBins * numDopplerBins * sizeof(int));
    memset((void *)doppler_detlines_vec, 0, numDopplerBins * sizeof(int));
  
   
    if(glbCtx->dopplerCfarCfg.winlen + glbCtx->dopplerCfarCfg.guardlen > numDopplerBins)
    {
        status = -2; // window length is larger than the Doppler bin size.
        goto exit;
    }


    rngCFARStartBinIdx = 0; // C索引从0开始

    // -------------------- Doppler CFAR --------------------
    if(glbCtx->dopplerCfarCfg.enable) 
    {
        int winlen = glbCtx->dopplerCfarCfg.winlen;
        int guardlen = glbCtx->dopplerCfarCfg.guardlen;
        int mode = glbCtx->dopplerCfarCfg.mode;
        float th_detection = glbCtx->dopplerCfarCfg.thold;

        for(int rngIdx = rngCFARStartBinIdx; rngIdx < numRangeBins; rngIdx ++) 
        {
            // 初始化左右窗口
            int left_start = (0 - guardlen - winlen + numDopplerBins) % numDopplerBins;
            int right_start = (0 + guardlen + 1) % numDopplerBins;
            get_window_indices(left_start, winlen, numDopplerBins, left_win);
            get_window_indices(right_start, winlen, numDopplerBins, right_win);

            float left_sum = 0, right_sum = 0;
            for(int i = 0; i < winlen; i ++)
            {
                left_sum += rdmapDb[left_win[i] * numRangeBins + rngIdx];  //[rngIdx][left_win[i]];
                right_sum += rdmapDb[right_win[i] * numRangeBins + rngIdx]; //[rngIdx][right_win[i]];
            }

            for(int dopIdx = 0; dopIdx < numDopplerBins; dopIdx ++)
            {
                int cur2dIdx = dopIdx * numRangeBins + rngIdx;
                noise_pwr = 0;
                if(mode == CFAR_MODE_CA) // CA
                {
                    noise_pwr = 0.5f*(left_sum + right_sum) / (float)winlen;
                } 
                else if(mode == CFAR_MODE_CAGO) // CAGO
                {
                    noise_pwr = fmaxf(left_sum, right_sum) / (float)winlen;
                } 
                else if(mode == CFAR_MODE_CASO) // CASO
                {
                    noise_pwr = fminf(left_sum, right_sum) / (float)winlen;
                }
                
                doppler_cfar_th_mat[cur2dIdx] = noise_pwr + th_detection;

                // 峰值判定
                int prev_dop = (dopIdx - 1 + numDopplerBins) % numDopplerBins;
                int next_dop = (dopIdx + 1) % numDopplerBins;
                int prev2dIdx = prev_dop * numRangeBins + rngIdx;
                int next2dIdx = next_dop * numRangeBins + rngIdx;
                bool is_peak = (rdmapDb[cur2dIdx] >= rdmapDb[prev2dIdx]) &&
                               (rdmapDb[cur2dIdx] > rdmapDb[next2dIdx]);
                is_peak_mat_dop[cur2dIdx] = is_peak;

                if(glbCtx->dopplerCfarCfg.isPeakdet)
                {
                    doppler_det_flag_mat[cur2dIdx] = (rdmapDb[cur2dIdx] > doppler_cfar_th_mat[cur2dIdx]) && is_peak;
                } else {
                    doppler_det_flag_mat[cur2dIdx] = (rdmapDb[cur2dIdx] > doppler_cfar_th_mat[cur2dIdx]);
                }

                if(!doppler_detlines_vec[dopIdx] && doppler_det_flag_mat[cur2dIdx])
                {
                    doppler_detlines_vec[dopIdx] = 1;
                }

                // 窗口更新

                left_sum = left_sum - rdmapDb[rngIdx + left_win[0] * numRangeBins] + \
                            rdmapDb[rngIdx + ((left_win[winlen-1] + 1) % numDopplerBins) * numRangeBins];
                right_sum = right_sum - rdmapDb[rngIdx + right_win[0] * numRangeBins] + \
                            rdmapDb[rngIdx + ((right_win[winlen-1] + 1) % numDopplerBins) * numRangeBins];

                for(int i = 0; i < winlen-1; i ++) {
                     left_win[i] = left_win[i + 1]; 
                     right_win[i] = right_win[i + 1]; 
                }
                left_win[winlen - 1] = (left_win[winlen - 2] + 1) % numDopplerBins;
                right_win[winlen - 1] = (right_win[winlen - 2] + 1) % numDopplerBins;
            }
        }
    }

    // -------------------- Range CFAR --------------------
    if(glbCtx->rangeCfarCfg.enable)
    {
        int winlen = glbCtx->rangeCfarCfg.winlen;
        int guardlen = glbCtx->rangeCfarCfg.guardlen;
        int mode = glbCtx->rangeCfarCfg.mode;
        float th_detection = glbCtx->rangeCfarCfg.thold;

        for(int dopLineIdx = 0; dopLineIdx < numDopplerBins; dopLineIdx ++)
        {
            if(!doppler_detlines_vec[dopLineIdx]) 
                continue;
            int upper_start = (0 - guardlen - winlen + numRangeBins) % numRangeBins;
            int lower_start = (0 + guardlen + 1) % numRangeBins;
            get_window_indices(upper_start, winlen, numRangeBins, upper_win);
            get_window_indices(lower_start, winlen, numRangeBins, lower_win);

            float upper_sum = 0,  lower_sum = 0;
            for(int i = 0; i < winlen; i ++)
            {
                upper_sum += rdmapDb[upper_win[i] + dopLineIdx * numRangeBins];
                lower_sum += rdmapDb[lower_win[i] + dopLineIdx * numRangeBins];
            }

            for(int rngIdx = 0; rngIdx < numRangeBins; rngIdx ++)
            {
                int cur2dIdx = dopLineIdx * numRangeBins + rngIdx;
                noise_pwr = 0;
                if(mode == CFAR_MODE_CA)  // CA
                {
                    noise_pwr = 0.5f*(upper_sum + lower_sum) / (float)winlen;
                } 
                else if(mode == CFAR_MODE_CAGO)  // CAGO
                {
                    noise_pwr = fmaxf(upper_sum, lower_sum) / (float)winlen;
                } 
                else if(mode == CFAR_MODE_CASO)  // CASO
                {
                    noise_pwr = fminf(upper_sum, lower_sum) / (float)winlen;
                }
                
                range_cfar_th_mat[cur2dIdx] = noise_pwr + th_detection;
                // 峰值判定
                bool is_peak = false;
                if(rngIdx>0 && rngIdx<numRangeBins-1)
                {
                    int prev_rng = rngIdx-1;
                    int next_rng = rngIdx+1;
                    is_peak = (rdmapDb[cur2dIdx] >= rdmapDb[prev_rng + dopLineIdx * numRangeBins]) &&
                              (rdmapDb[cur2dIdx] > rdmapDb[next_rng + dopLineIdx * numRangeBins]);
                    is_peak_mat_rng[cur2dIdx] = is_peak;
                }

                if(glbCtx->rangeCfarCfg.isPeakdet)
                {
                    det_flag_mat[cur2dIdx] = (rdmapDb[cur2dIdx]>range_cfar_th_mat[cur2dIdx]) && is_peak;
                } else 
                {
                    det_flag_mat[cur2dIdx] = (rdmapDb[cur2dIdx]>range_cfar_th_mat[cur2dIdx]);
                }

                // 更新窗口
                upper_sum = upper_sum - rdmapDb[upper_win[0] + dopLineIdx * numRangeBins]  + \
                            rdmapDb[(upper_win[winlen-1]+1)%numRangeBins + dopLineIdx * numRangeBins];
                lower_sum = lower_sum - rdmapDb[lower_win[0] + dopLineIdx * numRangeBins] + \
                            rdmapDb[(lower_win[winlen-1]+1)%numRangeBins + dopLineIdx * numRangeBins];

                for(int i = 0; i < winlen-1; i ++)
                { 
                    upper_win[i] = upper_win[i+1]; 
                    lower_win[i] = lower_win[i+1]; 
                }
                upper_win[winlen-1] = (upper_win[winlen-2] + 1) % numRangeBins;
                lower_win[winlen-1] = (lower_win[winlen-2] + 1) % numRangeBins;
            }
        }
    }

    // -------------------- Combine result --------------------
    if(glbCtx->rangeCfarCfg.enable)
    {
        for(int i = 0; i < numRangeBins; i ++)
            for(int j = 0; j < numDopplerBins; j++)
            {
                int cur2dIdx = i + j * numRangeBins;
                isPeak_flag_mat[cur2dIdx] = is_peak_mat_rng[cur2dIdx];
            }
                
    }
    if(glbCtx->dopplerCfarCfg.enable)
    {
        for(int i = 0; i < numRangeBins; i ++)
            for(int j = 0; j < numDopplerBins; j ++)
            {
                int cur2dIdx = i + j * numRangeBins;
                det_flag_mat[cur2dIdx] = det_flag_mat[cur2dIdx] & doppler_det_flag_mat[cur2dIdx];
                isPeak_flag_mat[cur2dIdx] = isPeak_flag_mat[cur2dIdx] & is_peak_mat_dop[cur2dIdx];
            }
    }

exit:

    return status;
}
