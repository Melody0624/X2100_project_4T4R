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

#ifdef __cplusplus
extern "C" {
#endif
	// 获取航迹ID
	int get_track_ID(GlbCtx* ctx);
	// 更新航迹分数
	void update_track_score(GlbCtx* ctx, int trackID);
	// 获取航迹运动状态
	void get_track_motion_state(GlbCtx* ctx, int trackID, bool isHeaderToNew);
	// 获取航迹参考边界
	//void get_reference_edge(GlbCtx* ctx, int trackID);
	// 删除航迹
	void delete_track(GlbCtx* ctx, int trkIdx);
	// 坐标系旋转
	void rotate_coordinates(float x_in, float y_in, float angle_rad, float* x_out, float* y_out);
	// 计算目标在雷达坐标系中的方位角覆盖范围
	void compute_FOV_range(float x_rcs, float y_rcs, float legnth_tcs,
		float width_tcs, float heading_rcs_rad, float* max_azm, float* min_azm);
	// 获取航迹最近边
	void get_closet_trk_edge(float x_rcs, float y_rcs, float heading_rcs_rad, int* refEdgeLen, int* refEdgeWid);
	// 计算主车（雷达处）速度矢量（基于静态目标的 VLC 估计，后向雷达专用）
	void getEgoVlcByStatic(GlbCtx* glbCtx, float* egoVx_out, float* egoVy_out, int pool_index);
	// 获取自车信息
	void getEgoVehInfo(GlbCtx* ctx, int pool_index);
	// 计算自车运动帧间差异量
	void compute_ego_motion(GlbCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif // ALGFUNCS_H
