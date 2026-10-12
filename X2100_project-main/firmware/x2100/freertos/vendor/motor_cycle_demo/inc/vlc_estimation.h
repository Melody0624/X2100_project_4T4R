/**
 * @file        vlc_estimation.h
 * @brief       目标速度估计模块接口。基于 RANSAC 算法，利用检测点的径向速度和方位角
 *              估计目标在雷达坐标系中的纵向(vx)和横向(vy)速度。
 * @author      Jie Wu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef VLC_ESTIMATION_H
#define VLC_ESTIMATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

	/* RANSAC 配置参数 */
#define VLC_RANSAC_SAMPLE_SIZE      2       // 最小样本点数
#define VLC_RANSAC_CONFIDENCE       0.99f   // 置信度 (99%)
#define VLC_RANSAC_MAX_NUM_TRIALS   999     // 最大迭代次数
#define VLC_MIN_DETS_REQUIRED       5       // 最小检测点数
#define VLC_MIN_INLIER_COUNT        3       // 最小内点数	
#define VLC_MIN_INLIER_RATIO        0.6f    // 最小内点比例
#define VLC_MIN_ANGLE_SPAN          5.0f    // 最小角度跨度 (度)

	/* 结构体定义 */
	typedef struct {
		float vx_rcs;						// 纵向速度 (m/s) 
		float vy_rcs;						// 横向速度 (m/s) 
		bool isAvailable;					// 是否有效 
		bool isMaxAttempts;					// 是否达到最大迭代次数
		int numInliers;						// 内点数量 (debug用)
	} VlcResult;

	typedef struct {
		const float* cosVal;				// cos(azimuth_rad)
		const float* sinVal;				// sin(azimuth_rad)
		const float* radVlc;				// 径向速度
		int          numDets;				// 检测点数
	} VlcCoreData;

	/* 函数声明 */
	// RANSAC 回调（可直接传给 ransac_run 的 fitFunc / distFunc 参数）
	bool vlc_core_fitFunc(const void* data, int dataStride, const int* indices, int numSamples,
		float* model, void* userData);

	float vlc_core_distFunc(const void* data, int dataStride, int dataIndex, const float* model,
		void* userData);

	// 最小二乘精化（用内点集重新拟合 vx/vy）
	void vlc_core_solve_lsq(const VlcCoreData* d, const bool* inliers,
		float* vx, float* vy);

	// 初始化 VLC 估计模块 (使用 C 标准库 rand)
	void vlc_estimation_init(unsigned int seed);

	// RANSAC 速度估计 (对应 Matlab vlc_estimation)
	// pool 为 scratch buffer 起始指针（用法参考 detection_processing）：
	// 内部临时数组直接按顺序从该地址划分，不释放，由调用方每帧从池基址重新使用
	VlcResult vlc_estimation(const float* det_azm_deg, const float* det_radVlc, int numDets,
		int pool_index);

#ifdef __cplusplus
}
#endif

#endif /* VLC_ESTIMATION_H */