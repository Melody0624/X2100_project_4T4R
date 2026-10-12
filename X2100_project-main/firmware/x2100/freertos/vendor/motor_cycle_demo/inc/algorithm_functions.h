/**
 * @file        algorithm_functions.h
 * @brief       算法工具函数接口。提供航迹ID管理、分数更新、运动状态判断、
 *              坐标旋转、FOV范围计算及自车信息获取等算法函数的声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef ALGFUNCS_H
#define ALGFUNCS_H

#include "radar_types.h"

 /* 阈值定义 */
// 一般阈值
extern const float th_fovEdgeInit_deg;		// 阈值：FOV边缘初始化角度（度）
extern const float th_FOVEdge_deg;			// 阈值：FOV边缘偏置（m）

extern const int th_maxAgeAsHeader_near;	// Header状态的最大Age
extern const int th_maxAgeAsHeader_far;		// Header状态的最大Age

extern const float th_len_max_basic;		// 长度上限（航迹头）
extern const float th_len_max_small_veh;	// 长度上限（小车）
extern const float th_len_max_large_veh;	// 长度上限（大车）
extern const float th_wid_max_veh;			// 宽度上限


#ifdef __cplusplus
extern "C" {
#endif
	// 获取航迹ID
	int get_track_ID(GlbCtx* ctx);
	// 获取航迹运动状态
	void get_track_motion_state(GlbCtx* ctx, int trackID, bool isHeaderToNew);
	// 删除航迹
	void delete_track(GlbCtx* ctx, int trkIdx);
	// 获取航迹最近边
	void get_closet_trk_edge(float x_rcs, float y_rcs, float heading_rcs_rad, int* refEdgeLen, int* refEdgeWid);
	// 获取聚类中心点(通过最近边)
	void get_ctr_by_edge(float* refPt_x_rcs, float* refPt_y_rcs,
		int refEdgeLen, int refEdgeWid, float* x_tcs_list, float* y_tcs_list, int numPoints,
		float boxLen, float boxWid, float heading_rcs_rad);
	// 计算主车车速（基于静态目标的 VLC 估计，后向雷达专用）
	float getEgoVlcByStatic(GlbCtx* glbCtx, int pool_index);
	// 获取自车信息
	void getEgoVehInfo(GlbCtx* ctx, int pool_index);
	// 计算自车运动帧间差异量
	void compute_ego_motion(GlbCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif // ALGFUNCS_H
