/**
 * @file        warning_detection.c
 * @brief       雷达预警模块实现。依安装角选择预警模式：后向输出 BSD/AOA/LCA/RCW，
 *              前向输出 collision(FCW，前方碰撞预警)，并计算最小 TTC。
 * @author      Jie Wu
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */

/*
 * warning_detection.c
 *
 * 雷达预警实现（源自两轮车后向雷达 MATLAB warningDetection.m，已按安装角通用化）
 *
 * 功能说明：
 *   依据航迹输出 trkInfo、ego 速度与安装角，判断以下预警并计算最小 TTC：
 *     前向安装 |φ| < 90°：
 *       collision : 正前方碰撞预警(FCW) — 正前方目标接近（|y| <= 0.8m，x <= 70m，TTC <= 6s）
 *     后向安装 |φ| >= 90°（与安装角通用化之前一致）：
 *       BSD : Blind Spot Detection   — 盲区（x <= 10m 且 ego 速度 >= 10km/h）
 *       AOA : Approaching Overtaken  — ego 已超越目标（dvx < 0）
 *       LCA : Lane Change Assist     — 斜后方来车（x <= 70m，靠近中且 TTC <= 6s）
 *       RCW : Rear Collision Warning — 正后方来车（|y| <= 0.8m，x <= 70m，TTC <= 6s）
 *   输出字段顺序与类型须与协议一致；无 warnMode 字段，上位机据 collision_cnt 是否为 0 区分前/后向。
 */

#define _CRT_SECURE_NO_WARNINGS

#include <math.h>
#include <string.h>
#include <float.h>
#include "radar_types.h"
#include "warning_detection.h"

