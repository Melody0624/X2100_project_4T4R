/**
 * @file        static_zone_supports.h
 * @brief       静止目标带支持函数接口。提供静止点降采样、聚类、静止带重置、形状检查等常用工具的声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-08-18
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef STATICZONESUPPORTS_H
#define	STATICZONESUPPORTS_H


 /* 静止目标带识别相关阈值 */
extern const float th_downsample_tol;			// 降采样 相似性阈值
extern const float th_max_x_diff_update;		// 静止带更新时x外扩
extern const float th_max_assoc_dist;			// 静止带关联点到直线距离阈值

extern const int ransac_sample_size;			// RANSAC 样本点数
extern const int ransac_max_trials;				// RANSAC 最大迭代次数
extern const float ransac_max_distance;			// RANSAC 内点距离阈值
extern const float ransac_confidence;			// RANSAC 置信度

extern const float forget_coeff_rls;			// RLS 遗忘因子
extern const float ema_alpha_x_range;			// EMA 平滑纵向范围系数

extern const float th_min_longitudinal_span;	// 最小纵向跨度(m)
extern const float th_min_aspect_ratio;			// 纵/横最小比例
extern const float th_max_x_range_static_zone;	// 处理的最大纵向距离(m)
extern const float th_max_x_diff_mark;			// 静止带标记

/* 静止目标带支持函数接口 */
// 相似点降采样
void downsample_points(float* points, int* numPoints, float tol);
// DBSCAN聚类算法 
int static_zone_cluster_direct(const StaticDetInfo* repo, unsigned char* clusterIDs);
// 重置静止带对象
void reset_static_zone(StaticZoneObj* zone);
// 检查点集是否呈条状分布 
bool check_strip_shape(const float* points, int numPoints);
// 标记静止目标带的检测结果
void static_zone_mark_detections(GlbCtx* ctx);

#endif // !STATICZONESUPPORTS_H
