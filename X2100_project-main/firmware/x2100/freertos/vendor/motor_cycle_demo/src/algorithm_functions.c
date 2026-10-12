#include <math.h>
#include <string.h>
#include <float.h>
#include "radar_types.h"
#include "radar_functions.h"
#include "general_functions.h"

 /* 阈值定义 */
// 一般阈值
const float th_fovEdgeInit_deg = 45.0F;		// 阈值：FOV边缘初始化角度（度）	
const float th_FOVEdge_deg = 60.0F;			// 阈值：FOV边缘偏置（m）

/* 航迹尺寸更新阈值 */
const float th_len_max_basic = 6.0f;		// 长度上限（航迹头）
const float th_len_max_small_veh = 6.0f;	// 长度上限（小车）
const float th_len_max_large_veh = 10.0f;	// 长度上限（大车）
const float th_wid_max_veh = 2.0f;			// 宽度上限

// 动静属性判定
static const int th_motionStatusCnt_staticTomove = 6;
static const int th_motionStatusCnt_moveTostop = 4;

/**
 * @brief   获取一个可用的航迹 ID。
 * @details 从航迹 ID 管理器（栈式空闲表）中弹出一个空闲 ID 返回，并把该槽位
 *          标记为已占用（置 -1）、栈顶索引 topIdx 递增。
 * @param   ctx  全局上下文
 * @return  int
 *          - >= 0 ：可用航迹 ID（同时作为 trkObj 数组下标）
 *          - -1   ：无可用 ID（topIdx 已达 MAX_TRACKS）
 * @note    与 delete_track() 配对使用，遵循后进先出（LIFO）的 ID 回收策略。
 * @see     delete_track()
 */
int get_track_ID(GlbCtx* ctx) {
	TrackIDManager* mgr = &ctx->trackIDManager;
	if (mgr->topIdx >= MAX_TRACKS) {
		return -1;
	}
	else {
		int trackID = mgr->trackIDList[mgr->topIdx];
		mgr->trackIDList[mgr->topIdx] = -1;
		mgr->topIdx++;
		return trackID;
	}
}

/**
 * @brief   更新航迹优先级得分 pScore（依据 MATLAB updateTrackerScore 实现）。
 * @details 得分由三部分构成，用于航迹排序与关联优先级：
 *          1) 运动权重 mvCoeff：运动=1.0，停止=0.5，非航迹头的静止=0.25，其余=0；
 *          2) 距离权重：1 - rng/100（距雷达越近得分越高）；
 *          3) 年龄权重：pow(age/300, 1.6667)（存活越久得分越高）。
 * @param   ctx     全局上下文
 * @param   trkIdx  航迹索引（trkObj 数组下标）
 * @return  void
 * @note    结果写入 trkObj->pScore，得分越高在 sortTracks() 中越靠前。
 * @see     sortTracks()
 */
void update_track_score(GlbCtx* ctx, int trkIdx)
{
	TrkObj* trkObj = &ctx->trkInfo.trkObj[trkIdx];
	float mvCoeff = 0.0f;
	float rngCoeff, ageCoeff;

	if (trkObj->motion_state == MOTION_STATUS_MOVE) {
		mvCoeff = 1.0f;
	}
	else if (trkObj->motion_state == MOTION_STATUS_STOP) {
		mvCoeff = 0.5f;
	}
	else if (trkObj->motion_state == MOTION_STATUS_STATIC &&
		trkObj->track_state > TRACK_STATE_HEADER) {
		mvCoeff = 0.25f;
	}

	rngCoeff = trkObj->rng / 100.0f;
	ageCoeff = (float)trkObj->age / 300.0f;

	trkObj->pScore = mvCoeff + (1.0f - rngCoeff) + powf(ageCoeff, 1.6667f);
}

