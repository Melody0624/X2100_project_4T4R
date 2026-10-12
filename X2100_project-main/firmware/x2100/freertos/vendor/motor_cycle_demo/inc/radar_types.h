/**
 * @file        radar_types.h
 * @brief       雷达系统核心类型定义。包含全局上下文结构体(GlbCtx)、
 *              检测对象、航迹对象、波形参数、CFAR 配置等所有数据结构及宏常量声明。
 * @author      Runze Huang
 * @company     SenardMicro
 * @date        2026-05-27
 * @version     1.0
 * @copyright   Copyright (c) 2026 SenardMicro. All rights reserved.
 */
#ifndef RADAR_TYPES_H
#define RADAR_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <fft.h>

#define USE_BPM                             1       // 使用BPM编码
#define OFFLINE_DEBUG_MODE					0		// 离线调试模式
#define IS_MIRROR							0								// 1：镜像安装，0：非镜像安装
#define WARNING_ENABLE						1								// 1：预警使能，0：预警禁用	
#define WARNING_PROFILE_HONDA				1       // 预警策略选择：0=零行(LingXing)默认策略，1=新大洲本田(honda)策略
#define EGO_VLC_ENABLE					    1       // 主车速度估計開關

#define SAVE_RAW_DATA						0		// 1 : 保存原始数据, 0 : 保存处理后的数据
#define ADC_REPLAY				            0
#define ZU3_FORMAT_DATA_REPLAY				0		// 1 : 读取 Zu3 所采集的 ADC 数据并处理，0 : 无作用	
#define DOT_REPLAY				            0		// 1 : 从 .dat 文件读取 ADC 数据并处理，0 : 从雷达芯片实时读取 ADC 数据并处理

#define USE_USB_UART			            1	    // 使用USB串口输出数据（如果为0则使用普通UART输出）
#define USE_USB_OUTPUT                      1       // 使用USB串口输出数据（如果为0则使用普通UART输出）
#define UART_ADC_SEND 			            0

#define WAVERANGE_CONFIG                    120

// 网络 ADC 发送控制（需 CONFIG_X2000_MAC0 开启网络后才生效）
#ifdef CONFIG_X2000_MAC0
#define NET_ADC_SEND                        1       // 0: 仅发送点云等数据  1: 增加发送 ADC 数据
#else
#define NET_ADC_SEND                        0		// 仅在网络未启用时作为定义使用，此处无需修改
#endif

#define WARNING_HONDA_MIN_EGO_KMH			0       // honda版全局启动门槛(整数km/h)；0=不设门槛，自车静止(车速0)时也预警


#define BOARD_ANTENNA_2T4R 					0       /* 用于 2T4R 雷达角度校准 */
#define BOARD_ANTENNA_3T4R 					1       /* 用于 3T4R 雷达角度校准 */
#define BOARD_ANTENNA_4T4R 					2       /* 用于 4T4R 雷达角度校准 */
/* [!] 在此选择当前雷达测试板的天线配置 */
#define BOARD_ANTENNA_CONFIG 				BOARD_ANTENNA_2T4R
// #define BOARD_ANTENNA_CONFIG 				BOARD_ANTENNA_3T4R
// #define BOARD_ANTENNA_CONFIG 				BOARD_ANTENNA_4T4R


#define cheetah_128_256_config				1
#define cheetah_128_512_config				2
#define cheetah_256_512_config				3

// #define CHEETAH_CONFIG 						cheetah_128_256_config
#define CHEETAH_CONFIG 						cheetah_128_512_config
// #define CHEETAH_CONFIG 						cheetah_256_512_config

typedef ne10_fft_cpx_float32_t fft_cpx_f32;

#define FFT_ARRAY_GUARD_LEN     		    4
#define PI									3.14159265358979323846f			// PI
#define PI_2								1.57079632679489661923f			// PI/2
#define LIGHT_SPEED							299792458.0f					// 光速 (m/s)
	// 数据类型
#define DATA_TYPE_DAT_ADC					0
#define DATA_TYPE_DAT_DETINFO				1
#define DATA_TYPE_MAT_DETINFO				2
	// 速度解模糊
#define VLC_DISAMB_CONF_MATCHED				1								// 置信度：帧间匹配成功，已解模糊
#define VLC_DISAMB_CONF_NEAR				2								// 置信度：近距未搜索模糊周期，速度仅在单帧 ±vmax 窗口内
#define VLC_DISAMB_CONF_UNSURE				5								// 置信度：未找到匹配，按模糊系数 0 处理
#define VLC_DISAMB_CONF_INIT				10								// 置信度：未经解模糊（首帧/初始值）
#define VLC_DISAMB_NEAR_RNG					15.0F							// 近距门限 (m)：该距离内不做模糊周期搜索
#define VLC_DISAMB_MAX_RAD_VLC				40.0F							// 解模糊后径向速度绝对值上限 (m/s)，与自车速度上限共用；路测 40~50 m/s 候选多为 CRT 误匹配
#define VLC_DISAMB_MIN_VMAX_DIFF			1.0F							// 相邻帧 vmax 最小差值 (m/s)：k=±1 假设在上一帧的多普勒间隔为 2·Δvmax，需大于动态匹配门限 1 m/s 才可区分
#define VLC_DISAMB_KNZ_CONFIRM_ENABLE		1								// 1：k≠0 的 CRT 结果需与上一帧匹配点的候选速度一致才采用（连续 2 帧确认），0：关闭
#define VLC_DISAMB_CONFIRM_TOL				1.0F							// k≠0 确认时，本帧与上一帧候选速度的最大差值 (m/s)
	// 硬件和波形参数
