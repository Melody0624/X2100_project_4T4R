#include <math.h>
#include <string.h>
#include <float.h>
#include "radar_types.h"
#include "general_functions.h"
#include "ego_vlc_estimation.h"

/**
 * @brief 获取可用的航迹ID
 * @param ctx 全局上下文
 * @return 航迹ID，-1表示无可用ID
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
 * @brief 更新航迹得分（根据MATLAB updateTrackerScore实现）
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
 * @brief 获取航迹运动状态（根据MATLAB getTrackerMotionState实现）
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
			trkObj->MSTransCnt > thr_motionStatusCnt_staticTomove) 
		{
			/* 静止/停止转运动 */
			if (trkObj->assocStaticOnlyCnt >= thr_motionStatusCnt_staticTomove) {
				trkObj->MSTransCnt = 0;
			}
			else {
				/* 静止/停止转运动 */
				trkObj->motion_state = MOTION_STATUS_MOVE;
				trkObj->MSTransCnt = -thr_motionStatusCnt_staticTomove / 2;
			}
		}

		if (trkObj->motion_state == MOTION_STATUS_MOVE &&
			trkObj->MSTransCnt < -thr_motionStatusCnt_moveTostop) {
			/* 运动转静止/停止 */
			trkObj->motion_state = MOTION_STATUS_STOP;
			trkObj->MSTransCnt = thr_motionStatusCnt_moveTostop / 2;
		}
	}
}

/*
* @brief 删除航迹
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

/*
* @brief 坐标系旋转（原点始终为雷达，只旋转坐标轴方向）
*/
void rotate_coordinates(float x_in, float y_in, float angle_rad, float* x_out, float* y_out) {
	float cosA = cosf(angle_rad);
	float sinA = sinf(angle_rad);
	*x_out = x_in * cosA + y_in * sinA;
	*y_out = -x_in * sinA + y_in * cosA;
}

/*
* @brief 计算目标在雷达坐标系中的方位角覆盖范围
*/
void compute_FOV_range(float x_rcs, float y_rcs, float length_tcs,
	float width_tcs, float heading_rcs_rad, float* max_azm, float* min_azm) {
	// 坐标系旋转（rcs->tcs）
	float x_tcs, y_tcs;
	rotate_coordinates(x_rcs, y_rcs, heading_rcs_rad, &x_tcs, &y_tcs);

	// 计算目标四个角点在tcs中的坐标
	float half_length = length_tcs * 0.5f;
	float half_width = width_tcs * 0.5f;
	float corners[4][2] = {
		{x_tcs + half_length, y_tcs + half_width},	// 左前
		{x_tcs + half_length, y_tcs - half_width},	// 右前
		{x_tcs - half_length, y_tcs + half_width},	// 左后
		{x_tcs - half_length, y_tcs - half_width}	// 右后
	};

	// 计算四个角点的方位角范围
	*max_azm = -FLT_MAX;
	*min_azm = FLT_MAX;
	float azm;
	for (int i = 0; i < 4; i++) {
		azm = atan2f(corners[i][1], corners[i][0]);
		*max_azm = (azm > *max_azm) ? azm : *max_azm;
		*min_azm = (azm < *min_azm) ? azm : *min_azm;
	}

	// 坐标系转换（tcs->rcs）
	*max_azm = *max_azm + heading_rcs_rad;
	*min_azm = *min_azm + heading_rcs_rad;

	// 角度修正
	*max_azm = (*max_azm < -PI) ? (*max_azm + 2 * PI) : ((*max_azm > PI) ? (*max_azm - 2 * PI) : *max_azm);
	*min_azm = (*min_azm < -PI) ? (*min_azm + 2 * PI) : ((*min_azm > PI) ? (*min_azm - 2 * PI) : *min_azm);
	
	// 角度转换为度
	*max_azm = rad2deg(*max_azm);
	*min_azm = rad2deg(*min_azm);
}