/**
 * @brief   更新航迹的运动状态（静止 / 停止 / 运动）。
 * @details 依据当前航迹速度幅值与阈值 th_vcsStatic 判定本帧运动属性，并通过
 *          转移计数器 MSTransCnt 做去抖：
 *          - isHeaderToNew == true（航迹头→新航迹首帧）：直接赋值，并按运动/静止
 *            将 MSTransCnt 初始化为 -2 / +2；
 *          - 否则：状态未变时计数向 0 收敛；状态改变时计数反向累加；当计数越过
 *            th_motionStatusCnt_staticTomove / th_motionStatusCnt_moveTostop 时
 *            完成迁移（静止/停止 ↔ 运动）。
 * @param   ctx           全局上下文
 * @param   trackID       航迹索引（trkObj 数组下标）
 * @param   isHeaderToNew 是否为“航迹头→新航迹”的首帧转换标识
 * @return  void
 * @note    更新 trkObj->motion_state 与 trkObj->MSTransCnt；静止/停止转运动时会
 *          参考 trkObj->assocStaticOnlyCnt（连续仅关联静止点的帧计数）。
 */
void get_track_motion_state(GlbCtx* ctx, int trackID, bool isHeaderToNew)
{
	TrkObj* trkObj = &ctx->trkInfo.trkObj[trackID];
	float trackerSpeed;
	int motionState;

	/* 计算当前帧的目标速度 */
	trackerSpeed = sqrtf(trkObj->vx_rcs * trkObj->vx_rcs + trkObj->vy_rcs * trkObj->vy_rcs);

	/* 确定当前帧的运动属性 */
	motionState = (trackerSpeed > th_vcsStatic) ? MOTION_STATUS_MOVE : MOTION_STATUS_STATIC;

	/* 更新航迹的运动属性 */
	if (isHeaderToNew) {
		/* 航迹头->新，直接赋值 */
		trkObj->motion_state = motionState;
		trkObj->MSTransCnt = (motionState == MOTION_STATUS_MOVE) ? -2 : 2;
	}
	else {
		/* 新、成熟，更新运动状态转移计数器 */
		if ((trkObj->motion_state == MOTION_STATUS_MOVE && motionState == MOTION_STATUS_MOVE) ||
			(trkObj->motion_state < MOTION_STATUS_MOVE && motionState < MOTION_STATUS_MOVE)) {
			/* 运动状态未变 */
			if (trkObj->MSTransCnt > 0) {
				trkObj->MSTransCnt = trkObj->MSTransCnt - 1;
			}
			else if (trkObj->MSTransCnt < 0) {
				trkObj->MSTransCnt = trkObj->MSTransCnt + 1;
			}
		}
		else {
			/* 运动状态改变 */
			if (trkObj->motion_state < MOTION_STATUS_MOVE) {
				/* 静止/停止转运动 */
				trkObj->MSTransCnt = (trkObj->MSTransCnt >= 0) ? trkObj->MSTransCnt + 1 : 0;
			}
			else {
				/* 运动转停止 */
				trkObj->MSTransCnt = (trkObj->MSTransCnt <= 0) ? trkObj->MSTransCnt - 1 : 0;
			}
		}

		/* 更新运动状态 */
		if (trkObj->motion_state < MOTION_STATUS_MOVE &&
			trkObj->MSTransCnt > th_motionStatusCnt_staticTomove)
		{
			/* 静止/停止转运动 */
			if (trkObj->assocStaticOnlyCnt >= th_motionStatusCnt_staticTomove) {
				trkObj->MSTransCnt = 0;
			}
			else {
				/* 静止/停止转运动 */
				trkObj->motion_state = MOTION_STATUS_MOVE;
				trkObj->MSTransCnt = -th_motionStatusCnt_staticTomove / 2;
			}
		}

		if (trkObj->motion_state == MOTION_STATUS_MOVE &&
			trkObj->MSTransCnt < -th_motionStatusCnt_moveTostop) {
			/* 运动转静止/停止 */
			trkObj->motion_state = MOTION_STATUS_STOP;
			trkObj->MSTransCnt = th_motionStatusCnt_moveTostop / 2;
		}
	}
}