WarnResult warning_detection(const TrkInfo* trkInfo, float egoVx, float installAngComp_deg)
{
    /* ---------- 可调参数 ----------
     * 经会议讨论调整：
     *   - 坐标系：RCS → VCS（车体坐标系，自车前方 +x，左侧 +y）
     *   - 速度：track 输出 vx/vy 已为对地速度（VCS 前向为正），无需再扣 ego
     *   - Y_LANE_MAX:   4.4 → 4.6（边缘抖动时减少切换）
     *   - Y_CENTER_MAX: 0.5 → 0.8（同上）
     *   - 区域判定考虑航迹宽高，使目标更早进入报警区域 */
    const float BSD_X_MAX = 10.0f;          /* BSD 区纵向范围，同时作为 LCA 闪烁(level 2)区 */
    const float LCA_X_MAX = 70.0f;          /* LCA / 碰撞预警纵向范围 */
    const float Y_CENTER_MAX = 0.8f;
    const float Y_LANE_MAX = 4.6f;
    const float TTC_THRESH = 6.0f;
    const float LCA_MIN_CLOSE_SPD = 1.0f;
    const float BSD_MIN_EGO_SPEED = 10.0f / 3.6f;  /* 10 km/h */
    const float STATIC_SPD_THRESH = 0.5f;
    /* ------------------------------------------------------------------ */

    WarnResult res;
    memset(&res, 0, sizeof(res));
    res.TTC_min = FLT_MAX;  /* 对应 MATLAB 的 inf */

    /* 安装角归一化到 (-180, 180]，|φ| < 90° 为前向（仅 collision/FCW），
       否则为后向（BSD/AOA/LCA/RCW，与通用化之前一致） */
    float ang = fmodf(installAngComp_deg, 360.0f);
    if (ang > 180.0f) ang -= 360.0f;
    else if (ang <= -180.0f) ang += 360.0f;
    bool isForward = fabsf(ang) < 90.0f;

    if (trkInfo->numTrks <= 0) return res;

    for (uint32_t i = 0; i < trkInfo->numTrks; i++) 
    {
        uint32_t trkID = trkInfo->trkPriList[i];
        const TrkObj* trk = &trkInfo->trkObj[trkID];

        /* 仅对成熟航迹报警，避免 NEW/HEADER 航迹触发预警但尚未确认 */
        if (trk->track_state < TRACK_STATE_MATURE) continue;

        float x = trk->x_output;  // 航迹相对车体坐标vcs，自车前方为正
        float y = trk->y_output;  // 航迹相对车体坐标vcs, 左侧为正
        float vx = trk->vx_output; // 航迹相对地面速度，前向为正
        float vy = trk->vy_output; // 航迹相对地面速度，左向为正
        float halfW = fminf(fabsf(trk->maxWid_tcs * 0.5f), 1.5f);
		float halfH = fminf(fabsf(trk->maxLen_tcs * 0.5f), 4.0f);
        float y_left_edge = y + halfW;  // 航迹左边缘
        float y_right_edge = y - halfW;  // 航迹右边缘
        float x_front_edge = x + halfH;  // 航迹前边缘
        float x_rear_edge = x - halfH;  // 航迹后边缘
        /* 前向模式仅处理车辆前方目标（x > 0），后向模式仅处理车辆后方目标（x < 0） */
        if (isForward ? (x <= 0.0f) : (x >= 0.0f)) continue;

        /* 过滤地面绝对静止目标
         * 修正前用 fabsf(vx) < 0.5，會漏掉同速跟車（vx≈0）、誤觸護欄（vx≈v_ego） */
        float v_target_fwd = vx;
        if (fabsf(v_target_fwd) < STATIC_SPD_THRESH && fabsf(vy) < STATIC_SPD_THRESH) continue;
        if (trk->motion_state == MOTION_STATUS_STATIC) continue;
        /* 对向的车辆不触发预警
         * 閾值 -0.5 m/s：明確反向才過濾，留 0.5 buffer 避免低速跟車噪聲誤判 */
        if (v_target_fwd < -0.5f) continue;

        /* vx_output 为对地速度：检测点 vlc 在后处理已补偿自车速度，航迹由此估出对地速度。
         * 纵向相对速度须扣除自车速度：dvx = vx - egoVx
         *   后向：dvx > 0 为后方目标靠近；前向：dvx < 0 为前方目标靠近 */
        float dvx = vx - egoVx;

        /* 纵向最近边缘距离（≥0 表示未重叠）与 1D 纵向靠近速度（正值 = 距离在缩短）
         *   后向：最近边缘为目标前边缘，距离 = -x_front_edge，靠近速度 = dvx
         *   前向：最近边缘为目标后边缘，距离 =  x_rear_edge， 靠近速度 = -dvx */
        float nearDist = isForward ? x_rear_edge : -x_front_edge;
        float closing_spd = isForward ? -dvx : dvx;

        /* 1D TTC：纵向碰撞时间 TTC = nearDist / closing_spd（仅用纵向分量）*/
        float TTC;
        if (closing_spd > 1e-6f) {
            TTC = nearDist / closing_spd;
        } else {
            TTC = FLT_MAX;
        }

        /* 横向区域判断（外侧含端点 ±4.6m，内侧 ±0.8m 归属 isCenter 避免双重触发）
         * 使用航迹边缘做重叠测试：目标 [y_right_edge, y_left_edge] 与区域有交集即触发，
         * 让车辆边缘首先触碰区域边界时即报警（更早进入），不必等中心点跨越阈值。*/
        bool isLeft   = (y > Y_CENTER_MAX) && (y_right_edge <= Y_LANE_MAX);  /* 0.8 < y <= 4.6 */
        bool isRight  = (y < -Y_CENTER_MAX) && (y_left_edge >= -Y_LANE_MAX);  /*  -4.6 <=  y < -0.8 */
        bool isCenter = (y >= -Y_CENTER_MAX) && (y <=  Y_CENTER_MAX); /* |y| <= 0.8 */

        uint8_t id = (uint8_t)trk->trkID;

        /* ---- BSD / AOA（仅后向模式）---- */
        if (!isForward && nearDist <= BSD_X_MAX && egoVx >= BSD_MIN_EGO_SPEED) {
            /* AOA：dvx < 0 表示目標在 -x 方向遠離（ego 超車中，ego 速度 > 目標速度） */
            if (isLeft) {
                res.BSD_left = true;
                if (res.BSD_left_cnt < MAX_WARN_IDS)
                    res.BSD_left_IDs[res.BSD_left_cnt++] = id;
                if (dvx < 0.0f) {
                    res.AOA_left = true;
                    if (res.AOA_left_cnt < MAX_WARN_IDS)
                        res.AOA_left_IDs[res.AOA_left_cnt++] = id;
                }
            }
            else if (isRight) {
                res.BSD_right = true;
                if (res.BSD_right_cnt < MAX_WARN_IDS)
                    res.BSD_right_IDs[res.BSD_right_cnt++] = id;
                if (dvx < 0.0f) {
                    res.AOA_right = true;
                    if (res.AOA_right_cnt < MAX_WARN_IDS)
                        res.AOA_right_IDs[res.AOA_right_cnt++] = id;
                }
            }
        }

        /* ---- LCA（仅后向模式）---- */
        if (!isForward &&
            nearDist <= LCA_X_MAX && closing_spd > LCA_MIN_CLOSE_SPD && TTC <= TTC_THRESH) {
            /* 0-10m：闪烁(level 2)；10-70m：常亮(level 1) */
            uint8_t lca_level = (nearDist <= BSD_X_MAX) ? 2 : 1;
            if (isLeft) {
                res.LCA_left = true;
                if (res.LCA_left_cnt < MAX_WARN_IDS)
                    res.LCA_left_IDs[res.LCA_left_cnt++] = id;
                if (TTC < res.TTC_min) res.TTC_min = TTC;
                if (lca_level > res.LCA_left_level) res.LCA_left_level = lca_level;
            }
            else if (isRight) {
                res.LCA_right = true;
                if (res.LCA_right_cnt < MAX_WARN_IDS)
                    res.LCA_right_IDs[res.LCA_right_cnt++] = id;
                if (TTC < res.TTC_min) res.TTC_min = TTC;
                if (lca_level > res.LCA_right_level) res.LCA_right_level = lca_level;
            }
        }

        /* ---- 碰撞预警：同一判定，方向由 nearDist/closing_spd 区分；
               前向写入 collision(FCW)，后向写入 RCW，上位机据 collision_cnt 区分模式 ---- */
        if (nearDist <= LCA_X_MAX && isCenter &&
            closing_spd > LCA_MIN_CLOSE_SPD && TTC <= TTC_THRESH) {
            if (isForward) {
                res.collision = true;
                if (res.collision_cnt < MAX_WARN_IDS)
                    res.collision_IDs[res.collision_cnt++] = id;
            }
            else {
                res.RCW = true;
                if (res.RCW_cnt < MAX_WARN_IDS)
                    res.RCW_IDs[res.RCW_cnt++] = id;
            }
            if (TTC < res.TTC_min) res.TTC_min = TTC;
        }
    }

    return res;
}