#define NUM_SUBFRAMES						1								// 单帧子帧数量
#define NUM_VX_SF1							8								// 总VX数量	子帧1
#define NUM_VX_SF2							0								// 总VX数量	子帧2
#define NUM_VX_AZM_SF1						8								// 方向维VX数量 子帧1
#define NUM_VX_AZM_SF2						0								// 方向维VX数量 子帧2
#define NUM_VX_EL_SF1						0								// 俯仰维VX数量 子帧1
#define NUM_VX_EL_SF2						0								// 俯仰维VX数量 子帧2
// 窗函数
#if ((CHEETAH_CONFIG == cheetah_128_512_config) || (CHEETAH_CONFIG == cheetah_256_512_config))
#define WIN_LEN_RANGE_FFT					506		// range-fft 窗长度
#elif CHEETAH_CONFIG == cheetah_128_256_config
#define WIN_LEN_RANGE_FFT					256		// range-fft 窗长度
#endif
#if ((CHEETAH_CONFIG == cheetah_128_256_config) || (CHEETAH_CONFIG == cheetah_128_512_config))
#define WIN_LEN_DOPPLER_FFT					128		// doppler-fft 窗长度
#elif CHEETAH_CONFIG == cheetah_256_512_config
#define WIN_LEN_DOPPLER_FFT					256		// doppler-fft 窗长度
#endif
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R
#define WIN_LEN_AZIMUTH_FFT					8		// azimuth-fft 窗长度
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_3T4R
#define WIN_LEN_AZIMUTH_FFT					12		// azimuth-fft 窗长度
#elif BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
#define WIN_LEN_AZIMUTH_FFT					16		// azimuth-fft 窗长度
#endif
// 雷达参数
#define AZM_FFT_SIZE						128     // 方位FFT点数
#define MAX_DETECTIONS						256     // 检测点最大数量
#define MAX_TRACKS							32      // 航迹最大数量
#define MAX_WARN_IDS 						32		// 每类预警最多记录的 trkID 数量
#define MAX_DETS_FOR_TRK_UPDATE				5       // 航迹状态更新的检测点最大数量（速度部分）
#define MAX_ASSOC_DETS_RECORD				10      // 航迹历史检测点记录最大数量
#define MAX_FRM_TRK_HIST_REC				5       // 航迹历史信息最大记录帧数
// 静止目标带参数
#define MAX_STATIC_ZONES					6       // 最大静止目标带数量
#define MAX_STATIC_DET_INFO_REC				300     // 静止检测点仓库容量
#define MIN_POINT_FOR_STATIC_ZONE			15      // 起始/更新所需最小点数
#define MAX_STATIC_STORAGE_PERIOD			5       // 静止点最长存储时间/静止带更新间隔(帧)
// ADC 数据格式
#define MAGIC_BYTE_SEARCH_LIMIT				500		// 魔数搜索范围
#define FILE_HEADER_BYTES					25712	// 文件头字节数，需根据实际数据文件调整
#define T_SENSOR_BYTES						0       // 温度传感器字节数
#define PACKET_HEADER_BYTES					(T_SENSOR_BYTES + 68 + 8)   // 数据包头字节数
#define CHIRP_HEADER_BYTES_CHEETAH			32      // Cheetah芯片Chirp头字节数
#define CHIRP_END_BYTES						0       // Chirp尾字节数
#define PACKET_PAD_ZERO_BYTES				(T_SENSOR_BYTES ? 0 : 16)   // 数据包尾填充字节数
	// 算法参数
#define	EGO_VLC_EST_ENABLE					1								// 自车速度估计使能	
#define MOTION_STATUS_STATIC     			0								// 静止
#define MOTION_STATUS_STOP     				1								// 停止
#define MOTION_STATUS_MOVE       			2								// 运动
	// 1. 检测层参数
#define AZM_FFT_SIZE						128								// 方位FFT点数
#define	IS_ANG_ACC_IMPROVE_INTERP			true   // 是否使用角度FFT插值提升角度估计精度（true: 使用插值，false: 不使用插值）
#define USE_ELEVATION_IN_COORDS				0								// 是否使用俯仰角度参与坐标计算
#define MAX_DETECTIONS						256								// 检测点最大数量
#define IS_CRT								false   // 是否帧间速度解模糊 
#define RANGE_CFAR_ENABLE					true    // 距离CFAR使能
#define DOPPLER_CFAR_ENABLE					true    // 速度CFAR使能

