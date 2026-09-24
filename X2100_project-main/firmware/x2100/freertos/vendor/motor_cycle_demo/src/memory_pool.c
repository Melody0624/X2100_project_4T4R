#include "memory_pool.h"
#include "general_functions.h"
#include "ransac.h"
#include "vlc_estimation.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "heap_malloc.h"

// 全局内存池实例
MemoryPool g_memoryPool[MAXPOOLNUM] = {0};

static unsigned int g_numTxs = 0;
static unsigned int g_numRxs = 0;

// 内存池初始化函数
int init_memory_pool(GlbCtx* ctx, int pool_index, int adcOutFlg) {

    // printf("init_memory_pool pool_index: %d, g_memoryPool[pool_index].initialized: %d\n", pool_index, g_memoryPool[pool_index].initialized);
    if (g_memoryPool[pool_index].initialized)
    {
        return -2; // 已经初始化
    }
    
    // 获取雷达参数
    g_numTxs = ctx->basic_params.numTxs;
    g_numRxs = ctx->basic_params.numRxs;
    unsigned int adcLen = ctx->wave_params.adcLen;
    unsigned int numChirps = ctx->wave_params.numChirps;
    unsigned int numTotalSubBands = g_numTxs + ctx->wave_params.numEmptyBands;
    unsigned int numVirtualAnts = g_numTxs * g_numRxs;
    unsigned int numRXPerMMIC = ctx->basic_params.numRXPerMMIC;

    unsigned int NFFT_range = nextPowerOfTwo(adcLen);
    unsigned int numRngBins = NFFT_range / 2;
    unsigned int NFFT_doppler = nextPowerOfTwo(numChirps);
    unsigned int numDopBinsPerSubBand = NFFT_doppler / numTotalSubBands;
    unsigned int totalAdcSamples = adcLen * numChirps * g_numRxs;
    
    // 分配检测处理相关缓冲区
    g_memoryPool[pool_index].adcData_winDC = (fft_cpx_f32*)malloc(totalAdcSamples * sizeof(fft_cpx_f32));
    g_memoryPool[pool_index].rangeFFT_res = (fft_cpx_f32*)malloc(numRngBins * numChirps * g_numRxs * sizeof(fft_cpx_f32));
    g_memoryPool[pool_index].dopplerFFT_res = (fft_cpx_f32*)malloc(numRngBins * NFFT_doppler * g_numRxs * sizeof(fft_cpx_f32));
    
    unsigned int maxFFTSize = (NFFT_range > NFFT_doppler) ? NFFT_range : NFFT_doppler;
    g_memoryPool[pool_index].fftTemp = (fft_cpx_f32*)malloc((maxFFTSize + FFT_ARRAY_GUARD_LEN) * sizeof(fft_cpx_f32));
    
    g_memoryPool[pool_index].dopplerFFT_sumRx = (float*)malloc(numRngBins * NFFT_doppler * sizeof(float));
    g_memoryPool[pool_index].maxMetricIndices = (unsigned int*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(unsigned int));
    g_memoryPool[pool_index].RDMap = (float*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(float));
    
    if (numTotalSubBands == 6 || numTotalSubBands == 12)			// Doppler FFT点数修正
		NFFT_doppler = NFFT_doppler / 4 * 3;
    
    // 分配和初始化FFT配置结构体
    g_memoryPool[pool_index].range_fft_cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)NFFT_range);
    g_memoryPool[pool_index].doppler_fft_cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)NFFT_doppler);
    g_memoryPool[pool_index].azim_fft_cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)AZM_FFT_SIZE);

    // 分配Range FFT内部缓冲区
    g_memoryPool[pool_index].rangeFFT_in = (float*)malloc((NFFT_range + FFT_ARRAY_GUARD_LEN) * sizeof(float));
    g_memoryPool[pool_index].rangeFFT_out = (float*)malloc((NFFT_range + FFT_ARRAY_GUARD_LEN) * sizeof(float));

    // 分配Doppler FFT相关缓冲区
    g_memoryPool[pool_index].doppler_fft_in = (fft_cpx_f32*)malloc((NFFT_doppler + FFT_ARRAY_GUARD_LEN) * sizeof(fft_cpx_f32));
    g_memoryPool[pool_index].doppler_fft_out = (fft_cpx_f32*)malloc((NFFT_doppler + FFT_ARRAY_GUARD_LEN) * sizeof(fft_cpx_f32));

    // 分配角度估计相关缓冲区
    g_memoryPool[pool_index].bv = (fft_cpx_f32*)malloc(sizeof(fft_cpx_f32) * numVirtualAnts);
    g_memoryPool[pool_index].azimFFTMag = (float*)malloc(AZM_FFT_SIZE * sizeof(float));
    g_memoryPool[pool_index].azim_fft_in = (fft_cpx_f32*)malloc((AZM_FFT_SIZE + FFT_ARRAY_GUARD_LEN) * sizeof(fft_cpx_f32));
    g_memoryPool[pool_index].azim_fft_out = (fft_cpx_f32*)malloc((AZM_FFT_SIZE + FFT_ARRAY_GUARD_LEN) * sizeof(fft_cpx_f32));
    
    // 分配非相干累积临时缓冲区
    g_memoryPool[pool_index].rx_abs_values = (float**)malloc(g_numRxs * sizeof(float*));
    for (int i = 0; i < g_numRxs; i++)
    {
        g_memoryPool[pool_index].rx_abs_values[i] = (float*)malloc(numRngBins * NFFT_doppler * sizeof(float));
    }
    
    // 分配CFAR检测相关缓冲区
    g_memoryPool[pool_index].cfar_det_flag_mat = (int*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_range_th_mat = (float*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(float));
    g_memoryPool[pool_index].cfar_doppler_th_mat = (float*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(float));
    g_memoryPool[pool_index].cfar_isPeak_flag_mat = (int*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_is_peak_mat_rng = (int*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_doppler_det_flag_mat = (int*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_is_peak_mat_dop = (int*)malloc(numRngBins * numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_doppler_detlines_vec = (int*)malloc(numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_left_win = (int*)malloc(numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_right_win = (int*)malloc(numDopBinsPerSubBand * sizeof(int));
    g_memoryPool[pool_index].cfar_upper_win = (int*)malloc(numRngBins * sizeof(int));
    g_memoryPool[pool_index].cfar_lower_win = (int*)malloc(numRngBins * sizeof(int));
    
    // 分配DDMA解码相关缓冲区
    g_memoryPool[pool_index].ddma_curSubBandVals = (float*)malloc((numTotalSubBands + g_numTxs - 1) * sizeof(float));
    g_memoryPool[pool_index].ddma_minSubBandVal = (float*)malloc(numTotalSubBands * sizeof(float));
    
    // 分配跟踪处理相关缓冲区
    g_memoryPool[pool_index].trk_measurement = (float*)malloc(MAX_DETS_FOR_TRK_UPDATE * sizeof(float));
    g_memoryPool[pool_index].trk_innovation = (float*)malloc(MAX_DETS_FOR_TRK_UPDATE * sizeof(float));
    g_memoryPool[pool_index].trk_kalman_gain = (float*)malloc(MAX_DETS_FOR_TRK_UPDATE * sizeof(float));
    g_memoryPool[pool_index].trk_validTrkID_list = (int*)malloc(MAX_TRACKS * sizeof(int));

    // 分配VLC估计相关缓冲区
    g_memoryPool[pool_index].vlc_azRad = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].vlc_cosVal = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].vlc_sinVal = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].vlc_inlierMask = (bool*)malloc(MAX_DETECTIONS * sizeof(bool));

    // 分配RANSAC相关缓冲区
    g_memoryPool[pool_index].ransac_sampleIndices = (int*)malloc(RANSAC_MAX_SAMPLE_SIZE * sizeof(int));
    g_memoryPool[pool_index].ransac_tempModel = (float*)malloc(RANSAC_DEFAULT_MODEL_SIZE * sizeof(float));
    g_memoryPool[pool_index].ransac_tempInliers = (bool*)malloc(MAX_DETECTIONS * sizeof(bool));
    g_memoryPool[pool_index].ransac_perm = (int*)malloc(MAX_DETECTIONS * sizeof(int));

    // 分配trkInit相关缓冲区
    g_memoryPool[pool_index].trkInit_clusterMark_vec = (int*)malloc(MAX_DETECTIONS * sizeof(int));
    g_memoryPool[pool_index].trkInit_detAzm_vec = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].trkInit_detVlc_vec = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].x_tcs_list = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].y_tcs_list = (float*)malloc(MAX_DETECTIONS * sizeof(float));

    // 分配Kalman filter相关缓冲区
    unsigned int maxMeasMatLen = MAX_DETS_FOR_TRK_UPDATE + 2;
    g_memoryPool[pool_index].kalman_ZVec = (float*)malloc(maxMeasMatLen * sizeof(float));
    g_memoryPool[pool_index].kalman_HMat = (float*)malloc(maxMeasMatLen * NUM_TRACKER_STATES * sizeof(float));
    g_memoryPool[pool_index].kalman_RMat = (float*)malloc(maxMeasMatLen * maxMeasMatLen * sizeof(float));

    // 分配Kalman filter内部计算缓冲区
    g_memoryPool[pool_index].kalman_S = (float*)malloc(maxMeasMatLen * maxMeasMatLen * sizeof(float));
    g_memoryPool[pool_index].kalman_Sinv = (float*)malloc(maxMeasMatLen * maxMeasMatLen * sizeof(float));
    g_memoryPool[pool_index].kalman_K = (float*)malloc(NUM_TRACKER_STATES * maxMeasMatLen * sizeof(float));
    g_memoryPool[pool_index].kalman_PH_T = (float*)malloc(NUM_TRACKER_STATES * maxMeasMatLen * sizeof(float));
    g_memoryPool[pool_index].kalman_HP = (float*)malloc(maxMeasMatLen * NUM_TRACKER_STATES * sizeof(float));
    g_memoryPool[pool_index].kalman_innovation = (float*)malloc(maxMeasMatLen * sizeof(float));
    g_memoryPool[pool_index].kalman_KH = (float*)malloc(NUM_TRACKER_STATES * NUM_TRACKER_STATES * sizeof(float));
    g_memoryPool[pool_index].kalman_I_KH = (float*)malloc(NUM_TRACKER_STATES * NUM_TRACKER_STATES * sizeof(float));
    g_memoryPool[pool_index].kalman_tempP = (float*)malloc(NUM_TRACKER_STATES * NUM_TRACKER_STATES * sizeof(float));
    g_memoryPool[pool_index].kalman_aug = (float*)malloc(maxMeasMatLen * 2 * maxMeasMatLen * sizeof(float));
    
    // 分配PCA分析相关缓冲区
    g_memoryPool[pool_index].pca_pson_vec = (float*)malloc(2 * MAX_ASSOC_DETS_RECORD * sizeof(float));
    g_memoryPool[pool_index].pca_cov = (float*)malloc(4 * sizeof(float));
    g_memoryPool[pool_index].assoc_x_tcs_list = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    g_memoryPool[pool_index].assoc_y_tcs_list = (float*)malloc(MAX_DETECTIONS * sizeof(float));
    
    // 分配协方差计算相关缓冲区
    g_memoryPool[pool_index].cov_mean = (float*)malloc(2 * sizeof(float));  // numDims=2（默认矩阵维度为2）
    g_memoryPool[pool_index].cov_diff = (float*)malloc(2 * sizeof(float));  // numDims=2（默认矩阵维度为2）
    
    // 分配mmw_pkt_info缓冲区
    g_memoryPool[pool_index].mmw_pkt_info_buf = (Mmw_pkt_info*)malloc(sizeof(Mmw_pkt_info));

    // 分配保存原始数据包缓冲区
    uint32_t save_raw_packetLen =  numChirps * (sizeof(CheetahChirpHeadInfo) + sizeof(uint16_t) * numRXPerMMIC * adcLen + CHIRP_END_BYTES);
    g_memoryPool[pool_index].saver_pkt_buf_raw = (char*)malloc(save_raw_packetLen * sizeof(char));

#if(USE_USB_OUTPUT == 1)
#if(UART_ADC_SEND == 0)
    if(adcOutFlg == 1)
    {
        uint32_t shared_packetLen = sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl) + save_raw_packetLen;
        uint32_t shared_totalPacketLen = shared_packetLen;
        g_memoryPool[pool_index].shared_pkt_buf = (char *)malloc(shared_totalPacketLen * sizeof(char));
        g_memoryPool[pool_index].shared_pkt_buf_size = shared_totalPacketLen;
    }
    else
    {
        // 分配发送数据包缓冲区
        uint32_t shared_packetLen = sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_DetInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_TrkInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_EgoVlcInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_WarnInfo);
        // uint32_t shared_totalPacketLen = MMWDEMO_OUTPUT_MSG_SEGMENT_LEN * ((shared_packetLen + (MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - 1)) / MMWDEMO_OUTPUT_MSG_SEGMENT_LEN);
        uint32_t shared_totalPacketLen = shared_packetLen;
        g_memoryPool[pool_index].shared_pkt_buf = (char *)malloc(shared_totalPacketLen * sizeof(char));
        g_memoryPool[pool_index].shared_pkt_buf_size = shared_totalPacketLen;
    }
