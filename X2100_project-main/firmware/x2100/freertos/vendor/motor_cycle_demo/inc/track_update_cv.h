/**
 * @file        track_update_cv.h
 * @brief       航迹状态更新模块接口（CV 模型）。提供 Kalman 状态更新、航向角更新、
 *              运动状态判断、航迹得分更新、尺寸计算及 R 矩阵计算等函数声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */

/*
 * tracker_update_cv.h
 * 航迹状态更新模块 (CV模型)
 * 对应 MATLAB trackerUpdateCV.m
 */

#ifndef TRACKER_UPDATE_CV_H
#define TRACKER_UPDATE_CV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "radar_types.h"

/* 运动状态转换阈值 */
#define TH_MS_CNT_STATIC_TO_MOVE    6       /* 静止/停止转运动计数 */
#define TH_MS_CNT_MOVE_TO_STOP      4       /* 运动转停止计数 */

/* 航迹尺寸更新阈值 */
#define TH_LEN_MAX_BASIC            6.0f    /* 长度上限（航迹头）*/
#define TH_LEN_MAX_SMALL_VEH        6.0f    /* 长度上限（小车）*/
#define TH_LEN_MAX_LARGE_VEH        10.0f   /* 长度上限（大车）*/
#define TH_WID_MAX_VEH              2.0f    /* 宽度上限 */


/**
 * @brief 航迹状态更新 (CV模型)
 * @param ctx   全局上下文指针
 * @note  对应 MATLAB trackerUpdateCV 函数
 */
void track_update_CV(GlbCtx *ctx, int pool_index);

/**
 * @brief 更新航迹得分
 * @param ctx       全局上下文指针
 * @param trkIdx    航迹索引 (0-based)
 */
void update_track_score(GlbCtx *ctx, int trkIdx);

/**
 * @brief 测试函数 (与 MATLAB test_tracker_update_cv.m 对比)
 */

/**
 * @brief 计算测量噪声矩阵 R
 * @param trkObj       航迹对象指针
 * @param measMatLen   测量向量长度
 * @param RMat         输出的 R 矩阵 (row-major)
 */
void cal_R_matrix(TrkObj* trkObj, int measMatLen, float* RMat);

/**
 * @brief Kalman 状态更新
 * @param xPred     预测状态向量
 * @param PMat      协方差矩阵 (in-place 更新)
 * @param HMat      观测矩阵
 * @param ZVec      测量向量
 * @param RMat      测量噪声矩阵
 * @param stateLen  状态维度
 * @param measLen   测量维度
 * @param xUpdate   输出更新后的状态
 */
void kalman_update(float* xPred, float* PMat, float* HMat, float* ZVec,
                  float* RMat, int stateLen, int measLen, float* xUpdate, int pool_index);

/**
 * @brief 更新航迹航向角
 * @param ctx       全局上下文指针
 * @param trkIdx    航迹索引 (0-based)
 */
void update_track_heading(GlbCtx* ctx, int trkIdx);

/**
 * @brief 计算尺寸更新系数
 * @param trkObj     航迹对象指针
 * @param measVal    测量值
 * @param updateIntv 更新时间间隔
 * @param edgeMark   边标记 (EDGE_LEN_A/B, EDGE_WID_A/B)
 * @return           更新后的尺寸值
 */
float get_box_coeff(TrkObj* trkObj, float measVal, float updateIntv, int edgeMark);


#ifdef __cplusplus
}
#endif

#endif /* TRACKER_UPDATE_CV_H */
