/**
 * @file        detection_post_processing.c
 * @brief       检测后处理模块。对原始检测点进行排序、安装角坐标旋转补偿、
 *              动/静态分类、大角度异常点剔除及弹跳点(Bouncing)剔除。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "radar_functions.h"
#include "general_functions.h"

static void sortDetectionsByRange(DetObj* obj, int num);
static void markBouncingDetections(GlbCtx* glbCtx, unsigned int* detType_mark);

void detection_post_processing(GlbCtx* glbCtx)
{
	int i;
	unsigned int detType_mark[MAX_DETECTIONS] = { 0 };

	/* ---------- 1. 按 range 排序 ---------- */
	sortDetectionsByRange(glbCtx->detInfo.detObj,
		glbCtx->detInfo.numDets);

	/* ---------- 2. 坐标系旋转 ---------- */
	float ang = deg2rad(glbCtx->radarInfo.installAngComp_deg);
	float cosA = cosf(ang);
	float sinA = sinf(ang);

	for (i = 0; i < glbCtx->detInfo.numDets; i++) {
		DetObj* detObj = &glbCtx->detInfo.detObj[i];

		detObj->x_output = detObj->x_rcs * cosA + detObj->y_rcs * sinA;
		detObj->y_output = detObj->y_rcs * cosA - detObj->x_rcs * sinA;
	}
#if (DOT_REPLAY == 0)
	/* DAT_DETINFO 回放时文件内的 vlc 已是韧体补偿后的对地速度（vlc_amb 才是原始径向），
	   重复补偿会把静态点推回 ≈ 车速、被误判为动态，故跳过 */
	if (!glbCtx->radarInfo.isDetVlcPreCompensated)
	{
		for (int detIdx = 0; detIdx < glbCtx->detInfo.numDets; detIdx++)
		{
			// 计算对地径向速度
			glbCtx->detInfo.detObj[detIdx].vlc = glbCtx->detInfo.detObj[detIdx].vlc +
				glbCtx->radarInfo.vx_radar_vcs * cosf(deg2rad(glbCtx->detInfo.detObj[detIdx].azm_deg + glbCtx->radarInfo.installAngComp_deg)) +
				glbCtx->radarInfo.vy_radar_vcs * sinf(deg2rad(glbCtx->detInfo.detObj[detIdx].azm_deg + glbCtx->radarInfo.installAngComp_deg));
		}
	}
#endif

	/* ---------- 4. 动静判断 ---------- */
	for (i = 0; i < glbCtx->detInfo.numDets; i++) {
		if (fabsf(glbCtx->detInfo.detObj[i].vlc) > th_vcsStatic)
			glbCtx->detInfo.detObj[i].motion_state = MOTION_STATUS_MOVE;
		else
			glbCtx->detInfo.detObj[i].motion_state = MOTION_STATUS_STATIC;

	}

	/* ---------- 5. 大角度剔除 (bit0) ---------- */
	for (i = 0; i < glbCtx->detInfo.numDets; i++) {
		if ((fabsf(glbCtx->detInfo.detObj[i].azm_deg) > 60.0f)) {
			detType_mark[i] |= 0x01;
		}
	}

	/* ---------- 6. 反射点判定 (bit1) ---------- */
	markBouncingDetections(glbCtx, detType_mark);

	/* ---------- 7. 近距离地面反射检测点 (bit3) ---------- */
	for (i = 0; i < glbCtx->detInfo.numDets; i++) {
		if (glbCtx->detInfo.detObj[i].rng < 5.0f &&
			fabsf(glbCtx->detInfo.detObj[i].azm_deg) < 45.0f &&
			glbCtx->detInfo.detObj[i].pwr < 120.0f) {
			detType_mark[i] |= 0x08;
		}
		else if (fabsf(glbCtx->detInfo.detObj[i].y_rcs) < 5.0f &&
			glbCtx->detInfo.detObj[i].x_rcs < 10.0f &&
			glbCtx->detInfo.detObj[i].pwr < 105.0f &&
			glbCtx->detInfo.detObj[i].motion_state == MOTION_STATUS_MOVE)
		{
			detType_mark[i] |= 0x04;
		}
	}

	/* ---------- 9. 自车车尾以及前两个bin的反射点判定 (bit4) ---------- */
	for (i = 0; i < glbCtx->detInfo.numDets; i++) {
		if ((glbCtx->detInfo.detObj[i].rng < 0.5f)) {
			detType_mark[i] |= 0x05;
		}
	}

	/* ---------- 8. 删除无效检测点 ---------- */
	unsigned int newCnt = 0;
	for (i = 0; i < glbCtx->detInfo.numDets; i++) {
		if (detType_mark[i] == 0) {
			glbCtx->detInfo.detObj[newCnt++] =
				glbCtx->detInfo.detObj[i];
		}
	}

	glbCtx->detInfo.numDets = newCnt;
}


static void sortDetectionsByRange(DetObj* obj, int num)
{
	int i, j;
	for (i = 0; i < num - 1; i++) {
		for (j = i + 1; j < num; j++) {
			if (obj[i].rng > obj[j].rng) {
				DetObj tmp = obj[i];
				obj[i] = obj[j];
				obj[j] = tmp;
			}
		}
	}
}


static void markBouncingDetections(GlbCtx* glbCtx,
	unsigned int* detType_mark)
{
	int numDets = glbCtx->detInfo.numDets;

	if (numDets == 0)
		return;
	for (int detIdx_A = 0; detIdx_A < numDets; detIdx_A++) {
		if (detType_mark[detIdx_A] != 0)
			continue;

		for (int detObj_B = detIdx_A + 1; detObj_B < numDets; detObj_B++) {
			float azm_diff = fabsf(glbCtx->detInfo.detObj[detIdx_A].azm_deg -
				glbCtx->detInfo.detObj[detObj_B].azm_deg);
			if (azm_diff > 2.0f)
				continue;
			for (int times = 2; times <= 3; times++) {
				/* 用 vlc_amb（补偿ego前的原始径向速度）做倍数比对：
				 * vlc 经过 ego 速度补偿后是加法偏移（vlc = vlc_amb + ego投影），
				 * 二次反射的倍数关系只在补偿前的原始多普勒上成立，
				 * 补偿后 detObj_B.vlc 不再等于 times * detIdx_A.vlc */
				float rng_diff = fabsf(glbCtx->detInfo.detObj[detObj_B].rng -
					times * glbCtx->detInfo.detObj[detIdx_A].rng);
				float vlc_diff = fabsf(glbCtx->detInfo.detObj[detObj_B].vlc_amb -
					times * glbCtx->detInfo.detObj[detIdx_A].vlc_amb);

				if ((rng_diff > 0 && rng_diff < 2.0f) &&
					(vlc_diff > 0 && vlc_diff < 1.0f)) {
					detType_mark[detObj_B] |= 0x02;
					break;
				}
			}
		}
	}
}