#else
    uint32_t shared_packetLen;
    shared_packetLen = sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl) + save_raw_packetLen
                         + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_DetInfo)
                         + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_TrkInfo)
                         + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_EgoVlcInfo)
                         + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_WarnInfo);
    uint32_t shared_totalPacketLen = shared_packetLen;
    g_memoryPool[pool_index].shared_pkt_buf = (char *)malloc(shared_totalPacketLen * sizeof(char));
    g_memoryPool[pool_index].shared_pkt_buf_size = shared_totalPacketLen;
#endif
#else
    // 分配发送数据包缓冲区
    uint32_t shared_packetLen = sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_DetInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_TrkInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_EgoVlcInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_WarnInfo);
    // uint32_t shared_totalPacketLen = MMWDEMO_OUTPUT_MSG_SEGMENT_LEN * ((shared_packetLen + (MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - 1)) / MMWDEMO_OUTPUT_MSG_SEGMENT_LEN);
    uint32_t shared_totalPacketLen = shared_packetLen;
    g_memoryPool[pool_index].shared_pkt_buf = (char *)malloc(shared_totalPacketLen * sizeof(char));
    g_memoryPool[pool_index].shared_pkt_buf_size = shared_totalPacketLen;

#endif
#if 0
    // 分配保存处理后的数据包缓冲区
    uint32_t save_proc_packetLen = sizeof(Mmw_output_message_header) + sizeof(Mmw_output_message_tl) + save_raw_packetLen 
                                   + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_DetInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_TrkInfo)
                                   + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_EgoVlcInfo) + sizeof(Mmw_output_message_tl) + sizeof(Mmw_output_message_WarnInfo);
    uint32_t save_proc_totalPacketLen = MMWDEMO_OUTPUT_MSG_SEGMENT_LEN * ((save_proc_packetLen + (MMWDEMO_OUTPUT_MSG_SEGMENT_LEN - 1)) / MMWDEMO_OUTPUT_MSG_SEGMENT_LEN);
    g_memoryPool[pool_index].saver_pkt_buf_proc = (char*)malloc(save_proc_totalPacketLen * sizeof(char));