#define CFAR_MODE_CA						0								// CA-CFAR
#define CFAR_MODE_CAGO						1								// GO-CFAR
#define CFAR_MODE_CASO						2								// SO-CFAR

	// 2. 跟踪层参数
#define NUM_TRACKER_STATES       			4								// 航迹状态属性数量 CV模型4 CA模型6
#define MAX_ASSOC_TRK_FOR_SINGLE_DETS		2								// 单个检测点关联航迹最大数量
#define MAX_DETS_FOR_TRK_UPDATE  			5								// 用于航迹测量向量的检测点最大数量
#define MAX_FRM_TRK_HIST_REC     			5								// 航迹历史信息记录上限（帧数量）
#define MAX_ASSOC_DETS_FOR_SINGLE_TRK		10								// 单个航迹关联检测点最大数量
#define MAX_ASSOC_DETS_RECORD				10								// 航迹历史检测点记录最大数量

#define TRACK_STATE_HEADER     				0								// 航迹头 
#define TRACK_STATE_NEW        				1								// 新航迹
#define TRACK_STATE_MATURE     				2								// 成熟航迹

#define UPDATE_STATUS_MISS       			0								// 本帧未更新
#define UPDATE_STATUS_UPDATE     			1								// 本帧已经更新

#define EDGE_LEN_A               			0								// 参考边: 纵向A
#define EDGE_LEN_B               			1								// 参考边: 纵向B
#define EDGE_WID_A               			2								// 参考边: 横向A
#define EDGE_WID_B               			3								// 参考边: 横向B

#define ASSOC_STATUS_NON         			0								// 未关联
#define ASSOC_STATUS_COMPETITIVE 			1								// 弱关联（竞争关联）
#define ASSOC_STATUS_ASSOC       			2								// 强关联
#define ASSOC_STATUS_BAD         			3								// 不正常点，不允许关联

	// 3. 静止目标带参数
#define MAX_STATIC_ZONES					6								// 最大静止目标带数量
#define MAX_STATIC_DET_INFO_REC				300								// 静止检测点仓库容量
#define MIN_POINT_FOR_STATIC_ZONE			15								// 起始/更新所需最小点数
#define MAX_STATIC_STORAGE_PERIOD			5								// 静止点最长存储时间/静止带更新间隔(帧)
#define MAX_STATIC_ZONE_MISS_ASSOC_CNT		6								// 静止带连续未关联计数上限
#define LINE_MODEL_SIZE						2								// 模型参数数量 (直线: y = kx + b)	


// 辅助宏
#define SQ(x)	((x)*(x))						// 平方

	// 异常宏定义值检查
#if NUM_SUBFRAMES < 1 || NUM_SUBFRAMES > 2
#error "NUM_SUBFRAMES: 取值只能为 1 或 2"
#endif

	/* +++++++++++++++++  2. 全局变量定义 +++++++++++++++++ */
	extern float* winRng_sf1;						// 距离向窗函数
	extern float* winRng_sf2;
	extern float* winRng_sf3;
	extern float* winRng_sf4;
	extern float* winDop_sf1;						// 多普勒窗函数
	extern float* winDop_sf2;
	extern float* winDop_sf3;
	extern float* winDop_sf4;
	extern float* winAzm_sf1;						// 角度向窗函数
	extern float* winAzm_sf2;
	extern float* winAzm_sf3;
	extern float* winAzm_sf4;

	extern const float th_vcsStatic;

	extern int8_t bpm_code[512];

	extern fft_cpx_f32 angCalibMat_sf1[NUM_VX_SF1];
#if NUM_SUBFRAMES == 2
	extern fft_cpx_f32 angCalibMat_sf2[NUM_VX_SF2];
