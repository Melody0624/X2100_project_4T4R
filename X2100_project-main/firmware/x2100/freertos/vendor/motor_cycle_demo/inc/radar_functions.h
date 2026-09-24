/**
 * @file        radar_functions.h
 * @brief       雷达处理主函数接口声明。包含检测层(Detection)和跟踪层(Tracking)
 *              所有对外暴露函数的声明，以及 ADC 数据读取接口。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef RADAR_FUNCTIONS_H
#define RADAR_FUNCTIONS_H

#include "radar_types.h"

#if OFFLINE_DEBUG_MODE
#include "../test/read_mat_file.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif
#include <stdlib.h>
#include <stdio.h>

	// 全局变量定义
	GlbCtx* set_parameters(bool isCalibInstallAng, bool isInhighAltitude, bool isFlipInstall_azm, float radarInstallAng);
	// ADC 数据读取
	size_t get_adc_total_size(GlbCtx* ctx);
	int get_adc_from_dat_file(GlbCtx* ctx, FILE* fid, bool isSkipFrm); 	// Under replay conditions
	int extract_ADCData_from_buffer(GlbCtx* ctx, void* pFrameBuf);		// Under actual testing conditions

	/*************************** Detection Processing ***************************/
	// 检测层处理
#if OFFLINE_DEBUG_MODE
	int detection_processing(GlbCtx* ctx, fftwf_complex* angCalibMat, int* debug_print_counter, float** rdMapOut, int pool_index);
	int detection_processing_v2(GlbCtx* ctx, fftwf_complex* angCalibMat, int* debug_print_counter, float** rdMapOut, int pool_index);
#else
	int detection_processing(GlbCtx* ctx, fft_cpx_f32* angCalibMat, int pool_index); 	// Under replay conditions
	int detection_processing_v2(GlbCtx* ctx, fft_cpx_f32* angCalibMat, int pool_index); 	// Under actual testing conditions
#endif
	
	// CFAR检测外层函数
	void cfar_detection(GlbCtx* ctx, const float* RDMap, int numRngBins, int numDopplerBins, unsigned int* maxMetricIndices, int pool_index);

	// CFAR检测函数
	int cfar_func_v2(GlbCtx* glbCtx, const float *rdmapDb, int numRangeBins, int numDopplerBins,
		int*det_flag_mat, float *range_cfar_th_mat, float *doppler_cfar_th_mat, int*isPeak_flag_mat,
        int*is_peak_mat_rng, int pool_index);

	// 角度估计
	void angle_estimation_v1(GlbCtx* glbCtx, const fft_cpx_f32* angCalibMat, const float* winAz,
		const fft_cpx_f32* dopplerFFT_res, unsigned int* maxMetricIndices, int numRangeBins, int numDoppBinsPerSubBand, int numVirtualAnts, int pool_index);

	// 检测后处理
	void detection_post_processing(GlbCtx* ctx);

	/*************************** Tracking Processing ***************************/
   // 跟踪处理
#if OFFLINE_DEBUG_MODE
	int tracking_processing(GlbCtx* ctx, MATFile* rTrkMat, MATFile* wTrkMat, mxArray* trkInfo_vec_in, mxArray* trkInfo_vec_out, int numSkippedFrms, int* debug_print_counter);
#else
	int tracking_processing(GlbCtx* ctx, int pool_index);
#endif

	// 跟踪预处理
	void tracking_preprocessing(GlbCtx* ctx, int pool_index);

	// 航迹起始
	void track_initialization(GlbCtx* ctx, int pool_index);

	// 生成新航迹
	void generate_new_track(GlbCtx* ctx, ClusterObj* clusterObj, int* clusterMark_vec);

	// 航迹预测
	void track_prediction_CV(GlbCtx* ctx);

	// 航迹关联
	void track_association(GlbCtx* ctx);

	// 航迹与检测点匹配
	void track_and_detection_association(GlbCtx* ctx, TrkAssocInfo* info, int detIdx, int trkIdx);

	// 航迹更新
	void track_update_CV(GlbCtx* ctx, int pool_index);

	// 航迹管理
	void track_management(GlbCtx* ctx);

	// 静止目标带管理
	void static_zone_obj_management(GlbCtx* ctx, int pool_index);
	//void static_zone_mark_detections(GlbCtx* ctx);
	//void static_zone_terminate_tracks(GlbCtx* ctx);

	// 航迹输出
	void track_output(GlbCtx* ctx);

#ifdef __cplusplus
}
#endif

#endif // RADAR_FUNCTIONS_H