#endif

    // 检查所有分配是否成功
    if (g_memoryPool[pool_index].adcData_winDC == NULL || g_memoryPool[pool_index].rangeFFT_res == NULL ||
        g_memoryPool[pool_index].dopplerFFT_res == NULL || g_memoryPool[pool_index].fftTemp == NULL || g_memoryPool[pool_index].dopplerFFT_sumRx == NULL ||
        g_memoryPool[pool_index].maxMetricIndices == NULL || g_memoryPool[pool_index].RDMap == NULL ||
        g_memoryPool[pool_index].cfar_det_flag_mat == NULL || g_memoryPool[pool_index].cfar_range_th_mat == NULL ||
        g_memoryPool[pool_index].cfar_doppler_th_mat == NULL || g_memoryPool[pool_index].cfar_isPeak_flag_mat == NULL ||
        g_memoryPool[pool_index].cfar_is_peak_mat_rng == NULL || g_memoryPool[pool_index].cfar_doppler_det_flag_mat == NULL ||
        g_memoryPool[pool_index].cfar_is_peak_mat_dop == NULL || g_memoryPool[pool_index].cfar_doppler_detlines_vec == NULL ||
        g_memoryPool[pool_index].cfar_left_win == NULL || g_memoryPool[pool_index].cfar_right_win == NULL ||
        g_memoryPool[pool_index].cfar_upper_win == NULL || g_memoryPool[pool_index].cfar_lower_win == NULL ||
        g_memoryPool[pool_index].range_fft_cfg == NULL || g_memoryPool[pool_index].doppler_fft_cfg == NULL || g_memoryPool[pool_index].azim_fft_cfg == NULL ||
        g_memoryPool[pool_index].pca_pson_vec == NULL || g_memoryPool[pool_index].pca_cov == NULL ||
        g_memoryPool[pool_index].assoc_x_tcs_list == NULL || g_memoryPool[pool_index].assoc_y_tcs_list == NULL ||
        g_memoryPool[pool_index].cov_mean == NULL || g_memoryPool[pool_index].cov_diff == NULL ||
        g_memoryPool[pool_index].rangeFFT_in == NULL || g_memoryPool[pool_index].rangeFFT_out == NULL ||
        g_memoryPool[pool_index].doppler_fft_in == NULL || g_memoryPool[pool_index].doppler_fft_out == NULL ||
        g_memoryPool[pool_index].azim_fft_in == NULL || g_memoryPool[pool_index].azim_fft_out == NULL ||
        g_memoryPool[pool_index].bv == NULL || g_memoryPool[pool_index].azimFFTMag == NULL ||
        g_memoryPool[pool_index].rx_abs_values == NULL || g_memoryPool[pool_index].ddma_curSubBandVals == NULL || g_memoryPool[pool_index].ddma_minSubBandVal == NULL ||
        g_memoryPool[pool_index].vlc_azRad == NULL || g_memoryPool[pool_index].vlc_cosVal == NULL || g_memoryPool[pool_index].vlc_sinVal == NULL || g_memoryPool[pool_index].vlc_inlierMask == NULL ||
        g_memoryPool[pool_index].ransac_sampleIndices == NULL || g_memoryPool[pool_index].ransac_tempModel == NULL || g_memoryPool[pool_index].ransac_tempInliers == NULL || g_memoryPool[pool_index].ransac_perm == NULL ||
        g_memoryPool[pool_index].trkInit_clusterMark_vec == NULL || g_memoryPool[pool_index].trkInit_detAzm_vec == NULL || g_memoryPool[pool_index].trkInit_detVlc_vec == NULL ||
        g_memoryPool[pool_index].x_tcs_list == NULL || g_memoryPool[pool_index].y_tcs_list == NULL ||
        g_memoryPool[pool_index].kalman_ZVec == NULL || g_memoryPool[pool_index].kalman_HMat == NULL || g_memoryPool[pool_index].kalman_RMat == NULL ||
        g_memoryPool[pool_index].kalman_S == NULL || g_memoryPool[pool_index].kalman_Sinv == NULL || g_memoryPool[pool_index].kalman_K == NULL ||
        g_memoryPool[pool_index].kalman_PH_T == NULL || g_memoryPool[pool_index].kalman_HP == NULL || g_memoryPool[pool_index].kalman_innovation == NULL ||
        g_memoryPool[pool_index].kalman_KH == NULL || g_memoryPool[pool_index].kalman_I_KH == NULL || g_memoryPool[pool_index].kalman_tempP == NULL ||
        g_memoryPool[pool_index].kalman_aug == NULL ||
        g_memoryPool[pool_index].trk_measurement == NULL || g_memoryPool[pool_index].trk_innovation == NULL ||
        g_memoryPool[pool_index].trk_kalman_gain == NULL || g_memoryPool[pool_index].trk_validTrkID_list == NULL ||
        g_memoryPool[pool_index].mmw_pkt_info_buf == NULL || g_memoryPool[pool_index].shared_pkt_buf == NULL ||
        g_memoryPool[pool_index].saver_pkt_buf_raw == NULL 
#if 0
        || g_memoryPool[pool_index].saver_pkt_buf_proc == NULL
#endif
    )
    {
        
        fprintf(stderr, "内存池分配失败\n");
        free_memory_pool(pool_index);
        return -1;
    }
    
    // 检查接收通道缓冲区分配
    for (int i = 0; i < g_numRxs; i++)
    {
        if (g_memoryPool[pool_index].rx_abs_values[i] == NULL)
        {
            fprintf(stderr, "接收通道缓冲区分配失败\n");
            free_memory_pool(pool_index);
            return -1;
        }
    }
    
    // 初始化内存池状态
    g_memoryPool[pool_index].initialized = true;
    
    return 0;
}

