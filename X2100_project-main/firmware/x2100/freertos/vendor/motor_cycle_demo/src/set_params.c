/**
 * @file        set_params.c
 * @brief       全局参数初始化模块。分配并初始化全局上下文(GlbCtx)，设置波形参数、
 *              天线配置、CFAR 阈值、雷达安装信息及航迹 ID 管理器。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-08-18
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "general_functions.h"
#include "radar_types.h"
#include "memory_pool.h"
#include "board_calib_config.h"
#include "../../config_manager.h"
#include "heap_malloc.h"

 /* ++++++++++++  1. 全局变量定义  ++++++++++++ */
  // 窗函数变量
float* winRng_sf1 = NULL;
float* winRng_sf2 = NULL;
float* winRng_sf3 = NULL;
float* winRng_sf4 = NULL;

float* winDop_sf1 = NULL;
float* winDop_sf2 = NULL;
float* winDop_sf3 = NULL;
float* winDop_sf4 = NULL;

float* winAzm_sf1 = NULL;
float* winAzm_sf2 = NULL;
float* winAzm_sf3 = NULL;
float* winAzm_sf4 = NULL;

/* Retained source DSP uses these globals; the new detection archive owns
 * its windows internally. Keep shared storage alive across pool rebuilds. */
static float source_range_window[WIN_LEN_RANGE_FFT];
static float source_doppler_window[WIN_LEN_DOPPLER_FFT];
static float source_azimuth_window[WIN_LEN_AZIMUTH_FFT];

// 阈值
const float th_vcsStatic = 0.5f;		// 一般动静属性判别门限 m/s
const float th_FOVEdge = 60.0f;         // FOV角度范围
const float th_FOVEdge_bias = 60.0f;    // FOV边缘范围
const float th_rngEdge_near = 15.0f;    // 近距离分界线
const float th_rngEdge_far = 50.0f;     // 远距离分界线
const int th_maxAgeAsHeader_near = 5;   // Header状态的最大Age
const int th_maxAgeAsHeader_far = 3;   // Header状态的最大Age

/* ++++++++++++  2. 局部变量定义++++++++++++ */

/**
 * @brief   全局参数初始化。分配 GlbCtx 并填充基本参数、波形参数、雷达安装信息、
 *          CFAR 配置及临时内存池。调用方负责最终释放。
 * @param   isInhighAltitude    是否高海拔模式（启用高度补偿）
 * @param   isFlipInstall_azm   是否方位向反装
 * @param   radarInstallAng     雷达安装角（度）
 * @return  成功返回已初始化的 GlbCtx 指针，失败返回 NULL
 */
