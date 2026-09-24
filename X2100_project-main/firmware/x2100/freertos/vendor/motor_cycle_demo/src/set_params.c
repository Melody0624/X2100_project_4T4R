#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "general_functions.h"
#include "radar_types.h"
#include "board_calib_config.h"
#include "../../config_manager.h"
#include "heap_malloc.h"

// 窗函数全局变量定义
float* winRng = NULL;
float* winDop = NULL;
float* winAz = NULL;

// 定义
const float th_vcsStatic = 1.0f;		// 一般动静属性判别门限
const float th_FOVEdge = 60.0f;         // FOV角度范围
const float th_FOVEdge_bias = 60.0f;    // FOV边缘范围
const float th_rngEdge_near = 15.0f;    // 近距离分界线
const float th_rngEdge_far = 50.0f;     // 远距离分界线
const int th_maxAgeAsHeader_near = 5;   // Header状态的最大Age
const int th_maxAgeAsHeader_far = 3;   // Header状态的最大Age

const int thr_motionStatusCnt_staticTomove = 6;
const int thr_motionStatusCnt_moveTostop = 4;

// 主函数
GlbCtx* set_parameters(bool isCalibInstallAng, bool isInhighAltitude, bool isFlipInstall_azm, float radarInstallAng) {
	// ctx内存分配(清零)
    GlbCtx* ctx = (GlbCtx*)calloc(1, sizeof(GlbCtx)); // 清零
    if (!ctx) return NULL;

    // 基本参数
    ctx->basic_params.cascade = false;
    ctx->basic_params.numMMIC = 1;
    ctx->basic_params.numRXPerMMIC = 4;
    ctx->basic_params.numTxs = 2;
    ctx->basic_params.numRxs = 4;
    ctx->basic_params.minElemSpacing = 0.5;
    int numVx = ctx->basic_params.numTxs * ctx->basic_params.numRxs;

    // 波形参数
    ctx->wave_params.sampling_freq = 26.665e6;
    ctx->wave_params.frame_class = 0;
    ctx->wave_params.mimo_mode = 0;
#if SAVE_RAW_DATA
    ctx->wave_params.frame_period = 0.24984f;

#elif ADC_REPLAY
    ctx->wave_params.frame_period = 0.24984f;
#else
    ctx->wave_params.frame_period = 0.050328f; 
    // ctx->wave_params.frame_period = 0.05f;
#endif
    ctx->wave_params.numSubFrms = 1;
    ctx->wave_params.numChirps = 128;
#if CHEETAH_CONFIG == cheetah_128_256_config
    ctx->wave_params.adcLen = 256;
#elif CHEETAH_CONFIG == cheetah_128_512_config
    ctx->wave_params.adcLen = 506;
#endif
    ctx->wave_params.central_freq = 76.5e9f;
    ctx->wave_params.chirp_period = 26e-6f;
    ctx->wave_params.slope = 19.531e12f; //39.063e12f;
    ctx->wave_params.numEmptyBands = 2;
    ctx->wave_params.wavelength = LIGHT_SPEED / ctx->wave_params.central_freq;
    ctx->wave_params.rngRes = LIGHT_SPEED / 2.0f /
        (ctx->wave_params.slope * (ctx->wave_params.adcLen / ctx->wave_params.sampling_freq));
    unsigned int NFFT_2d = nextPowerOfTwo(ctx->wave_params.numChirps);
    ctx->wave_params.dopRes = ctx->wave_params.wavelength / (2.0f * ctx->wave_params.chirp_period * NFFT_2d);
    ctx->wave_params.maxUmAmbVlc = ctx->wave_params.dopRes * NFFT_2d / 2;

    // 雷达信息
    ctx->radarInfo.isCompInstallAng = isCalibInstallAng;
    ctx->radarInfo.isFlipInstall_azm = isFlipInstall_azm;
    ctx->radarInfo.isInHighAlt = isInhighAltitude;
    ctx->radarInfo.installAngComp_deg = radarInstallAng;
    ctx->radarInfo.mountingHeightSqr = (2 + 5.5) * (2 + 5.5);

    ctx->radarInfo.longOffset = 0.5f;

    ctx->radarInfo.vxPosn = (float*)malloc(numVx * sizeof(float));
    if (ctx->radarInfo.vxPosn != NULL)
        memcpy(ctx->radarInfo.vxPosn, (float[]) {0.0f,0.5f,1.0f,1.5f,2.0f,2.5f,3.0f,3.5f },8 * sizeof(float));
    ctx->radarInfo.logicIdx_reorder = (int*)malloc(numVx * sizeof(int));
    if (ctx->radarInfo.logicIdx_reorder != NULL)
        memcpy(ctx->radarInfo.logicIdx_reorder, (int[]) {0,1,2,3,4,5,6,7 },8 * sizeof(int));

    // 帧信息
    ctx->frmInfo.frmID = 0;
    ctx->frmInfo.lastFrmID = 0;

	// 算法信息
	ctx->algInfo.angFFTvec = (float*)malloc(AZM_FFT_SIZE * sizeof(float));
    if (IS_ANG_ACC_IMPROVE_INTERP)
    {
        // 当前不支持
        if (ctx->algInfo.angFFTvec != NULL)
            	memcpy(ctx->algInfo.angFFTvec, angFFT_interp, AZM_FFT_SIZE * sizeof(float)); // angFFT_interp 为复数数组 fftwf_complex[128]
    }
    else 
    {
        // 角度FFT插值向量预计算
        for (int index = 0; index < AZM_FFT_SIZE; index++) 
        {
            // 将非负的数组索引转换为有符号频率索引
			int shiftIdx = (index < AZM_FFT_SIZE / 2) ? index : (index - AZM_FFT_SIZE);
            // 计算 sin(θ) = k / (N * d)
            float sin_theta = (float)shiftIdx / (AZM_FFT_SIZE * ctx->basic_params.minElemSpacing);
            // 夹紧到 asinf 的有效定义域 [-1, 1]
            if (sin_theta > 1.0f)  sin_theta = 1.0f;
            if (sin_theta < -1.0f) sin_theta = -1.0f;
            // 计算角度并转换为度
            ctx->algInfo.angFFTvec[index] = rad2deg(asinf(sin_theta));
        }
    }

    // Range CFAR配置
    ctx->rangeCfarCfg.enable = RANGE_CFAR_ENABLE;
    ctx->rangeCfarCfg.winlen = 4;
    ctx->rangeCfarCfg.guardlen = 2;
    ctx->rangeCfarCfg.mode = CFAR_MODE_CAGO; 
    ctx->rangeCfarCfg.thold = 3.0f;
    ctx->rangeCfarCfg.isPeakdet = true;

    // Doppler CFAR配置
    ctx->dopplerCfarCfg.enable = DOPPLER_CFAR_ENABLE;
    ctx->dopplerCfarCfg.winlen = 4;
    ctx->dopplerCfarCfg.guardlen = 2;
    ctx->dopplerCfarCfg.mode = CFAR_MODE_CASO; 
    ctx->dopplerCfarCfg.thold = 6.0f;
    ctx->dopplerCfarCfg.isPeakdet = true;

    // 航迹ID管理器
    ctx->trackIDManager.topIdx = 0;
    ctx->trackIDManager.maxTrackID = MAX_TRACKS;
    for (int i = 0; i < MAX_TRACKS; i++) {
        ctx->trackIDManager.trackIDList[i] = i;
    }

    // 航迹信息初始化
    ctx->trkInfo.numTrks = 0;
    for (int i = 0; i < MAX_TRACKS; i++) {
        ctx->trkInfo.trkObj[i].isvalid = false;
    }

    // ADC数据内存分配（简化）
    int totalSamples = ctx->wave_params.adcLen * ctx->wave_params.numChirps * ctx->basic_params.numRxs;
    ctx->adcData.sf1 = (float*)malloc(totalSamples * sizeof(float)); // 稍后分配
    if (ctx->adcData.sf1 == NULL) {
        // 内存分配失败，但后续主程序会重新分配，此处仅设为 NULL
        ctx->adcData.sf1 = NULL;
    }
    ctx->adcData.isSubframe1Exist = false;

    // 检测点信息初始化
    ctx->detInfoRD.numDets = 0;
    ctx->detInfo.numDets = 0;

	// 静止目标带识别初始化 (calloc已清零，显式设置关键字段)
	ctx->staticDetInfo.numMembers = 0;
	ctx->staticDetInfo.storageIdx = 0;  /* 0-based, 初始写入位置 */
	ctx->staticZoneInfo.updateAge = 0;
	ctx->staticZoneInfo.numStaticZone = 0;
	/* staticZoneObj数组已被calloc清零 → 全部isValid=false */

	// 窗函数初始化
	if (winRng == NULL) {
		winRng = (float*)malloc(WIN_LEN_RANGE_FFT * sizeof(float));
		if (winRng) blackman_window(winRng, WIN_LEN_RANGE_FFT);
	}
	if (winDop == NULL) {
		winDop = (float*)malloc(WIN_LEN_DOPPLER_FFT * sizeof(float));
		if (winDop) hanning_window(winDop, WIN_LEN_DOPPLER_FFT);
	}
	if (winAz == NULL) {
		winAz = (float*)malloc(WIN_LEN_AZIMUTH_FFT * sizeof(float));
		if (winAz) hanning_window(winAz, WIN_LEN_AZIMUTH_FFT);
	}

    return ctx;
}