#endif

	/* ++++++++++  3. 数据结构体定义(非跟踪部分) ++++++++++ */
	// 基本参数结构体
	typedef struct {
		bool cascade;								// 是否级联模式
		unsigned int numMMIC;						// 级联芯片数量
		unsigned int numRXPerMMIC;					// 单芯片RX数量
		unsigned int numTxs[NUM_SUBFRAMES];			// 雷达发射天线数量
		unsigned int numRxs[NUM_SUBFRAMES];			// 雷达接收天线数量
		float minElemSpacing[NUM_SUBFRAMES];		// 最小阵元间距（unit: lambda）
	} BasicParams;

	// 波形参数结构体
	typedef struct {
		bool isCRT;									// 是否速度解模糊
		unsigned int frame_class;					// 速度解模糊AB波标识 0:A波，1:B波
		unsigned int mimo_mode;						// MIMO模式 0:'ddm',1:'tdm'
		unsigned int numSubFrms;					// 子帧数量
		float frame_period;							// 帧周期（sec）
		unsigned int numChirps[NUM_SUBFRAMES];		// Chirp数量
		unsigned int adcLen[NUM_SUBFRAMES];			// 单Chirp的ADC数据长度
		unsigned int numEmptyBands[NUM_SUBFRAMES];	// 空条带数量
		float sampling_freq[NUM_SUBFRAMES];			// 子帧ADC采样率
		float central_freq[NUM_SUBFRAMES];			// 中心频率
		float chirp_period[NUM_SUBFRAMES];			// 子帧chirp周期
		float slope[NUM_SUBFRAMES];					// 调频斜率
		float wavelength[NUM_SUBFRAMES];			// 波长
		float rngRes[NUM_SUBFRAMES];				// 距离分辨率
		float dopRes[NUM_SUBFRAMES];				// 速度分辨率
		float maxUmAmbVlc[NUM_SUBFRAMES];			// 最大不模糊速度
	} WaveParams;

	// 雷达信息结构体
	typedef struct {
		bool isFlipInstall_azm;						// 是否雷达反装
		bool isInHighAlt;							// 雷达是否处于高空
		bool isDetVlcPreCompensated;				// detInfo.vlc 是否已由固件补偿为对地速度
		float installAngComp_deg;					// 安装补偿角：RCS→VCS 按 -installAngComp_deg 旋转
		float mountingHeightSqr;					// 高空补偿
		unsigned int* logicIdx_reorder_sf1;			// 逻辑索引重排
		unsigned int* vxBVIdx_sf1_azm;				// BV索引 Azm
		float* vxPosn_sf1_azm;						// 天线位置
		unsigned int* vxBVIdx_sf1_el;				// BV索引 El（俯仰）
		float* vxPosn_sf1_el;						// 俯仰天线位置
#if NUM_SUBFRAMES == 2
		unsigned int* logicIdx_reorder_sf2;
		unsigned int* vxBVIdx_sf2_azm;
		float* vxPosn_sf2_azm;
		unsigned int* vxBVIdx_sf2_el;
		float* vxPosn_sf2_el;
#endif

		float longOffset;							// 纵向偏移（正：天线在前，负：天线在后）
		float vx_radar_vcs;							// 雷达速度（VCS，当前帧）
		float vy_radar_vcs;
		float vx_radar_vcs_pre;						// 雷达速度（VCS，前一帧）
		float vy_radar_vcs_pre;
		float x_radar_rcs_frmDiff;					// 帧间纵向位移（RCS）
		float y_radar_rcs_frmDiff;					// 帧间横向位移（RCS）
		float ang_radar_frmDiff_rad;				// 帧间坐标系角度变化（rad）
	} RadarInfo;

// chirp信息
typedef struct CheetahChirpInfo_t{
    // line 1st
    uint16_t Burst_Profile:3;
    uint16_t Chirp_Profile:3;
    uint16_t RX_ACTIVE:3;
    uint16_t TX_ACTIVE:3;
    uint16_t pad_1:4;
    // line 2nd
    uint16_t ext_2:3;
    uint16_t Chirp_Index:9;
    uint16_t pad_2:4;
    // line 3rd
    uint16_t ext_3:2;
    uint16_t BurstL_Index:3;
    uint16_t Burst_Loop_Index:7;
    uint16_t pad_3:4;
    // line 4th
    uint16_t ext_4:6;
    uint16_t Beam_Profile_Index:4;
    uint16_t SF_Index:2;
    uint16_t pad_4:4;
}CheetahChirpInfo;

typedef struct CheetahDDMAInfo_t{
    uint16_t Tx_DDMA_1:6;
    uint16_t Tx_DDMA_2:6;
    uint16_t pad:4;
}CheetahDDMAInfo;