// 内存池释放函数
void free_memory_pool(int pool_index) {
    if (!g_memoryPool[pool_index].initialized)
    {
        return;
    }
    
    // 释放检测处理相关缓冲区
    if (g_memoryPool[pool_index].adcData_winDC) free(g_memoryPool[pool_index].adcData_winDC);
    if (g_memoryPool[pool_index].rangeFFT_res) free(g_memoryPool[pool_index].rangeFFT_res);
    if (g_memoryPool[pool_index].dopplerFFT_res) free(g_memoryPool[pool_index].dopplerFFT_res);
    if (g_memoryPool[pool_index].fftTemp) free(g_memoryPool[pool_index].fftTemp);
    if (g_memoryPool[pool_index].dopplerFFT_sumRx) free(g_memoryPool[pool_index].dopplerFFT_sumRx);
    if (g_memoryPool[pool_index].maxMetricIndices) free(g_memoryPool[pool_index].maxMetricIndices);
    if (g_memoryPool[pool_index].RDMap) free(g_memoryPool[pool_index].RDMap);
    
    // 释放FFT配置结构体
    if (g_memoryPool[pool_index].range_fft_cfg) ne10_fft_destroy_r2c_float32(g_memoryPool[pool_index].range_fft_cfg);
    if (g_memoryPool[pool_index].doppler_fft_cfg) ne10_fft_destroy_r2c_float32(g_memoryPool[pool_index].doppler_fft_cfg);
    if (g_memoryPool[pool_index].azim_fft_cfg) ne10_fft_destroy_r2c_float32(g_memoryPool[pool_index].azim_fft_cfg);
    
    // 释放Range FFT内部缓冲区
    if (g_memoryPool[pool_index].rangeFFT_in) free(g_memoryPool[pool_index].rangeFFT_in);
    if (g_memoryPool[pool_index].rangeFFT_out) free(g_memoryPool[pool_index].rangeFFT_out);

    // 释放Doppler FFT相关缓冲区
    if (g_memoryPool[pool_index].doppler_fft_in) free(g_memoryPool[pool_index].doppler_fft_in);
    if (g_memoryPool[pool_index].doppler_fft_out) free(g_memoryPool[pool_index].doppler_fft_out);

    // 释放azim FFT相关缓冲区
    if (g_memoryPool[pool_index].azim_fft_in) free(g_memoryPool[pool_index].azim_fft_in);
    if (g_memoryPool[pool_index].azim_fft_out) free(g_memoryPool[pool_index].azim_fft_out);

    // 释放角度估计相关缓冲区
    if (g_memoryPool[pool_index].bv) free(g_memoryPool[pool_index].bv);
    if (g_memoryPool[pool_index].azimFFTMag) free(g_memoryPool[pool_index].azimFFTMag);
    
    // 释放非相干累积临时缓冲区
    if (g_memoryPool[pool_index].rx_abs_values)
    {
        for (int i = 0; i < g_numRxs; i++)
        {
            if (g_memoryPool[pool_index].rx_abs_values[i]) free(g_memoryPool[pool_index].rx_abs_values[i]);
        }
        free(g_memoryPool[pool_index].rx_abs_values);
    }
    
    // 释放CFAR检测相关缓冲区
    if (g_memoryPool[pool_index].cfar_det_flag_mat) free(g_memoryPool[pool_index].cfar_det_flag_mat);
    if (g_memoryPool[pool_index].cfar_range_th_mat) free(g_memoryPool[pool_index].cfar_range_th_mat);
    if (g_memoryPool[pool_index].cfar_doppler_th_mat) free(g_memoryPool[pool_index].cfar_doppler_th_mat);
    if (g_memoryPool[pool_index].cfar_isPeak_flag_mat) free(g_memoryPool[pool_index].cfar_isPeak_flag_mat);
    if (g_memoryPool[pool_index].cfar_is_peak_mat_rng) free(g_memoryPool[pool_index].cfar_is_peak_mat_rng);
    if (g_memoryPool[pool_index].cfar_doppler_det_flag_mat) free(g_memoryPool[pool_index].cfar_doppler_det_flag_mat);
    if (g_memoryPool[pool_index].cfar_is_peak_mat_dop) free(g_memoryPool[pool_index].cfar_is_peak_mat_dop);
    if (g_memoryPool[pool_index].cfar_doppler_detlines_vec) free(g_memoryPool[pool_index].cfar_doppler_detlines_vec);
    if (g_memoryPool[pool_index].cfar_left_win) free(g_memoryPool[pool_index].cfar_left_win);
    if (g_memoryPool[pool_index].cfar_right_win) free(g_memoryPool[pool_index].cfar_right_win);
    if (g_memoryPool[pool_index].cfar_upper_win) free(g_memoryPool[pool_index].cfar_upper_win);
    if (g_memoryPool[pool_index].cfar_lower_win) free(g_memoryPool[pool_index].cfar_lower_win);
    
    // 释放DDMA解码相关缓冲区
    if (g_memoryPool[pool_index].ddma_curSubBandVals) free(g_memoryPool[pool_index].ddma_curSubBandVals);
    if (g_memoryPool[pool_index].ddma_minSubBandVal) free(g_memoryPool[pool_index].ddma_minSubBandVal);
    
    // 释放跟踪处理相关缓冲区
    if (g_memoryPool[pool_index].trk_measurement) free(g_memoryPool[pool_index].trk_measurement);
    if (g_memoryPool[pool_index].trk_innovation) free(g_memoryPool[pool_index].trk_innovation);
    if (g_memoryPool[pool_index].trk_kalman_gain) free(g_memoryPool[pool_index].trk_kalman_gain);
    if (g_memoryPool[pool_index].trk_validTrkID_list) free(g_memoryPool[pool_index].trk_validTrkID_list);

    // 释放VLC估计相关缓冲区
    if (g_memoryPool[pool_index].vlc_azRad) free(g_memoryPool[pool_index].vlc_azRad);
    if (g_memoryPool[pool_index].vlc_cosVal) free(g_memoryPool[pool_index].vlc_cosVal);
    if (g_memoryPool[pool_index].vlc_sinVal) free(g_memoryPool[pool_index].vlc_sinVal);
    if (g_memoryPool[pool_index].vlc_inlierMask) free(g_memoryPool[pool_index].vlc_inlierMask);
    
    // 释放RANSAC相关缓冲区
    if (g_memoryPool[pool_index].ransac_sampleIndices) free(g_memoryPool[pool_index].ransac_sampleIndices);
    if (g_memoryPool[pool_index].ransac_tempModel) free(g_memoryPool[pool_index].ransac_tempModel);
    if (g_memoryPool[pool_index].ransac_tempInliers) free(g_memoryPool[pool_index].ransac_tempInliers);
    if (g_memoryPool[pool_index].ransac_perm) free(g_memoryPool[pool_index].ransac_perm);
    
    // 释放trkInit相关缓冲区
    if (g_memoryPool[pool_index].trkInit_clusterMark_vec) free(g_memoryPool[pool_index].trkInit_clusterMark_vec);
    if (g_memoryPool[pool_index].trkInit_detAzm_vec) free(g_memoryPool[pool_index].trkInit_detAzm_vec);
    if (g_memoryPool[pool_index].trkInit_detVlc_vec) free(g_memoryPool[pool_index].trkInit_detVlc_vec);
    if (g_memoryPool[pool_index].x_tcs_list) free(g_memoryPool[pool_index].x_tcs_list);
    if (g_memoryPool[pool_index].y_tcs_list) free(g_memoryPool[pool_index].y_tcs_list);
    
    // 释放Kalman filter相关缓冲区
    if (g_memoryPool[pool_index].kalman_ZVec) free(g_memoryPool[pool_index].kalman_ZVec);
    if (g_memoryPool[pool_index].kalman_HMat) free(g_memoryPool[pool_index].kalman_HMat);
    if (g_memoryPool[pool_index].kalman_RMat) free(g_memoryPool[pool_index].kalman_RMat);
    
    // 释放Kalman filter内部计算缓冲区
    if (g_memoryPool[pool_index].kalman_S) free(g_memoryPool[pool_index].kalman_S);
    if (g_memoryPool[pool_index].kalman_Sinv) free(g_memoryPool[pool_index].kalman_Sinv);
    if (g_memoryPool[pool_index].kalman_K) free(g_memoryPool[pool_index].kalman_K);
    if (g_memoryPool[pool_index].kalman_PH_T) free(g_memoryPool[pool_index].kalman_PH_T);
    if (g_memoryPool[pool_index].kalman_HP) free(g_memoryPool[pool_index].kalman_HP);
    if (g_memoryPool[pool_index].kalman_innovation) free(g_memoryPool[pool_index].kalman_innovation);
    if (g_memoryPool[pool_index].kalman_KH) free(g_memoryPool[pool_index].kalman_KH);
    if (g_memoryPool[pool_index].kalman_I_KH) free(g_memoryPool[pool_index].kalman_I_KH);
    if (g_memoryPool[pool_index].kalman_tempP) free(g_memoryPool[pool_index].kalman_tempP);
    if (g_memoryPool[pool_index].kalman_aug) free(g_memoryPool[pool_index].kalman_aug);
    
    // 释放PCA分析相关缓冲区
    if (g_memoryPool[pool_index].pca_pson_vec) free(g_memoryPool[pool_index].pca_pson_vec);
    if (g_memoryPool[pool_index].pca_cov) free(g_memoryPool[pool_index].pca_cov);
    if (g_memoryPool[pool_index].assoc_x_tcs_list) free(g_memoryPool[pool_index].assoc_x_tcs_list);
    if (g_memoryPool[pool_index].assoc_y_tcs_list) free(g_memoryPool[pool_index].assoc_y_tcs_list);
    
    // 释放协方差计算相关缓冲区
    if (g_memoryPool[pool_index].cov_mean) free(g_memoryPool[pool_index].cov_mean);
    if (g_memoryPool[pool_index].cov_diff) free(g_memoryPool[pool_index].cov_diff);
    
    // 释放mmw_pkt_info缓冲区
    if (g_memoryPool[pool_index].mmw_pkt_info_buf) free(g_memoryPool[pool_index].mmw_pkt_info_buf);
    // 释放共享数据包缓冲区
    if (g_memoryPool[pool_index].shared_pkt_buf) free(g_memoryPool[pool_index].shared_pkt_buf);
    // 释放保存原始数据包缓冲区
    if (g_memoryPool[pool_index].saver_pkt_buf_raw) free(g_memoryPool[pool_index].saver_pkt_buf_raw);
#if 0
    // 释放保存处理后的数据包缓冲区
    if (g_memoryPool[pool_index].saver_pkt_buf_proc) free(g_memoryPool[pool_index].saver_pkt_buf_proc);
#endif

    // 重置内存池状态
    memset(&g_memoryPool[pool_index], 0, sizeof(MemoryPool));
}