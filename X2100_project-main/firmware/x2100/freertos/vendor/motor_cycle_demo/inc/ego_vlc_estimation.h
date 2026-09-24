/**
 * @file        ego_vlc_estimation.h
 * @brief       自车速度估计模块接口。基于 RANSAC 算法，利用静态目标的径向速度
 *              估计自车速度，额外输出内点掩码(inlierMask)。受 EGO_VLC_ENABLE 宏控制。
 * @author      Jie Wu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */

/*
 * ego_vlc_estimation.h
 *
 * ego 速度估计模块（对应 MATLAB egoVlcEstimation.m）
 *
 * 与 vlc_estimation 的关系：
 *   两者共用同一底层 ransac_run()，仅参数不同：
 *     vlc_estimation    : maxDistance 自适应（≈0.1 m/s），内点比例 ≥ 60%
 *     ego_vlc_estimation: maxDistance 固定 0.5 m/s，  内点比例 ≥ 40%
 *   主要差别：ego 版本额外输出 inlierMask，供主循环标记参考点用。
 *
 * 受 EGO_VLC_ENABLE 宏控制（定义于 radar_types.h），默认关闭。
 */

#ifndef EGO_VLC_ESTIMATION_H
#define EGO_VLC_ESTIMATION_H

#include "radar_types.h" /* EGO_VLC_ENABLE, MAX_DETECTIONS */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  EgoVlcResult — ego 速度估计输出                                     */
/*  对应 MATLAB egoVlcEstimation 的四个输出：                           */
/*    isAvailable, vx_rcs, vy_rcs, inlierIdx                           */
/* ------------------------------------------------------------------ */
typedef struct {
    float vx_rcs;                       /* 雷达坐标系纵向速度 (m/s) */
    float vy_rcs;                       /* 雷达坐标系横向速度 (m/s) */
    bool  isAvailable;                  /* 估计结果是否有效 */
    bool  inlierMask[MAX_DETECTIONS];   /* 内点标志，索引与输入检测点对齐 */
    int   numInliers;
} EgoVlcResult;

    /**
     * @brief  ego 速度估计（后向雷达自测速专用）
     *         对应 MATLAB egoVlcEstimation.m
     *
     *         底层复用 ransac_run()，与 vlc_estimation 共用同一 RANSAC 核心，
     *         仅参数不同：
     *           maxDistance       = 0.5 m/s（固定，vlc_estimation 为自适应值）
     *           inlierRatioThresh = 0.4    （vlc_estimation 为 0.6）
     *
     * @param det_azm_deg  检测点方位角数组 (deg)，长度 numDets
     * @param det_radVlc   检测点径向速度数组 (m/s)，长度 numDets
     * @param numDets      检测点数量（上限 MAX_DETECTIONS）
     * @return EgoVlcResult 估计结果（含 inlierMask）
     */
    EgoVlcResult ego_vlc_estimation(const float *det_azm_deg, const float *det_radVlc,
                                    int numDets, int pool_index);

#ifdef __cplusplus
}
#endif

#endif /* EGO_VLC_ESTIMATION_H */