typedef struct CheetahChirpHeadInfo_t{
    uint16_t ext_1:12;
    uint16_t pad_1:4;
    uint16_t ext_2:12;
    uint16_t pad_2:4;
    CheetahChirpInfo ChirpInfo_1;
    CheetahChirpInfo ChirpInfo_2;
    CheetahDDMAInfo DDMAInfo_12;
    CheetahDDMAInfo DDMAInfo_34;
    CheetahDDMAInfo DDMAInfo_56;
    CheetahDDMAInfo DDMAInfo_78;
    uint16_t ext_15:12;
    uint16_t pad_15:4;
    uint16_t ext_16:12;
    uint16_t pad_16:4;
} CheetahChirpHeadInfo;

	// 帧信息
	typedef struct {
		int frmID;									// 本帧ID
		int lastFrmID;								// 前一帧ID
	} FrmInfo;

	/* 主车信息结构体 */
	typedef struct {
		float egoSpeed;								// 自车速度（m/s）
		bool isEgoSpeedValid;						// 自车速度是否已有可信来源
		int egoRejCnt;								// 与上一帧不一致而被拒绝的连续估计次数（前后连贯时累计）
		float egoRejRef;							// 前一个被拒绝的估计值（用于判断连续被拒绝的估计是否连贯）
		int egoMissCnt;								// 自上次采用以来无可用估计的帧数（含丢帧），用于放宽一致性门限
		float yawRate;								// 横摆角速度（rad/s）
		float yawRate_pre;							// 前一帧横摆角速度
		float rollRate;								// 横滚角速度（rad/s）
		float rollRate_pre;							// 前一帧横滚角速度
	} EgoVehInfo;

	// 算法参数信息
	typedef struct {
		float* angFFTvec;							// 角度FFT向量
	}AlgInfo;

	// ADC数据结构体
	typedef struct {
		bool isSubframe1Exist;						// 子帧是否存在
		bool isSubframe2Exist;						// 子帧是否存在
		float* sf1;									// 子帧ADC
		float* sf2;									// 子帧ADC
	} AdcData;

	// CFAR配置
	typedef struct {
		bool enable;								// 是否使能
		bool isPeakdet;								// 是否峰值检测
		unsigned int winlen;						// 窗口长度
		unsigned int guardlen;						// 守卫长度
		unsigned int mode;							// 模式 0:CA-CFAR, 1:CAGO-CFAR 2:CASO-CFAR
		float thold;								// 检出阈值
	} CfarCfg;

	// 单个检测结构体 RD
	typedef struct {
		uint32_t sfIdx;								// 子帧索引
		uint32_t rngIdx;							// 距离索引
		uint32_t dopIdx;							// 速度索引
		uint32_t subbandIdx;						// 子带索引
		bool isPeak;								// 是否峰值
		float pwr;									// Power
		float snr;									// SNR
		float rngBinIdx;							// 距离单元索引
		float dopBinIdx_dec;						// 速度单元索引
	} DetObjRD;

	// RD检测点结构体
	typedef struct {
		int numDets;
		DetObjRD detObjRD[MAX_DETECTIONS];
	} DetInfoRD;

	//单个检测结构体 点云
	typedef struct {
		uint32_t relRDIdx;							// 关联的detObjRD索引（随结构体移动，排序后仍保持对应关系）
		uint32_t sfID;								// 对应子帧ID
		float pwr;									// 非相参积累（in db）
		float snr;									// 信噪比
		bool isPeak;								// 标识：峰值
		unsigned int rngIdx;						// 距离索引
		unsigned int dopIdx;						// 速度索引
		float rng;									// 径向距离
		float vlc_amb;								// 径向速度
		float vlc;									// 径向速度（解模糊）；检测后处理前为相对速度，后处理补偿后为对地速度
		int vlc_disAmb_conf;						// 速度解模糊置信度，取值见 VLC_DISAMB_CONF_*（1:已解模糊, 2:近距未搜索, 5:将信将疑, 10:不可靠）
		int vlc_disAmb_fac;							// 速度解模糊系数
		float vlc_cand;								// CRT 候选速度（无论是否采用都保存，供下一帧确认 k≠0）
		uint32_t motion_state;						// 运动状态
		float azm_deg;								// 方向角度
#if  NUM_VX_EL_SF1 != 0
		float el_deg;								// 俯仰角度
#endif
		float x_rcs;								// 位置信息（内部使用）
		float y_rcs;
		float x_output;								// 位置信息（外部输出）
		float y_output;
		/* 跟踪相关字段 */
		bool isInStaticZone;						// 是否在静止区域
		int assocStatus;							// 关联状态
		int assocTrkID[MAX_ASSOC_TRK_FOR_SINGLE_DETS];						// 关联航迹ID[纵向,横向]
		bool isForVlcUpdate[MAX_ASSOC_TRK_FOR_SINGLE_DETS];						// 是否用于速度更新[纵向,横向]
		float assocTrkVlc[MAX_ASSOC_TRK_FOR_SINGLE_DETS];						// 关联航迹速度[纵向,横向] (m/s)
		float assocTrkVlcDiff[MAX_ASSOC_TRK_FOR_SINGLE_DETS];					// 关联速度差[纵向,横向] (m/s)
	} DetObj;

	// 单帧检测点结构体
	typedef struct {
		unsigned int numDets;						// 检测点数量
		unsigned int numStaticDets;					// 静态检测点数量
		DetObj detObj[MAX_DETECTIONS];				// 检测点数组
	} DetInfo;

	// 帧缓存结构体
	typedef struct {
		DetInfo detInfo_prev;						// 上一帧检测信息（与detInfo同构，memcpy直拷）
		int     numDets_sf0;						// sf0检测点数（sf1从索引numDets_sf0开始）
		float   v_max_prev[NUM_SUBFRAMES];			// 上一帧各子帧最大不模糊速度
		float   dopRes_prev[NUM_SUBFRAMES];			// 上一帧各子帧速度分辨率
		float	rngRes_prev[NUM_SUBFRAMES];			// 上一帧各子帧距离分辨率
		int     frmID_prev;							// 上一帧帧号（用于判断帧是否连续）
		bool    is_valid;							// 缓存是否有效
	} FrameCache;

	/* +++++++++++  4. 数据结构体定义(跟踪部分) +++++++++++ */
	// 自车速度估计结果结构体
	typedef struct {
		float vx_rcs;								// 雷达坐标系纵向速度 (m/s)
		float vy_rcs;								// 雷达坐标系横向速度 (m/s)
		bool  isAvailable;							// 估计结果是否有效
		bool  inlierMask[MAX_DETECTIONS];			// 内点标志，索引与输入检测点对齐（供后续其它模块使用，保留）
		int   numInliers;
	} EgoVlcResult;

	// 航迹起始：聚类结构体
	typedef struct {
		int clusterID;								// 聚类ID
		int numDetsIncluster;						// 聚类内检测点数量
		float snr;									// SNR
		float pwr;									// 功率
		float rng;									// 距离 (m)
		float refPt_x_rcs;							// 参考点 RCS-X (m)
		float refPt_y_rcs;							// 参考点 RCS-Y (m)
		float azm;									// 方位角 (deg)
		float length;								// 长度 (m)
		float width;								// 宽度 (m)
		float radVlc;								// 径向速度 (m/s)
		bool isVlcEstAvailiable;					// 速度估计是否可用
		bool isInitTrk;								// 是否已初始化航迹
		float vx_rcs;								// 纵向速度 RCS (m/s)
		float vy_rcs;								// 横向速度 RCS (m/s)
		float heading_rcs;							// 航向角 (deg)
	} ClusterObj;

	// 当前帧航迹框参数
	typedef struct {
		float x_rcs;								// 框中心 RCS-X (m)
		float y_rcs;								// 框中心 RCS-Y (m)
		float x_tcs;								// 框中心 TCS-X (m)
		float y_tcs;								// 框中心 TCS-Y (m)
		float length_tcs;							// 框长度 TCS (m)
		float width_tcs;							// 框宽度 TCS (m)
	} CurFrmBoxParam;

	// 历史关联检测点记录
	typedef struct {
		int numDetsInRepo;							// 记录中检测点数量
		float snr[MAX_ASSOC_DETS_RECORD];			// SNR历史记录
		float azm_deg[MAX_ASSOC_DETS_RECORD];		// 方位角历史记录 (deg)
		float vlc[MAX_ASSOC_DETS_RECORD];			// 速度历史记录 (m/s)
		float x_rcs[MAX_ASSOC_DETS_RECORD];			// RCS-X历史记录 (m)
		float y_rcs[MAX_ASSOC_DETS_RECORD];			// RCS-Y历史记录 (m)
	} AssocDetRec;

	// 航迹历史记录
	typedef struct {
		int numFrmRec;								// 记录帧数
		int repoIdx;								// 记录仓库索引
		float vx_rcs[MAX_FRM_TRK_HIST_REC];			// 纵向速度历史 (m/s)
		float vy_rcs[MAX_FRM_TRK_HIST_REC];			// 横向速度历史 (m/s)
	} TrkHistRec;

	// 单航迹结构体
	typedef struct {
		bool isvalid;								// 航迹有效性
		float pScore;								// 航迹概率得分
		uint32_t trkID;								// 航迹ID
		uint32_t age;								// 航迹生存帧数
		float snr;									// 信噪比
		float pwr;									// 功率
		float rng;									// 距离 (m)
		float azm;									// 方位角 (deg)
		float radVlc;								// 径向速度 (m/s)
		float x_rcs;								// RCS坐标系 X (m)
		float y_rcs;								// RCS坐标系 Y (m)
		float vx_rcs;								// 纵向速度 RCS (m/s)
		float vy_rcs;								// 横向速度 RCS (m/s)
		float ax_rcs;								// 纵向加速度 RCS (m/s²)
		float ay_rcs;								// 横向加速度 RCS (m/s²)
		float length_tcs;							// 长度 TCS (m)
		float width_tcs;							// 宽度 TCS (m)
		float lengthA_tcs;							// 前向长度 TCS (m)
		float lengthB_tcs;							// 后向长度 TCS (m)
		float widthA_tcs;							// 左侧宽度 TCS (m)
		float widthB_tcs;							// 右侧宽度 TCS (m)
		float maxLen_tcs;							// 最大长度 TCS (m)
		float maxWid_tcs;							// 最大宽度 TCS (m)
		int refEdgeLen;								// 长度参考边
		int refEdgeWid;								// 宽度参考边
		float v_mag;								// 速度幅值 (m/s)
		float vx_rcs_win;							// 窗平滑后纵向速度 (m/s)
		float vy_rcs_win;							// 窗平滑后横向速度 (m/s)
		float heading_rcs;							// 航向角 (deg)
		int lastUpdateFrmID;						// 最后更新帧ID
		int curFrmUpdateState;						// 当前帧更新状态
		int track_state;							// 航迹状态 (Header/New/Mature)
		int motion_state;							// 运动状态 (Static/Stop/Move)
		bool isUnstable;							// 是否不稳定
		bool isGoodLongMeas;						// 是否良好纵向测量
		bool isGoodLatMeas;							// 是否良好横向测量
		bool isFOVEdgeInit;							// 是否在FOV边缘初始化
		int MSTransCnt;								// 运动状态转换计数
		int assocStaticOnlyCnt;						// 连续关联静止点帧计数
		int updateMissCnt;							// 更新缺失计数
		int numAssocDets;							// 关联检测点数量
		int numAssocDetsForUpdate;					// 用于更新的关联检测点数量
		float assocVlcMean;							// 关联速度均值 (m/s)
		float innerBox_x_dist;						// 内框纵向距离 (m)
		float innerBox_y_dist;						// 内框横向距离 (m)
		float x_output;								// 输出 X (m)
		float y_output;								// 输出 Y (m)
		float vx_output;							// 输出纵向速度 (m/s)
		float vy_output;							// 输出横向速度 (m/s)
		float heading_output;						// 输出航向角 (rad)
		float tcsTransMat[2][2];					// RCS←→TCS坐标转换矩阵
		float assocDetVlcDiffForUpdate[MAX_DETS_FOR_TRK_UPDATE];				// 关联检测速度差 (m/s)
		int assocDetIDForUpdate[MAX_DETS_FOR_TRK_UPDATE];						// 关联检测ID列表
		float stateErrCov[NUM_TRACKER_STATES][NUM_TRACKER_STATES];				// 状态误差协方差矩阵
		TrkHistRec trkHistRec;						// 航迹速度历史
		AssocDetRec assocDetRec;					// 关联检测记录
		CurFrmBoxParam curFrmBoxParam;				// 当前帧框参数
	} TrkObj;

	// 航迹信息
	typedef struct {
		uint32_t numTrks;							// 航迹数量
		uint32_t trkPriList[MAX_TRACKS];			// 航迹优先级列表
		TrkObj trkObj[MAX_TRACKS];					// 航迹对象数组
	} TrkInfo;

	// 航迹ID管理
	typedef struct {
		int topIdx;									// 栈顶索引
		int maxTrackID;								// 最大航迹ID
		int trackIDList[MAX_TRACKS];				// 航迹ID列表
	} TrackIDManager;

	/* +++++++++++++  5. 数据结构体定义(跟踪辅助)  +++++++++++++ */
	// 航迹关联结构体
	typedef struct {
		float trk_x_tcs;							// 航迹位置 TCS-X (m)
		float trk_y_tcs;							// 航迹位置 TCS-Y (m)
		float det_x_tcs;							// 检测位置 TCS-X (m)
		float det_y_tcs;							// 检测位置 TCS-Y (m)
		float innerAssocBox_x_dist;					// 内关联框纵向半长 (m)
		float innerAssocBox_y_dist;					// 内关联框横向半长 (m)
		float outerAssocBox_x_dist;					// 外关联框纵向半长 (m)
		float outerAssocBox_y_dist;					// 外关联框横向半长 (m)
		float det_inBoxAttr;						// 检测点框属性
	} TrkAssocInfo;

	// 航迹关联信息记录结构体
	typedef struct {
		bool isGetLongClosest;						// 是否已获取纵向最近点
		bool isGetLatClosest;						// 是否已获取横向最近点
		bool isAssocStaticOnly;						// 是否仅关联静态点
		int numOuterDets;							// 外框内检测点数
		int numInnerDets;							// 内框内检测点数
		float trk_x_tcs;							// 航迹位置 TCS-X (m)
		float trk_y_tcs;							// 航迹位置 TCS-Y (m)
		float closet_x_tcs;							// 最近点 TCS-X (m)
		float closet_y_tcs;							// 最近点 TCS-Y (m)
		float innerDetRatio;						// 内框检测点比例
		float meas_x_tcs;							// 测量值 TCS-X (m)
		float meas_y_tcs;							// 测量值 TCS-Y (m)
		float maxTrk_x_tcs;							// 航迹最大 TCS-X (m)
		float minTrk_x_tcs;							// 航迹最小 TCS-X (m)
		float maxTrk_y_tcs;							// 航迹最大 TCS-Y (m)
		float minTrk_y_tcs;							// 航迹最小 TCS-Y (m)
		int assocRel[MAX_ASSOC_DETS_FOR_SINGLE_TRK];							// 关联关系标记
		int elemAttr[MAX_ASSOC_DETS_FOR_SINGLE_TRK];							// 元素属性
		int assocDetIDs[MAX_ASSOC_DETS_FOR_SINGLE_TRK];							// 关联检测ID列表
		int rejectFlag[MAX_ASSOC_DETS_FOR_SINGLE_TRK];							// 拒绝标记
		float det_x_tcs[MAX_ASSOC_DETS_FOR_SINGLE_TRK];							// 检测点 TCS-X (m)
		float det_y_tcs[MAX_ASSOC_DETS_FOR_SINGLE_TRK];							// 检测点 TCS-Y (m)
	} TrkAssocRecInfo;




	/* ------------------------------------------------------------------ */
	/*  WarnResult — 预警输出                                               */
	/*  对应 MATLAB warningDetection 返回的 warnResult struct               */
	/*                                                                     */
	/*  各字段含义：                                                        */
	/*    BSD_left/right  : 盲区警示（Blind Spot Detection）               */
	/*    AOA_left/right  : 超车后警示（Approaching from Overtake）        */
	/*    LCA_left/right  : 车道变更辅助（Lane Change Assist）             */
	/*    RCW             : 正后方碰撞预警（Rear Collision Warning）       */
	/*    TTC_min         : 所有警示目标中最小 TTC (s)，无警示时为 FLT_MAX */
	/*    *_IDs / *_cnt   : 触发各类预警的 trkID 列表及数量               */
	/* ------------------------------------------------------------------ */
	typedef struct {
		bool  BSD_left;
		bool  BSD_right;
		bool  AOA_left;
		bool  AOA_right;
		bool  LCA_left;
		bool  LCA_right;
		bool  RCW;
		bool  collision;
		float TTC_min;

		uint8_t BSD_left_cnt;
		uint8_t BSD_right_cnt;
		uint8_t AOA_left_cnt;
		uint8_t AOA_right_cnt;
		uint8_t LCA_left_cnt;
		uint8_t LCA_right_cnt;
		uint8_t RCW_cnt;
		uint8_t collision_cnt;

		uint8_t BSD_left_IDs[MAX_WARN_IDS];
		uint8_t BSD_right_IDs[MAX_WARN_IDS];
		uint8_t AOA_left_IDs[MAX_WARN_IDS];
		uint8_t AOA_right_IDs[MAX_WARN_IDS];
		uint8_t LCA_left_IDs[MAX_WARN_IDS];
		uint8_t LCA_right_IDs[MAX_WARN_IDS];
		uint8_t RCW_IDs[MAX_WARN_IDS];
		uint8_t collision_IDs[MAX_WARN_IDS];

		uint8_t LCA_left_level;  /* 0=无警示, 1=常亮(10-70m), 2=闪烁(0-10m) */
		uint8_t LCA_right_level; /* 0=无警示, 1=常亮(10-70m), 2=闪烁(0-10m) */
	} WarnResult;

	/* +++++++++++++  6. 静止目标带结构体定义 +++++++++++++ */
	// 静止检测点仓库 (循环缓冲区)
	typedef struct {
		uint32_t numMembers;                        // 仓库内有效成员数
		uint32_t storageIdx;                        // 循环写入起始索引 (-1=已满) 
		uint32_t age[MAX_STATIC_DET_INFO_REC];      // 各槽位存活帧数 (0=空闲) 
		uint32_t assocID[MAX_STATIC_DET_INFO_REC];  // 关联的静止带ID (0=未关联) 
		float x_rcs[MAX_STATIC_DET_INFO_REC];       // 纵向位置 (m) 
		float y_rcs[MAX_STATIC_DET_INFO_REC];       // 横向位置 (m) 
	} StaticDetInfo;

	// 单条静止目标带
	typedef struct {
		int missCnt;								// 连续未关联计数
		bool isValid;								// 是否有效 
		int age;									// 已存在帧数 
		float max_x_rcs;							// 纵向范围最大值 (EMA平滑, m) 
		float min_x_rcs;							// 纵向范围最小值 (EMA平滑, m) 
		float coeffA;								// 直线斜率 y = coeffA*x + coeffB 
		float coeffB;								// 直线截距 
		float errCov[4];							// 2x2参数协方差矩阵 (Block RLS递推, row-major) 
	} StaticZoneObj;

	// 静止目标带管理信息
	typedef struct {
		int updateAge;                              // 距上次更新帧数
		int numStaticZone;                          // 当前有效静止带数 
		//float staticZoneTempBuffer[MAX_STATIC_DET_INFO_REC * 2];				// 工作缓冲区(静止点收集/聚类/RANSAC复用)
		StaticZoneObj staticZoneObj[MAX_STATIC_ZONES];							// 静止目标带数组 
	} StaticZoneInfo;

	
	/* +++++++++++++  7. 全局上下文结构体定义 +++++++++++++ */
	typedef struct {
		BasicParams		basicParams;				// 基本参数
		WaveParams		waveParams;					// 波形参数
		RadarInfo		radarInfo;					// 帧信息
		FrmInfo			frmInfo;					// 帧信息
		EgoVehInfo		egoVehInfo;					// 自车信息
		AlgInfo			algInfo;					// 算法参数信息
		AdcData			adcData;					// ADC数据
		CfarCfg			rangeCfarCfg;				// 距离CFAR配置
		CfarCfg			dopplerCfarCfg;				// 速度CFAR配置
		DetInfoRD		detInfoRD;					// 检测点RD信息
		DetInfo			detInfo;					// 检测点信息
		FrameCache		prevFrameCache;				// 上一帧缓存
		TrkInfo			trkInfo;					// 航迹信息
		TrackIDManager	trackIDManager;				// 航迹ID管理
		StaticDetInfo	staticDetInfo;				// 静止检测点仓库
		StaticZoneInfo	staticZoneInfo;				// 静止目标带管理信息
		WarnResult		warnResult;					// 预警结果
	} GlbCtx;

#ifdef __cplusplus
}
#endif
#endif // RADAR_TYPES_H