void handle_warnings(GlbCtx *ctx)
{
    if (ctx->trkInfo.numTrks != 0) {
        WarnResult warn = warning_detection(&ctx->trkInfo, ctx->egoVehInfo.egoSpeed,
            ctx->radarInfo.installAngComp_deg);
        if (warn.BSD_left || warn.BSD_right || warn.AOA_left || warn.AOA_right ||
            warn.LCA_left || warn.LCA_right || warn.RCW || warn.collision) {
            printf("[预警]");
            // if (warn.BSD_left) { printf(" BSD_L(trk:"); for (int i = 0; i < warn.BSD_left_cnt;  i++) printf("%d ", warn.BSD_left_IDs[i]);  printf(")"); }
            // if (warn.BSD_right) { printf(" BSD_R(trk:"); for (int i = 0; i < warn.BSD_right_cnt; i++) printf("%d ", warn.BSD_right_IDs[i]); printf(")"); }
            // if (warn.AOA_left) { printf(" AOA_L(trk:"); for (int i = 0; i < warn.AOA_left_cnt;  i++) printf("%d ", warn.AOA_left_IDs[i]);  printf(")"); }
            // if (warn.AOA_right) { printf(" AOA_R(trk:"); for (int i = 0; i < warn.AOA_right_cnt; i++) printf("%d ", warn.AOA_right_IDs[i]); printf(")"); }
            // if (warn.LCA_left) { printf(" LCA_L(trk:"); for (int i = 0; i < warn.LCA_left_cnt;  i++) printf("%d ", warn.LCA_left_IDs[i]);  printf(")"); }
            // if (warn.LCA_right) { printf(" LCA_R(trk:"); for (int i = 0; i < warn.LCA_right_cnt; i++) printf("%d ", warn.LCA_right_IDs[i]); printf(")"); }
            // if (warn.RCW) { printf(" RCW(trk:");   for (int i = 0; i < warn.RCW_cnt;       i++) printf("%d ", warn.RCW_IDs[i]);        printf(")"); }
            // if (warn.collision) { printf(" FCW(trk:"); for (int i = 0; i < warn.collision_cnt; i++) printf("%d ", warn.collision_IDs[i]); printf(")"); }
            float ttc = (warn.TTC_min < FLT_MAX * 0.5f) ? warn.TTC_min : -1.0f;
            warn.TTC_min = ttc;
            // printf(" TTC_min:%.2fs\n", ttc);
        }

        ctx->warnResult = warn;
    }
}