/*
* @brief 获取航迹最近边
*              ^
*              |
*       A      |     D
*   LenB,WidB  | LenA,WidB
*              |
* -------------|-------------
*              |
*       B      |     C
*   LenB,WidA  | LenA,WidA
*              |
*
*/
void get_closet_trk_edge(float x_rcs, float y_rcs, float heading_rcs_rad, int *refEdgeLen, int *refEdgeWid) {
	// 坐标系转换（rcs->tcs）
	float x_tcs, y_tcs;
	rotate_coordinates(x_rcs, y_rcs, heading_rcs_rad, &x_tcs, &y_tcs);

	// 计算目标所在象限
	float relAngToRadar =  atan2f(y_tcs, x_tcs);
	if (relAngToRadar >=0 && relAngToRadar < PI_2)
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

/*
* @brief 计算主车（雷达处）速度矢量（基于静态目标的 VLC 估计，后向雷达专用）
*        同时输出纵向 (egoVx_out) 与横向 (egoVy_out) 分量。
*        转弯时雷达处会产生横向/切向速度，若只取纵向分量，静止目标（护栏、墙面、
*        行道树等偏离正前/正后方的静止物）的都卜勒残留将无法被扣除，进而被
*        误判为动态目标——此为后向碰撞预警误报的根因之一，故两分量都必须输出。
*/
void getEgoVlcByStatic(GlbCtx* glbCtx, float* egoVx_out, float* egoVy_out, int pool_index)
{

	float egoVx_smooth = glbCtx->egoVehInfo.egoSpeed;      /* EMA 平滑，跨帧持续（对应 MATLAB egoVx_smooth = 0）*/
	float egoVy_smooth = glbCtx->egoVehInfo.egoSpeedLat;   /* EMA 平滑，跨帧持续 */

	const float EGO_EMA_ALPHA = 0.6f;
	const float EGO_VY_ABS_LIMIT = 2.0f;       /* 橫向速度限幅：超過視為 RANSAC 尖峰，不更新 lateral */
	const int EGO_VY_MIN_INLIERS = 8;          /* lateral 更新需更多內點，避免少量點幾何退化造成大 vy */
	int numDets = glbCtx->detInfo.numDets;
	if (numDets >= 5) {
		float det_azm[MAX_DETECTIONS];
		float det_radVlc[MAX_DETECTIONS];
		int n = (numDets <= MAX_DETECTIONS) ? numDets : MAX_DETECTIONS;
		for (int i = 0; i < n; i++) {
			/* 用原始（未加安裝角）方位角擬合，維持與既有 vx_rcs 用法一致的慣例：
			   detection_post_processing.c 的補償公式用 (azm_deg + installAngComp_deg)
			   當作與 (vx_radar_vcs, vy_radar_vcs) 內積的角度。在 installAngComp_deg =
			   180° 時，cos/sin 的雙重反號恰好抵消，使得直接採用原始方位角擬合出的
			   (vx_rcs, vy_rcs) 可以不經旋轉、不取負直接當作 (egoSpeed, egoSpeedLat)
			   使用——這與原本 vx_rcs「無需取負」的既有慣例一致（見下方賦值）。
			   若未來安裝角不是 180°，此處與補償公式需要一併重新推導旋轉關係。 */
			det_azm[i] = glbCtx->detInfo.detObj[i].azm_deg;
			det_radVlc[i] = glbCtx->detInfo.detObj[i].vlc_amb;
		}
		EgoVlcResult egoRes = ego_vlc_estimation(det_azm, det_radVlc, n, pool_index);
		if (egoRes.isAvailable) {
			/* 靜態目標表觀速度朝 +x 方向（與 ego 前進方向同號），無需取負；橫向分量同理 */
			egoVx_smooth = EGO_EMA_ALPHA * egoRes.vx_rcs + (1.0f - EGO_EMA_ALPHA) * egoVx_smooth;
			/* vy 對角度分佈與少量離群點更敏感：超出合理上限或內點太少時沿用上一幀，
			   避免單幀尖峰污染 egoSpeedLat。 */
			if (fabsf(egoRes.vy_rcs) <= EGO_VY_ABS_LIMIT &&
				egoRes.numInliers >= EGO_VY_MIN_INLIERS) {
				egoVy_smooth = EGO_EMA_ALPHA * egoRes.vy_rcs + (1.0f - EGO_EMA_ALPHA) * egoVy_smooth;
			}
		}
	}
	*egoVx_out = egoVx_smooth;
	*egoVy_out = egoVy_smooth;
}

/*
* @brief 获取自车信息（示例值，实际应从CAN总线或其他接口获取）
* @param ctx 全局结构体
*/
void getEgoVehInfo(GlbCtx* ctx, int pool_index)
{
#if (DOT_REPLAY == 0)
#if EGO_VLC_ENABLE
	/* 用 RANSAC 重新估計雷達處速度矢量（覆蓋 parse_dat_detInfo 從 dat 讀入的韌體值） */
	getEgoVlcByStatic(ctx, &ctx->egoVehInfo.egoSpeed, &ctx->egoVehInfo.egoSpeedLat, pool_index);
#else
	ctx->egoVehInfo.egoSpeed = 0.0f;		// 自车速度 (m/s) — 關閉 ego 估計
	ctx->egoVehInfo.egoSpeedLat = 0.0f;	// 自车横向速度 (m/s) — 關閉 ego 估計
#endif // EGO_VLC_EST_ENABLE
#endif
	/* yawRate/rollRate 保留外部传感器读数，不在此处清零。当前未接入 IMU/CAN 时，
	   calloc 初始化值为 0，compute_ego_motion() 会走 yawRate 近零的简化分支；
	   egoSpeedLat 先保留估计与门控结果，但不接入雷达速度矢量计算。 */
}


/**
 * @brief 计算自车运动帧间差异量
 *
 * 根据自车速度和横摆角速度，使用自行车模型计算雷达速度矢量，
 * 通过梯形法积分计算帧间位移和角度变化，再从 VCS 坐标系转换到 RCS 坐标系。
 *
 * 算法来源：refs/ref.c 中的 radNVehInfoVerification() 函数
 *
 * @param ctx 全局上下文
 */
void compute_ego_motion(GlbCtx* ctx)
{
	EgoVehInfo* egoVehInfo = &ctx->egoVehInfo;
	RadarInfo* radarInfo = &ctx->radarInfo;
	float delta_T = ctx->wave_params.frame_period * (ctx->frmInfo.frmID - ctx->frmInfo.lastFrmID);
	float longOffset = ctx->radarInfo.longOffset;

	// 保存前一帧的雷达所在位置的速度（vcs坐标系下）
	radarInfo->vx_radar_vcs_pre = radarInfo->vx_radar_vcs;
	radarInfo->vy_radar_vcs_pre = radarInfo->vy_radar_vcs;

	// 计算雷达速度矢量（vcs坐标系下）
	if (fabsf(egoVehInfo->yawRate) < 0.00001f)
	{
		// 当自车横摆角速度近于0时，简化处理
		radarInfo->vx_radar_vcs = egoVehInfo->egoSpeed;
		radarInfo->vy_radar_vcs = 0.0f;
	}
	else
	{
		float R_vcs = egoVehInfo->egoSpeed / egoVehInfo->yawRate;				// 转弯半径（车速获取参考点）
		float R_rcs = sqrtf(R_vcs * R_vcs + longOffset * longOffset);			// 转弯半径（雷达中心参考点）
		float angDiff = atan2f(longOffset, R_vcs);								// 速度方向偏差角
		radarInfo->vx_radar_vcs = R_rcs * egoVehInfo->yawRate * cosf(angDiff);	// 主车坐标系下的雷达所在位置的速度矢量
		radarInfo->vy_radar_vcs = R_rcs * egoVehInfo->yawRate * sinf(angDiff);
	}

	// 计算帧间位移相关差异
	if (ctx->frmInfo.frmID > 0)
	{
		// VCS坐标系下雷达的位移的增量
		float dx_vcs = radarInfo->vx_radar_vcs_pre * delta_T +
			0.5f * (radarInfo->vx_radar_vcs - radarInfo->vx_radar_vcs_pre) * delta_T;
		float dy_vcs = radarInfo->vy_radar_vcs_pre * delta_T +
			0.5f * (radarInfo->vy_radar_vcs - radarInfo->vy_radar_vcs_pre) * delta_T;

		// (VCS → RCS) RCS坐标系下雷达的位移增量（考虑安装角补偿） 
		float cosYaw = cosf(deg2rad(ctx->radarInfo.installAngComp_deg));
		float sinYaw = sinf(deg2rad(ctx->radarInfo.installAngComp_deg));
		radarInfo->x_radar_rcs_frmDiff = dx_vcs * cosYaw + dy_vcs * sinYaw;
		radarInfo->y_radar_rcs_frmDiff = dy_vcs * cosYaw - dx_vcs * sinYaw;

		// 角度差异（横摆角速度积分）
		float yawRate_pre = egoVehInfo->yawRate_pre;
		radarInfo->ang_radar_frmDiff_rad = yawRate_pre * delta_T +
			0.5f * (egoVehInfo->yawRate - yawRate_pre) * delta_T;
	}
	else
	{
		// 首帧：无前一帧数据，差异量为0
		radarInfo->x_radar_rcs_frmDiff = 0.0f;
		radarInfo->y_radar_rcs_frmDiff = 0.0f;
		radarInfo->ang_radar_frmDiff_rad = 0.0f;
	}


	// 更新横摆角速度前一帧值
	egoVehInfo->yawRate_pre = egoVehInfo->yawRate;
}