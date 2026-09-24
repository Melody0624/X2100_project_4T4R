/**
 * @file        warning_detection.h
 * @brief       两轮车后向雷达预警模块接口。定义 WarnResult 结构体及 BSD/AOA/LCA/RCW
 *              四种预警类型的检测函数声明。受 WARNING_ENABLE 宏控制，具体预警策略
 *              （零行/新大洲本田）由 WARNING_PROFILE_HONDA 宏选择。
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
 * 具体预警策略由 WARNING_PROFILE_HONDA 宏选择（0=零行/LingXing，1=新大洲本田/honda），
 * 定义于 radar_types.h，默认 0。
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

#include "radar_types.h"    /* WARNING_ENABLE, TrkInfo, MAX_TRACKS */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <float.h>

/* ------------------------------------------------------------------ */
/*  常量定义                                                           */
/* ------------------------------------------------------------------ */
// #define MAX_WARN_IDS    32      /* 每类预警最多记录的 trkID 数量 */

/* ------------------------------------------------------------------ */
/*  WarnResult — 预警输出                                               */
/*  对应 MATLAB warningDetection 返回的 warnResult struct               */
/*                                                                     */
/*  各字段含义：                                                        */
/*    BSD_left/right  : 盲区警示（Blind Spot Detection）               */
/*    AOA_left/right  : 超车后警示（Approaching from Overtake）        */
/*    LCA_left/right  : 车道变更辅助（Lane Change Assist）             */
/*    RCW             : 正后方碰撞预警（Rear Collision Warning）       */
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
 * @brief  两轮车后向雷达预警
 *         对应 MATLAB warningDetection.m
 *
 *         坐标系（雷达坐标系 RCS）：
 *           x_output : 纵向距离，正值 = 雷达前方 = 车辆后方（后向雷达）
 *           y_output : 横向距离，正值 = 右侧，负值 = 左侧
 *
 *         速度说明：
 *           vx/vy_output 为地面绝对坐标系速度（非相对 ego）
 *           dvx = vx + egoVx（ego 前进方向为 RCS -x 方向）
 *           TTC 采 2D 最近点时刻：TTC = -(x·dvx + y·dvy) / (dvx²+dvy²)
 *
 *         预警区域宽度、纵向长度、TTC/速度门槛等具体数值依 WARNING_PROFILE_HONDA
 *         宏选择的预警策略而定（0=零行/LingXing，1=新大洲本田/honda），详见 .c 实现。
 *
 * @param trkInfo  航迹集合指针（来自 GlbCtx.trkInfo）
 * @param egoVx    ego 前进速度 (m/s, 正值 = 前进)
 * @return WarnResult 预警结果
 */
WarnResult warning_detection(const TrkInfo *trkInfo, float egoVx);

/*
* @brief 预警结果处理（示例：打印预警信息）
 * @param ctx 全局结构体，包含 trkInfo 和其他相关信息
 */
void handle_warnings(GlbCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* WARNING_DETECTION_H */
