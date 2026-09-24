/**
 * @file        vlc_core.h
 * @brief       VLC RANSAC 核心模块接口。提供 vlc_estimation 与 ego_vlc_estimation
 *              共用的 RANSAC 回调函数（拟合/距离/最小二乘精化）及共享数据结构 VlcCoreData。
 * @author      Jie Wu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */

/*
 * vlc_core.h
 *
 * vlc_estimation 与 ego_vlc_estimation 共用的 RANSAC 核心：
 *   - VlcCoreData  : 两者共用的数据结构（cos/sin/radVlc/numDets）
 *   - vlc_core_fitFunc  : 2 点拟合回调
 *   - vlc_core_distFunc : 距离计算回调
 *   - vlc_core_solve_lsq: 最小二乘精化
 *
 * 调用方（vlc_estimation.c / ego_vlc_estimation.c）负责：
 *   - 预计算 cos/sin 并填入 VlcCoreData
 *   - 设置各自的 maxDistance / inlierRatioThresh
 *   - 解析各自格式的返回结果
 */
#ifndef VLC_CORE_H
#define VLC_CORE_H

/*
 * vlc_core.h
 *
 * vlc_estimation 与 ego_vlc_estimation 共用的 RANSAC 核心：
 *   - VlcCoreData  : 两者共用的数据结构（cos/sin/radVlc/numDets）
 *   - vlc_core_fitFunc  : 2 点拟合回调
 *   - vlc_core_distFunc : 距离计算回调
 *   - vlc_core_solve_lsq: 最小二乘精化
 *
 * 调用方（vlc_estimation.c / ego_vlc_estimation.c）负责：
 *   - 预计算 cos/sin 并填入 VlcCoreData
 *   - 设置各自的 maxDistance / inlierRatioThresh
 *   - 解析各自格式的返回结果
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* -----------------------------------------------------------------------
 *  共用数据包（传递给 RANSAC 回调 userData）
 * --------------------------------------------------------------------- */
typedef struct {
    const float *cosVal;   /* cos(azimuth_rad) */
    const float *sinVal;   /* sin(azimuth_rad) */
    const float *radVlc;   /* 径向速度 */
    int          numDets;  /* 检测点数 */
} VlcCoreData;

/* -----------------------------------------------------------------------
 *  RANSAC 回调（可直接传给 ransac_run 的 fitFunc / distFunc 参数）
 * --------------------------------------------------------------------- */
bool  vlc_core_fitFunc (const void *data, int dataStride,
                        const int *indices, int numSamples,
                        float *model, void *userData);

float vlc_core_distFunc(const void *data, int dataStride,
                        int dataIndex, const float *model,
                        void *userData);

/* -----------------------------------------------------------------------
 *  最小二乘精化（用内点集重新拟合 vx/vy）
 * --------------------------------------------------------------------- */
void vlc_core_solve_lsq(const VlcCoreData *d, const bool *inliers,
                        float *vx, float *vy);

#ifdef __cplusplus
}
#endif

#endif /* VLC_CORE_H */
