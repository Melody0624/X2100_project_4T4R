/**
 * @file        static_zone_management.h
 * @brief       静止目标带识别模块接口。基于多帧累积静止检测点，通过DBSCAN聚类+
 *              RANSAC直线拟合识别道路两侧护栏/隔离带等静态线性结构，
 *              用于抑制护栏反射产生的虚假航迹。
 *
 *              设计原则：
 *              - 仅处理近距离（<150m），一次直线模型
 *              - 高召回优先：宁可误标记，不可漏标记
 *              - 块状/条状分布判别：区分停止车辆与护栏
 *              - 最近距离优先关联：替代先来先占
 *
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-06-22
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef STATIC_ZONE_MANAGEMENT_H
#define STATIC_ZONE_MANAGEMENT_H

#include "radar_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 静止目标带管理主入口 (每帧调用)
 *
 * 处理流程:
 *   1. 静止点仓库更新(每帧): 去重入库、age递增、过期清理
 *   2. 每MAX_STATIC_STORAGE_PERIOD帧:
 *      a. 已有静止带更新: 点到直线关联 + Block RLS递推参数更新
 *      b. 新静止带发现: 未关联点DBSCAN聚类 + RANSAC直线拟合 + 形状判别
 *
 * @param ctx 全局上下文
 */
void static_zone_obj_management(GlbCtx* ctx, int pool_index);

/**
 * @brief 利用已知静止带标记当前帧检测点
 *
 * 遍历所有静止检测点，判断是否落入任一有效静止带范围。
 * 命中则设置 detObj[].isInStaticZone = true。
 *
 * @param ctx 全局上下文
 */
//void static_zone_mark_detections(GlbCtx* ctx);

/**
 * @brief 利用已知静止带终结落入其中的航迹
 *
 * 遍历所有有效航迹，判断是否落入任一有效静止带范围。
 * 命中则调用 delete_track() 删除该航迹。
 *
 * @param ctx 全局上下文
 */
//void static_zone_terminate_tracks(GlbCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* STATIC_ZONE_MANAGEMENT_H */
