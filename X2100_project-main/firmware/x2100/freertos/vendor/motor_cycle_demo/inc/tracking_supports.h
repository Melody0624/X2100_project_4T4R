/**
 * @file        tracking_supports.h
 * @brief       跟踪支撑模块接口。提供航迹-检测点关联判定、关联后处理聚类、
 *              Kalman 测量向量提取、CV 模型航迹状态更新及航迹得分/尺寸更新等函数的声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef TRACKING_SUPPORTS_H
#define TRACKING_SUPPORTS_H

#include "radar_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLAG_EDGE		1
#define FLAG_OUTER		2
	// 航迹-检测点关联主入口
	void track_and_detection_association(GlbCtx* ctx, TrkAssocInfo* info, uint32_t detIdx, int trkIdx);
	// 航迹关联后处理:航迹关联检测点信息记录
	void record_det_info_to_track(GlbCtx* glbCtx, TrkAssocRecInfo* trkAssocRecInfo, uint32_t detIdx, int trkIdx);
	// 航迹关联后处理：检测点聚类
	void track_association_postprocessing_clustering(GlbCtx* glbCtx, int trkIdx, TrkAssocRecInfo* trkAssocRecInfo, int pool_index);
	// 航迹关联后处理：卡尔曼位置测量值计算
	void get_kalman_posn_meas(GlbCtx* glbCtx, TrkAssocRecInfo* trkAssocRecInfo, int trkIdx);
	// 更新航迹得分
	void update_track_score(GlbCtx* ctx, int trkIdx);
	// 计算噪声协方差矩阵 R
	void cal_R_matrix(TrkObj* trkObj, int measMatLen, float* RMat, int pool_index);
	// 状态更新
	void kalman_update(float* xPred, float* PMat, float* HMat, float* ZVec,
		float* RMat, int stateLen, int measLen, float* xUpdate, int pool_index);
	// 更新后处理
	void update_post_processing(GlbCtx* ctx, int trkIdx, float* xUpdate, float* PMat, bool isHeaderToNew, int pool_index);
	// 更新航迹航向角
	void update_track_heading(GlbCtx* ctx, int trkIdx);
	// 更新航迹尺寸 
	void update_track_box(GlbCtx* ctx, int trkIdx);
#ifdef __cplusplus
}
#endif

#endif // TRACKING_SUPPORTS_H
