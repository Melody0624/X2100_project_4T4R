/**
 * @file        warning_detection.h
 * @brief       雷达预警模块接口。定义 WarnResult 结构体及预警检测函数声明：
 *              依安装角区分前向(FCW)/后向(RCW + LCA)，受 WARNING_ENABLE 宏控制。
 * @author      Jie Wu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */

/*
 * warning_detection.h
 *
 * 两轮车后向雷达预警模块（对应 MATLAB warningDetection.m）
 * 位于 header/，对应 MATLAB Func/warningFunc/ 子目录。
 *
 * 受 WARNING_ENABLE 宏控制（定义于 radar_types.h），默认关闭。
 */

#ifndef WARNING_DETECTION_H
#define WARNING_DETECTION_H

#include "radar_types.h"    /* WARNING_ENABLE, TrkInfo, MAX_TRACKERS */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <float.h>

/* ------------------------------------------------------------------ */
/*  常量定义                                                           */
/* ------------------------------------------------------------------ */
#define MAX_WARN_IDS    32      /* 每类预警最多记录的 trkID 数量 */

/* 预警模式（由安装角决定，单一雷达只输出一种碰撞预警） */
#define WARN_MODE_FCW   1       /* 前向安装 |φ| < 90°：前方碰撞预警 */
#define WARN_MODE_RCW   2       /* 后向安装 |φ| >= 90°：后方碰撞预警 + 左右 LCA */

/* ------------------------------------------------------------------ */
/*  WarnResult — 预警输出                                               */
/*                                                                     */
/*  各字段含义：                                                        */
/*    warnMode        : WARN_MODE_FCW / WARN_MODE_RCW                  */
/*    collision       : 正前方(FCW) / 正后方(RCW) 碰撞预警             */
/*    LCA_left/right  : 车道变更辅助（Lane Change Assist），仅 RCW 模式 */
/*    TTC_min         : 所有警示目标中最小 TTC (s)，无警示时为 FLT_MAX */
/*    *_IDs / *_cnt   : 触发各类预警的 trkID 列表及数量               */
/* ------------------------------------------------------------------ */
// typedef struct {
//     bool  BSD_left;
//     bool  BSD_right;
//     bool  AOA_left;
//     bool  AOA_right;
//     bool  LCA_left;
//     bool  LCA_right;
//     bool  RCW;
//     float TTC_min;

//     uint8_t BSD_left_IDs[MAX_WARN_IDS];
//     uint8_t BSD_left_cnt;
//     uint8_t BSD_right_IDs[MAX_WARN_IDS];
//     uint8_t BSD_right_cnt;
//     uint8_t AOA_left_IDs[MAX_WARN_IDS];
//     uint8_t AOA_left_cnt;
//     uint8_t AOA_right_IDs[MAX_WARN_IDS];
//     uint8_t AOA_right_cnt;
//     uint8_t LCA_left_IDs[MAX_WARN_IDS];
//     uint8_t LCA_left_cnt;
//     uint8_t LCA_right_IDs[MAX_WARN_IDS];
//     uint8_t LCA_right_cnt;
//     uint8_t RCW_IDs[MAX_WARN_IDS];
//     uint8_t RCW_cnt;

//     uint8_t LCA_left_level;  /* 0=无警示, 1=常亮(10-70m), 2=闪烁(0-10m) */
//     uint8_t LCA_right_level; /* 0=无警示, 1=常亮(10-70m), 2=闪烁(0-10m) */
// } WarnResult;

/**
 * @brief  雷达预警（依安装角选择 FCW 或 RCW + LCA）
 *
 *         坐标系（车体坐标系 VCS，已由 tracking_output 按安装角旋转）：
 *           x_output : 纵向距离，正值 = 车辆前方，负值 = 车辆后方
 *           y_output : 横向距离，正值 = 左侧，负值 = 右侧
 *
 *         速度说明：
 *           vx/vy_output 为地面绝对速度，车体坐标系，向前为正
 *           dvx = vx - egoVx：RCW 时 dvx > 0 为后方目标接近；FCW 时 dvx < 0 为前方目标接近
 *           TTC 取纵向最近边缘：RCW = -x_front_edge / dvx，FCW = x_rear_edge / (-dvx)
 *
 * @param trkInfo            航迹集合指针（来自 GlbCtx.trkInfo）
 * @param egoVx              ego 前进速度 (m/s, 正值 = 前进)
 * @param installAngComp_deg 雷达安装补偿角（度）：前向 0、后向 180、右侧 90、左侧 -90
 * @return WarnResult 预警结果
 */
WarnResult warning_detection(const TrkInfo *trkInfo, float egoVx, float installAngComp_deg);

/*
* @brief 预警结果处理（示例：打印预警信息）
 * @param ctx 全局结构体，包含 trkInfo 和其他相关信息
 */
void handle_warnings(GlbCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* WARNING_DETECTION_H */
