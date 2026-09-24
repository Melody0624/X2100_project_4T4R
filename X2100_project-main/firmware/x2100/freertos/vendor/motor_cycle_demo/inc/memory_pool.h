#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "radar_types.h"
struct Mmw_pkt_info_t;
#include <stdbool.h>

#define MAXPOOLNUM 3

// 内存池结构体
typedef struct {
    // 检测处理相关缓冲区
    fft_cpx_f32* adcData_winDC;                     // ADC去直流数据
    fft_cpx_f32* rangeFFT_res;                      // Range FFT结果
    fft_cpx_f32* dopplerFFT_res;                    // Doppler FFT结果
    fft_cpx_f32* fftTemp;                           // FFT临时缓冲区
    float* dopplerFFT_sumRx;                        // 非相干累积结果
    unsigned int* maxMetricIndices;                 // 最大度量索引
    float* RDMap;                                   // RD图数据
    
    // FFT配置结构体
    ne10_fft_r2c_cfg_float32_t range_fft_cfg;       // Range FFT配置
    ne10_fft_r2c_cfg_float32_t doppler_fft_cfg;     // Doppler FFT配置
    ne10_fft_r2c_cfg_float32_t azim_fft_cfg;        // azim FFT配置

    // Range FFT内部缓冲区
    float* rangeFFT_in;                             // Range FFT输入
    float* rangeFFT_out;                            // Range FFT输出

    // Doppler FFT相关缓冲区
    fft_cpx_f32* doppler_fft_in;                    // Doppler FFT输入缓冲区
    fft_cpx_f32* doppler_fft_out;                   // Doppler FFT输出缓冲区
    
    // azim FFT相关缓冲区
    fft_cpx_f32* azim_fft_in;                       // azim FFT输入缓冲区
    fft_cpx_f32* azim_fft_out;                      // azim FFT输出缓冲区

    // 角度估计相关缓冲区
    fft_cpx_f32* bv;                                // 波束形成向量
    float* azimFFTMag;                              // azim FFT幅度
    
    // 非相干累积临时缓冲区
    float** rx_abs_values;                          // 接收通道模值缓冲区
    
    // CFAR检测相关缓冲区
    int* cfar_det_flag_mat;                         // CFAR检测标志矩阵
    float* cfar_range_th_mat;                       // CFAR距离阈值矩阵
    float* cfar_doppler_th_mat;                     // CFAR多普勒阈值矩阵
    int* cfar_isPeak_flag_mat;                      // CFAR峰值标志矩阵
    int* cfar_is_peak_mat_rng;                      // CFAR距离峰值矩阵
    int* cfar_doppler_det_flag_mat;                 // CFAR多普勒检测标志矩阵
    int* cfar_is_peak_mat_dop;                      // CFAR多普勒峰值矩阵
    int* cfar_doppler_detlines_vec;                 // CFAR多普勒检测线向量
    int* cfar_left_win;                             // CFAR左窗口索引
    int* cfar_right_win;                            // CFAR右窗口索引
    int* cfar_upper_win;                            // CFAR上窗口索引
    int* cfar_lower_win;                            // CFAR下窗口索引
    
    // DDMA解码相关缓冲区
    float* ddma_curSubBandVals;                     // DDMA当前子带值
    float* ddma_minSubBandVal;                      // DDMA最小子带值
    
    // 跟踪处理相关缓冲区
    float* trk_measurement;                         // 跟踪测量向量
    float* trk_innovation;                          // 跟踪创新向量
    float* trk_kalman_gain;                         // 卡尔曼增益
    int* trk_validTrkID_list;                       // 有效航迹ID列表

    // VLC估计相关缓冲区
    float* vlc_azRad;                               // 角度弧度值缓冲区
    float* vlc_cosVal;                              // 余弦值缓冲区
    float* vlc_sinVal;                              // 正弦值缓冲区
    bool* vlc_inlierMask;                           // 内点掩码缓冲区
    
    // RANSAC相关缓冲区
    int* ransac_sampleIndices;                      // 采样索引缓冲区
    float* ransac_tempModel;                        // 临时模型缓冲区
    bool* ransac_tempInliers;                       // 临时内点掩码缓冲区
    int* ransac_perm;                               // 随机索引缓冲区
    
    // trkInit相关缓冲区
    int* trkInit_clusterMark_vec;                   // 聚类标记向量
    float* trkInit_detAzm_vec;                      // 检测点角度向量
    float* trkInit_detVlc_vec;                      // 检测点速度向量
    float* x_tcs_list;                              // 聚类中心X坐标列表
    float* y_tcs_list;                              // 聚类中心Y坐标列表
    
    // Kalman filter 相关缓冲区
    float* kalman_ZVec;                             // 测量向量缓冲区
    float* kalman_HMat;                             // 观测矩阵缓冲区
    float* kalman_RMat;                             // 测量噪声矩阵缓冲区
    
    // Kalman filter 内部计算缓冲区
    float* kalman_S;                                // 协方差矩阵S
    float* kalman_Sinv;                             // 协方差矩阵S的逆
    float* kalman_K;                                // 卡尔曼增益矩阵
    float* kalman_PH_T;                             // P * H转置
    float* kalman_HP;                               // H * P
    float* kalman_innovation;                       // 创新向量
    float* kalman_KH;                               // K * H
    float* kalman_I_KH;                             // I - K * H
    float* kalman_tempP;                            // 临时协方差矩阵
    float* kalman_aug;                              // 增广矩阵（用于矩阵求逆）
    
    // PCA分析相关缓冲区
    float* pca_pson_vec;                            // 所有数据点
    float* pca_cov;                                 // PCA协方差矩阵缓冲区
    // update reference point
    float* assoc_x_tcs_list;                              // 关联聚类中心X坐标列表
    float* assoc_y_tcs_list;                              // 关联聚类中心Y坐标列表
    
    // 协方差计算相关缓冲区
    float* cov_mean;                                // 协方差均值缓冲区（默认矩阵维度为2）
    float* cov_diff;                                // 协方差偏差缓冲区（默认矩阵维度为2）

    struct Mmw_pkt_info_t *mmw_pkt_info_buf;                 // mmw_pkt_info缓冲区，UART/SAVE/CAN统一处理数据格式转换，避免重复转换
    char *shared_pkt_buf;                           // 共享数据包缓冲区(UART/SAVE共用，由主任务统一封包，CAN需要单独处理)
    uint32_t shared_pkt_buf_size;                   // 共享数据包缓冲区大小
    char *saver_pkt_buf_raw;                        // 保存原始数据包缓冲区
#if 0
    char *saver_pkt_buf_proc;                       // 保存处理后的数据包缓冲区
#endif
    
    // 内存池状态标志
    bool initialized;                               // 内存池是否已初始化
    
} MemoryPool;

// 全局内存池实例
extern MemoryPool g_memoryPool[MAXPOOLNUM];

// 函数声明
int init_memory_pool(GlbCtx* ctx, int pool_index, int adcOutFlg);
void free_memory_pool(int pool_index);

#ifdef __cplusplus
}
#endif

#endif // MEMORY_POOL_H