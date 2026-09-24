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

	/* RANSAC 配置参数 */
#define VLC_RANSAC_SAMPLE_SIZE      2       /* 最小样本点数 */
#define VLC_RANSAC_CONFIDENCE       0.99f   /* 置信度 (99%) */
#define VLC_RANSAC_MAX_NUM_TRIALS   999     /* 最大迭代次数 */
#define VLC_MIN_DETS_REQUIRED       5       /* 最小检测点数 */
#define VLC_MIN_INLIER_COUNT        3       /* 最小内点数 */
#define VLC_MIN_INLIER_RATIO        0.6f    /* 最小内点比例 */
#define VLC_MIN_ANGLE_SPAN          5.0f    /* 最小角度跨度 (度) */

/* 定义结果结构 */
	typedef struct {
		float vx_rcs;       /* 纵向速度 (m/s) */
		float vy_rcs;       /* 横向速度 (m/s) */
		bool isAvailable;   /* 是否有效 */
		bool isMaxAttempts; /* 是否达到最大迭代次数 */
		int numInliers;     /* 内点数量 (debug用) */
	} VlcResult;

	/**
	 * @brief 初始化 VLC 估计模块 (使用 C 标准库 rand)
	 * @param seed 随机数种子 (MATLAB 使用 666)
	 */
	void vlc_estimation_init(unsigned int seed);

	/**
	 * @brief 重置随机数序列索引 (每帧开始时调用，确保可重复性)
	 */
	//void vlcEstimation_resetRandomIndex(void);

	/**
	 * @brief RANSAC 速度估计 (对应 Matlab vlc_estimation)
	 * @param det_azm_deg   角度数组 (度)
	 * @param det_radVlc    径向速度数组 (m/s)
	 * @param numDets       点数
	 * @return VlcResult    估计结果
	 */
	VlcResult vlc_estimation(const float* det_azm_deg, const float* det_radVlc, int numDets, int pool_index);

#ifdef __cplusplus
}
#endif

#endif /* VLC_ESTIMATION_H */
