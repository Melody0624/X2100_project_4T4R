/**
 * @file        track_association.h
 * @brief       航迹-检测点关联模块接口。提供关联判定、关联后处理聚类、
 *              Kalman 测量向量提取等航迹关联相关函数的声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef TRACK_ASSOCIATION_H
#define TRACK_ASSOCIATION_H

#include "radar_types.h"
#include "general_functions.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLAG_EDGE		1
#define FLAG_OUTER		2

void track_and_detection_association(GlbCtx* ctx, TrkAssocInfo* info, int detIdx, int trkIdx);
void record_det_info_to_track(GlbCtx *glbCtx, TrkAssocRecInfo *trkAssocRecInfo, int detIdx, int trkIdx);
void track_association_postprocessing_clustering(GlbCtx* glbCtx, int trkIdx, TrkAssocRecInfo* trkAssocRecInfo);
void get_kalman_posn_meas(GlbCtx* glbCtx, TrkAssocRecInfo* trkAssocRecInfo, int trkIdx);

#ifdef __cplusplus
}
#endif

#endif // TRACK_ASSOCIATION_H