GlbCtx* set_parameters(bool isInhighAltitude, bool isFlipInstall_azm, float radarInstallAng, int pool_index)
{
	// ctx内存分配(清零)
	GlbCtx* glbCtx = (GlbCtx*)calloc(1, sizeof(GlbCtx));
	if (!glbCtx)
		return NULL;

	// 基本参数
	unsigned int numVx[NUM_SUBFRAMES];
	glbCtx->basicParams.cascade = false;
	glbCtx->basicParams.numMMIC = 1;
	glbCtx->basicParams.numRXPerMMIC = 4;
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R
	glbCtx->basicParams.numTxs[0] = 2;	// （实际使用数量与俯仰/方位无关）
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R
    glbCtx->basicParams.numTxs[0] = 3;
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    glbCtx->basicParams.numTxs[0] = 4;
#endif
	glbCtx->basicParams.numRxs[0] = 4;
	glbCtx->basicParams.minElemSpacing[0] = 0.5F;
	numVx[0] = glbCtx->basicParams.numTxs[0] * glbCtx->basicParams.numRxs[0];
	if (numVx[0] != NUM_VX_SF1) {
		return NULL;
	}
#if NUM_SUBFRAMES == 2
	glbCtx->basicParams.numTxs[1] = 4;
	glbCtx->basicParams.numRxs[1] = 4;
	glbCtx->basicParams.minElemSpacing[1] = 0.5F;
	numVx[1] = glbCtx->basicParams.numTxs[1] * glbCtx->basicParams.numRxs[1];
	if (numVx[1] != NUM_VX_SF2) {
		return NULL;
	}
#endif
	
	
	// 波形参数
#if SAVE_RAW_DATA
    glbCtx->waveParams.frame_period = 0.24984f;

#elif ADC_REPLAY
    glbCtx->waveParams.frame_period = 0.050144F;
#else
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R
#if((USE_USB_OUTPUT == 1)&&(UART_ADC_SEND == 1))
    glbCtx->waveParams.frame_period = 0.083328f; 
#else
    glbCtx->waveParams.frame_period = 0.050328f; 
#endif
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R
#if ((CHEETAH_CONFIG == cheetah_128_256_config) || (CHEETAH_CONFIG == cheetah_128_512_config))
    glbCtx->waveParams.frame_period = 0.04984f;
#elif CHEETAH_CONFIG == cheetah_256_512_config
    glbCtx->waveParams.frame_period = 0.05368f;
#endif
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
#if ((CHEETAH_CONFIG == cheetah_128_256_config) || (CHEETAH_CONFIG == cheetah_128_512_config))
    glbCtx->waveParams.frame_period = 0.04984f;
#elif CHEETAH_CONFIG == cheetah_256_512_config
    glbCtx->waveParams.frame_period = 0.05368f;
#endif
#endif
#endif

    glbCtx->waveParams.frame_class = 0;
	glbCtx->waveParams.isCRT = true;
	glbCtx->waveParams.mimo_mode = 0;
	glbCtx->waveParams.numSubFrms = NUM_SUBFRAMES;
	
#if ((CHEETAH_CONFIG == cheetah_128_256_config) || (CHEETAH_CONFIG == cheetah_128_512_config))
	glbCtx->waveParams.numChirps[0] = 128;
#elif CHEETAH_CONFIG == cheetah_256_512_config
    glbCtx->waveParams.numChirps[0] = 256;
#endif

#if CHEETAH_CONFIG == cheetah_128_256_config
	glbCtx->waveParams.adcLen[0] = 506;
#elif ((CHEETAH_CONFIG == cheetah_128_512_config) || (CHEETAH_CONFIG == cheetah_256_512_config))
    glbCtx->waveParams.adcLen[0] = 506;
#endif

#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R
	glbCtx->waveParams.numEmptyBands[0] = 2;
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R
    glbCtx->waveParams.numEmptyBands[0] = 1;
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    glbCtx->waveParams.numEmptyBands[0] = 4;
#endif

#if ((BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R) || (BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R))
    glbCtx->waveParams.sampling_freq[0] = 26.665e6;
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    glbCtx->waveParams.sampling_freq[0] = 53.33e6;
#endif

	glbCtx->waveParams.central_freq[0] = 76.5e9f;
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R
    glbCtx->waveParams.chirp_period[0] = 26e-6f;
#else
    glbCtx->waveParams.chirp_period[0] = 30e-6f;
#endif
#if (WAVERANGE_CONFIG == 38)
	glbCtx->waveParams.slope[0] = 52.051e12f;  //38m  
#elif (WAVERANGE_CONFIG == 60)
	glbCtx->waveParams.slope[0] = 33.301e12f;  //60m
#else
    glbCtx->waveParams.slope[0] = 16.699e12f;  //120m
#endif
	glbCtx->waveParams.wavelength[0] = LIGHT_SPEED / glbCtx->waveParams.central_freq[0];
	glbCtx->waveParams.rngRes[0] = LIGHT_SPEED / 2.0f / (glbCtx->waveParams.slope[0] *
		(glbCtx->waveParams.adcLen[0] / glbCtx->waveParams.sampling_freq[0]));
#if NUM_SUBFRAMES == 2
	glbCtx->waveParams.numChirps[1] = 640;
	glbCtx->waveParams.adcLen[1] = 1024;
	glbCtx->waveParams.numEmptyBands[1] = 3;
	glbCtx->waveParams.sampling_freq[1] = 26.665e6F;
	glbCtx->waveParams.central_freq[1] = 76.5e9F;
	glbCtx->waveParams.slope[1] = 4.9805E12F;
	glbCtx->waveParams.wavelength[1] = LIGHT_SPEED / glbCtx->waveParams.central_freq[1];
	glbCtx->waveParams.rngRes[1] = LIGHT_SPEED / 2.0f / (glbCtx->waveParams.slope[1] *
		(glbCtx->waveParams.adcLen[1] / glbCtx->waveParams.sampling_freq[1]));
#endif
    glbCtx->waveParams.wavelength[0] = LIGHT_SPEED / glbCtx->waveParams.central_freq[0];
    glbCtx->waveParams.rngRes[0] = LIGHT_SPEED / 2.0f /
        (glbCtx->waveParams.slope[0] * (glbCtx->waveParams.adcLen[0] / glbCtx->waveParams.sampling_freq[0]));
    unsigned int NFFT_2d = nextPowerOfTwo(glbCtx->waveParams.numChirps[0]);
    glbCtx->waveParams.dopRes[0] = glbCtx->waveParams.wavelength[0] / (2.0f * glbCtx->waveParams.chirp_period[0] * NFFT_2d);
    glbCtx->waveParams.maxUmAmbVlc[0] = glbCtx->waveParams.dopRes[0] * NFFT_2d / 2;

	// 自车信息：显式初始化，勿依赖调用方清零（移植到韧体时若为静态/栈分配未 memset，
	// 残留值会经 getEgoVlcByStatic 的「沿用上一帧」路径直接输出）
	glbCtx->egoVehInfo.egoSpeed = 0.0f;
	glbCtx->egoVehInfo.isEgoSpeedValid = false;
	glbCtx->egoVehInfo.egoRejCnt = 0;
	glbCtx->egoVehInfo.egoRejRef = 0.0f;
	glbCtx->egoVehInfo.egoMissCnt = 0;
	glbCtx->egoVehInfo.yawRate = 0.0f;
	glbCtx->egoVehInfo.yawRate_pre = 0.0f;
	glbCtx->egoVehInfo.rollRate = 0.0f;
	glbCtx->egoVehInfo.rollRate_pre = 0.0f;

    // 帧信息
    glbCtx->frmInfo.frmID = 0;
    glbCtx->frmInfo.lastFrmID = 0;

	// 雷达信息
	glbCtx->radarInfo.isFlipInstall_azm = isFlipInstall_azm;
	glbCtx->radarInfo.isInHighAlt = isInhighAltitude;
	glbCtx->radarInfo.installAngComp_deg = radarInstallAng;
	glbCtx->radarInfo.mountingHeightSqr = 0.64F;

#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R && BOARD_TYPE == BOARD_TYPE_BARRACADE
	/* User phase-center document: TX0/RX0 are rightmost,
	 * TX-major virtual channels advance left by 1.96 mm. */
	glbCtx->basicParams.minElemSpacing[0] = -0.00196f / glbCtx->waveParams.wavelength[0];

#endif
	if (init_memory_pool(glbCtx, pool_index, 0, 0) != 0) {
		printf("错误: init_memory_pool 失败 pool_index=%d\n", pool_index);
		return NULL;
	}

	blackman_window(source_range_window, WIN_LEN_RANGE_FFT);
	hanning_window(source_doppler_window, WIN_LEN_DOPPLER_FFT);
	hanning_window(source_azimuth_window, WIN_LEN_AZIMUTH_FFT);
	winRng_sf1 = source_range_window;
	winDop_sf1 = source_doppler_window;
	winAzm_sf1 = source_azimuth_window;

	// Doppler CFAR配置
	glbCtx->dopplerCfarCfg.enable = DOPPLER_CFAR_ENABLE;
	glbCtx->dopplerCfarCfg.winlen = 4;
	glbCtx->dopplerCfarCfg.guardlen = 2;
	glbCtx->dopplerCfarCfg.mode = CFAR_MODE_CASO;
	glbCtx->dopplerCfarCfg.thold = 6.0f;
	glbCtx->dopplerCfarCfg.isPeakdet = true;
	// Range CFAR配置
	glbCtx->rangeCfarCfg.enable = RANGE_CFAR_ENABLE;
	glbCtx->rangeCfarCfg.winlen = 4;
	glbCtx->rangeCfarCfg.guardlen = 2;
	glbCtx->rangeCfarCfg.mode = CFAR_MODE_CAGO;
	glbCtx->rangeCfarCfg.thold = 3.0f;
	glbCtx->rangeCfarCfg.isPeakdet = true;

	// 航迹ID管理器
	glbCtx->trackIDManager.topIdx = 0;
	glbCtx->trackIDManager.maxTrackID = MAX_TRACKS;
	for (int i = 0; i < MAX_TRACKS; i++) {
		glbCtx->trackIDManager.trackIDList[i] = i;
	}

	return glbCtx;
}