/**
 * @brief   删除一条航迹并回收其 ID。
 * @details 清空该航迹对象数据（memset），将 trkIdx 压回航迹 ID 空闲表，并递减
 *          当前航迹总数 numTrks。
 * @param   ctx     全局上下文
 * @param   trkIdx  航迹索引（trkObj 数组下标）
 * @return  void
 * @note    仅当空闲表非空（topIdx > 0）时执行回收，否则不产生任何效果。
 * @warning 未校验航迹有效性：对同一条航迹重复删除会导致 numTrks 重复递减、
 *          ID 槽重复入栈，破坏 ID 管理器。调用方须确保传入的是有效且未被删除的航迹。
 */
void delete_track(GlbCtx* ctx, int trkIdx) {
	if (ctx->trackIDManager.topIdx > 0) {
		// 清空航迹数据
		memset(&ctx->trkInfo.trkObj[trkIdx], 0, sizeof(TrkObj));
		// 更新航迹ID管理器
		ctx->trackIDManager.topIdx--;
		ctx->trackIDManager.trackIDList[ctx->trackIDManager.topIdx] = trkIdx;
		// 更新航迹数量
		ctx->trkInfo.numTrks--;
	}
}

/**
 * @brief   坐标系旋转（原点始终为雷达，仅旋转坐标轴方向）。
 * @details 按旋转矩阵对点做坐标变换：
 *            x_out =  x_in*cos(θ) + y_in*sin(θ)
 *            y_out = -x_in*sin(θ) + y_in*cos(θ)
 * @param   x_in       输入 X 坐标
 * @param   y_in       输入 Y 坐标
 * @param   angle_rad  旋转角度（弧度）
 * @param   x_out      输出 X 坐标指针
 * @param   y_out      输出 Y 坐标指针
 * @return  void
 */
void rotate_coordinates(float x_in, float y_in, float angle_rad, float* x_out, float* y_out) {
	float cosA = cosf(angle_rad);
	float sinA = sinf(angle_rad);
	*x_out = x_in * cosA + y_in * sinA;
	*y_out = -x_in * sinA + y_in * cosA;
}

///*
//* @brief 计算目标在雷达坐标系中的方位角覆盖范围
//*/
//void compute_FOV_range(float x_rcs, float y_rcs, float length_tcs,
//	float width_tcs, float heading_rcs_rad, float* max_azm, float* min_azm) {
//	// 坐标系旋转（rcs->tcs）
//	float x_tcs, y_tcs;
//	rotate_coordinates(x_rcs, y_rcs, heading_rcs_rad, &x_tcs, &y_tcs);
//
//	// 计算目标四个角点在tcs中的坐标
//	float half_length = length_tcs * 0.5f;
//	float half_width = width_tcs * 0.5f;
//	float corners[4][2] = {
//		{x_tcs + half_length, y_tcs + half_width},	// 左前
//		{x_tcs + half_length, y_tcs - half_width},	// 右前
//		{x_tcs - half_length, y_tcs + half_width},	// 左后
//		{x_tcs - half_length, y_tcs - half_width}	// 右后
//	};
//
//	// 计算四个角点的方位角范围
//	*max_azm = -FLT_MAX;
//	*min_azm = FLT_MAX;
//	float azm;
//	for (int i = 0; i < 4; i++) {
//		azm = atan2f(corners[i][1], corners[i][0]);
//		*max_azm = (azm > *max_azm) ? azm : *max_azm;
//		*min_azm = (azm < *min_azm) ? azm : *min_azm;
//	}
//
//	// 坐标系转换（tcs->rcs）
//	*max_azm = *max_azm + heading_rcs_rad;
//	*min_azm = *min_azm + heading_rcs_rad;
//
//	// 角度修正
//	*max_azm = (*max_azm < -PI) ? (*max_azm + 2 * PI) : ((*max_azm > PI) ? (*max_azm - 2 * PI) : *max_azm);
//	*min_azm = (*min_azm < -PI) ? (*min_azm + 2 * PI) : ((*min_azm > PI) ? (*min_azm - 2 * PI) : *min_azm);
//	
//	// 角度转换为度
//	*max_azm = rad2deg(*max_azm);
//	*min_azm = rad2deg(*min_azm);
//}
//
/**
 * @brief   确定目标相对于雷达的最近参考边（长度边与宽度边）。
 * @details 先将目标位置由 RCS 旋转到以航向角为基准的 TCS，再按其在 TCS 中的相对
 *          象限判定朝向雷达的两条边（即“最近边”）：
 *            SectionA (±90°→0°)   ： LenB, WidB
 *            SectionB ( 90°→180°)  ： LenB, WidA
 *            SectionD ( 0°→-90°)   ： LenA, WidB
 *            SectionC (-90°→-180°) ： LenA, WidA
 *
 *               ^
 *               |
 *        A      |     D
 *    LenB,WidB  | LenA,WidB
 *               |
 * <-------------|-------------
 *               |
 *        B      |     C
 *    LenB,WidA  | LenA,WidA
 *
 * @param   x_rcs           目标 RCS-X 坐标 (m)
 * @param   y_rcs           目标 RCS-Y 坐标 (m)
 * @param   heading_rcs_rad 目标航向角（RCS，弧度）
 * @param   refEdgeLen      输出长度参考边（EDGE_LEN_A / EDGE_LEN_B）
 * @param   refEdgeWid      输出宽度参考边（EDGE_WID_A / EDGE_WID_B）
 * @return  void
 * @see     get_ctr_by_edge()
 */
void get_closet_trk_edge(float x_rcs, float y_rcs, float heading_rcs_rad, int* refEdgeLen, int* refEdgeWid) {
	// 坐标系转换（rcs->tcs）
	float x_tcs, y_tcs;
	rotate_coordinates(x_rcs, y_rcs, heading_rcs_rad, &x_tcs, &y_tcs);

	// 计算目标所在象限
	float relAngToRadar = atan2f(y_tcs, x_tcs);
	if (relAngToRadar >= 0 && relAngToRadar < PI_2)
	{
		// SectionA
		*refEdgeLen = EDGE_LEN_B;
		*refEdgeWid = EDGE_WID_B;
	}
	else if (relAngToRadar >= PI_2 && relAngToRadar <= PI)
	{
		// SectionB
		*refEdgeLen = EDGE_LEN_B;
		*refEdgeWid = EDGE_WID_A;
	}
	else if (relAngToRadar < 0 && relAngToRadar >= -PI_2)
	{
		// SectionD
		*refEdgeLen = EDGE_LEN_A;
		*refEdgeWid = EDGE_WID_B;
	}
	else
	{
		// SectionC
		*refEdgeLen = EDGE_LEN_A;
		*refEdgeWid = EDGE_WID_A;
	}
}

/**
 * @brief   通过最近边反推目标（聚类 / 航迹）的中心点坐标。
 * @details 先在 TCS 中按参考边取向取该方向最靠近雷达的边点（WidB / LenB 取最小值，
 *          WidA / LenA 取最大值），再沿边法向偏移半个框尺寸反推中心，最后旋转回
 *          RCS 输出。
 * @param   refPt_x_rcs     输出参考点 RCS-X 坐标 (m)
 * @param   refPt_y_rcs     输出参考点 RCS-Y 坐标 (m)
 * @param   refEdgeLen      输入长度参考边（EDGE_LEN_A / EDGE_LEN_B）
 * @param   refEdgeWid      输入宽度参考边（EDGE_WID_A / EDGE_WID_B）
 * @param   x_tcs_list      输入点集 X 坐标列表（TCS）
 * @param   y_tcs_list      输入点集 Y 坐标列表（TCS）
 * @param   numPoints       输入点数量（须 > 0）
 * @param   boxLen          输入目标长度（TCS, m）
 * @param   boxWid          输入目标宽度（TCS, m）
 * @param   heading_rcs_rad 输入目标航向角（RCS，弧度）
 * @return  void
 * @see     get_closet_trk_edge()
 */
void get_ctr_by_edge(float* refPt_x_rcs, float* refPt_y_rcs,
	int refEdgeLen, int refEdgeWid, float* x_tcs_list, float* y_tcs_list, int numPoints,
	float boxLen, float boxWid, float heading_rcs_rad)
{
	float cos_ang = cosf(heading_rcs_rad);
	float sin_ang = sinf(heading_rcs_rad);

	float cloest_x_tcs, cloest_y_tcs;
	if (refEdgeWid == EDGE_WID_B)
		cloest_x_tcs = array_min(x_tcs_list, numPoints);
	else
		cloest_x_tcs = array_max(x_tcs_list, numPoints);
	if (refEdgeLen == EDGE_LEN_B)
		cloest_y_tcs = array_min(y_tcs_list, numPoints);
	else
		cloest_y_tcs = array_max(y_tcs_list, numPoints);

	// 反推中心点
	float refPt_x_tcs, refPt_y_tcs;
	if (refEdgeWid == EDGE_WID_B)
		refPt_x_tcs = cloest_x_tcs + 0.5f * boxLen;
	else
		refPt_x_tcs = cloest_x_tcs - 0.5f * boxLen;
	if (refEdgeLen == EDGE_LEN_B)
		refPt_y_tcs = cloest_y_tcs + 0.5f * boxWid;
	else
		refPt_y_tcs = cloest_y_tcs - 0.5f * boxWid;

	// 坐标系转换
	*refPt_x_rcs = refPt_x_tcs * cos_ang - refPt_y_tcs * sin_ang;
	*refPt_y_rcs = refPt_x_tcs * sin_ang + refPt_y_tcs * cos_ang;
}

/**
 * @brief   获取 / 更新自车运动信息。
 * @details ADC 模式调用 getEgoVlcByStatic()，仅使用可靠解模糊点做 RANSAC；
 *          DAT_DETINFO 模式保留固件 TLV 中的自车速度，避免用已补偿 vlc 重复估计。
 *          横摆角速度与横滚角速度（及其前一帧值）暂置 0，预留 CAN 接口。
 * @param   ctx  全局上下文
 * @return  void
 * @note    当前未接入真实 CAN 信号，yawRate / rollRate 恒为 0，因此自车旋转补偿
 *          实际未启用，仅保留平移补偿路径。
 * @see     getEgoVlcByStatic(), compute_ego_motion()
 */
void getEgoVehInfo(GlbCtx* ctx, int pool_index)
{
#if (DOT_REPLAY == 0)
#if EGO_VLC_EST_ENABLE
	if (!ctx->radarInfo.isDetVlcPreCompensated) {
	    float egoSpd = getEgoVlcByStatic(ctx, pool_index);
#else
	if (!ctx->radarInfo.isDetVlcPreCompensated) {
		ctx->egoVehInfo.egoSpeed = 0.0f;		// 自车速度 (m/s) — 关闭 ego 估计
		ctx->egoVehInfo.isEgoSpeedValid = false;
	}
#endif // EGO_VLC_EST_ENABLE
#endif
		ctx->egoVehInfo.yawRate = 0.0f;			// 横摆角速度 (rad/s)
		ctx->egoVehInfo.yawRate_pre = 0.0f;		// 前一帧横摆角速度
		ctx->egoVehInfo.rollRate = 0.0f;		// 横滚角速度 (rad/s)
		ctx->egoVehInfo.rollRate_pre = 0.0f;	// 前一帧横滚角速度
    }  


}