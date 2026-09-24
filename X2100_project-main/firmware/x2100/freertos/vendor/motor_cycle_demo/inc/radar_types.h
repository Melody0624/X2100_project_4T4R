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
#define WARNING_ENABLE						1       // 预警功能开关（BSD / AOA / LCA / RCW / TTC）
#define WARNING_PROFILE_HONDA				1       // 预警策略选择：0=零行(LingXing)默认策略，1=新大洲本田(honda)策略
#define EGO_VLC_ENABLE					    1       // 主车速度估計開關

#define SAVE_RAW_DATA						1		// 1 : 保存原始ADC数据到TF卡, 0 : 保存处理后的数据
#define ADC_REPLAY				            0
#define DOT_REPLAY				            0		// 1 : 从 .dat 文件读取 ADC 数据并处理，0 : 从雷达芯片实时读取 ADC 数据并处理

#define USE_USB_UART			            1	    // 使用USB串口输出数据（如果为0则使用普通UART输出）
#define USE_USB_OUTPUT                      1       // 使用USB串口输出数据（如果为0则使用普通UART输出）
#define UART_ADC_SEND 			            0

#define cheetah_128_256_config				1
#define cheetah_128_512_config				2

// #define CHEETAH_CONFIG 		cheetah_128_256_config
#define CHEETAH_CONFIG 		cheetah_128_512_config

typedef ne10_fft_cpx_float32_t fft_cpx_f32;

#define FFT_ARRAY_GUARD_LEN     		    4
#define PI									3.14159265358979323846f
#define PI_2								1.57079632679489661923f
#define LIGHT_SPEED							299792458.0f
#define DATA_TYPE_DAT_ADC					0
#define DATA_TYPE_DAT_DETINFO				1
#define DATA_TYPE_MAT_DETINFO				2
// 窗函数
#if CHEETAH_CONFIG == cheetah_128_512_config
#define WIN_LEN_RANGE_FFT					506		// range-fft 窗长度
#elif CHEETAH_CONFIG == cheetah_128_256_config
#define WIN_LEN_RANGE_FFT					256		// range-fft 窗长度
#endif
#define WIN_LEN_DOPPLER_FFT					128		// doppler-fft 窗长度
#define WIN_LEN_AZIMUTH_FFT					8		// azimuth-fft 窗长度
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
// 检测参数
#define	IS_ANG_ACC_IMPROVE_INTERP			false   // 是否使用角度FFT插值提升角度估计精度（true: 使用插值，false: 不使用插值）
#define IS_CRT								false   // 是否帧间速度解模糊 
#define RANGE_CFAR_ENABLE					true    // 距离CFAR使能
#define DOPPLER_CFAR_ENABLE					true    // 速度CFAR使能
#define CFAR_MODE_CA						0       // CA-CFAR
#define CFAR_MODE_CAGO						1       // GO-CFAR
#define CFAR_MODE_CASO						2       // SO-CFAR
// 跟踪参数
#define TRACK_STATE_HEADER     				0       // 航迹头 age < 3
#define TRACK_STATE_NEW        				1       // 新航迹 age < 成熟标准
#define TRACK_STATE_MATURE     				2       // 成熟航迹 age >= 成熟标准
#define MOTION_STATUS_STATIC     			0       // 静止航迹
#define MOTION_STATUS_STOP       			1       // 停止航迹（运动->静止）
#define MOTION_STATUS_MOVE       			2       // 运动航迹
#define ASSOC_STATUS_NON         			0       // 未关联
#define ASSOC_STATUS_COMPETITIVE 			1       // 弱关联（竞争关联）
#define ASSOC_STATUS_ASSOC       			2       // 强关联
#define ASSOC_STATUS_BAD         			3       // 不正常点，不允许关联
#define UPDATE_STATUS_MISS       			0       // 本帧未更新
#define UPDATE_STATUS_UPDATE     			1       // 本帧已经更新
#define EDGE_LEN_A               			0       // 参考边: 纵向A
#define EDGE_LEN_B               			1       // 参考边: 纵向B
#define EDGE_WID_A               			2       // 参考边: 横向A
#define EDGE_WID_B               			3       // 参考边: 横向B
#define NUM_TRACKER_STATES       			4       // 航迹状态属性数量
#define MAX_DETS_FOR_TRK_UPDATE  			5       // 用于航迹测量向量的检测点最大数量
#define MAX_FRM_TRK_HIST_REC     			5       // 航迹历史信息记录上限（帧数量）
#define MAX_ASSOC_DETS_FOR_SINGLE_TRK		10		// 单个航迹关联检测点最大数量

// 辅助宏
#define SQ(x)	((x)*(x))						// 平方




	//  窗函数数组
	extern float* winRng;
	extern float* winDop;
	extern float* winAz;

	/* 全局阈值 */
	extern const float th_vcsStatic;				// 速度阈值：静止/运动
	extern const float th_FOVEdge;
	extern const float th_FOVEdge_bias;
	extern const float th_rngEdge_near;
	extern const int th_maxAgeAsHeader_near;
	extern const int th_maxAgeAsHeader_far;
	extern const float th_rngEdge_far;

	extern const int thr_motionStatusCnt_staticTomove;  // 静止→运动 状态切换计数阈值
	extern const int thr_motionStatusCnt_moveTostop;    // 运动→静止 状态切换计数阈值

	/* 基本参数结构体 */
	typedef struct {
		bool cascade;           // 是否级联模式
		int numMMIC;            // 级联芯片数量
		int numRXPerMMIC;       // 单芯片RX数量
		int numTxs;             // 雷达发射天线数量
		int numRxs;             // 雷达接收天线数量
		float minElemSpacing;   //最小阵元间距（unit: lambda）
	} BasicParams;

	/* 波形参数 */
	typedef struct {
		float sampling_freq;	// 子帧ADC采样率
		int frame_class;        // 速度解模糊AB波标识 0:A波，1:B波
		int mimo_mode;          // MIMO模式 0:'ddm',1:'tdm'
		float frame_period;     // 帧周期（sec）
		int numSubFrms;         // 子帧数量
		int numChirps;          // Chirp数量
		int adcLen;             // 单Chirp的ADC数据长度
		float central_freq;     // 中心频率
		float chirp_period;     // 子帧chirp周期
		float slope;            // 调频斜率
		int numEmptyBands;      // 空条带数量
		float wavelength;       // 波长
		float rngRes;           // 距离分辨率
		float dopRes;           // 速度分辨率
		float maxUmAmbVlc;      // 最大不模糊速度
	} WaveParams;

	// 雷达信息
	typedef struct {
		bool isCompInstallAng;      	// 是否安装角度自校准
		bool isFlipInstall_azm;     	// 是否雷达反装
		bool isInHighAlt;           	// 雷达是否处于高空
		float installAngComp_deg;   	// 安装角度
		float mountingHeightSqr;    	// 高空补偿
		float* vxPosn;					// 天线位置
		int* logicIdx_reorder;			// 逻辑关系序列

		float longOffset;           	// 纵向偏移（正：天线在前，负：天线在后）

		bool isDetVlcPreCompensated;	// 检测点 vlc 是否已为对地速度（DAT_DETINFO 回放：
										// 韧体已做过 ego 补偿，检测后处理不得重复补偿）

		float vx_radar_vcs;         	// 雷达速度（VCS，当前帧）
		float vy_radar_vcs;
		float vx_radar_vcs_pre;     	// 雷达速度（VCS，前一帧）
		float vy_radar_vcs_pre;
		float x_radar_rcs_frmDiff;  	// 帧间纵向位移（RCS）
		float y_radar_rcs_frmDiff;  	// 帧间横向位移（RCS）
		float ang_radar_vcs_frmDiff;	// 帧间坐标原点位移导致的角度变化（rad）
		float ang_radar_frmDiff_rad;    // 帧间角度变化（rad）
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
		int frmID;					// 本帧ID
		int lastFrmID;				// 前一帧ID
	} FrmInfo;

	/* 主车信息结构体 */
	typedef struct {
		float egoSpeed;             // 自车（雷达处）纵向速度分量（m/s），VCS坐标系
		float egoSpeedLat;          // 自车（雷达处）横向速度分量（m/s），VCS坐标系；来自 RANSAC 对静态点的 2D 拟合
		float yawRate;              // 横摆角速度（rad/s）
		float yawRate_pre;          // 前一帧横摆角速度
		float rollRate;             // 横滚角速度（rad/s）
		float rollRate_pre;         // 前一帧横滚角速度
	} EgoVehInfo;

	// 算法参数信息
	typedef struct {
		float* angFFTvec;           // 角度FFT向量
	}AlgInfo;

	// ADC数据结构体
	typedef struct {
		bool isSubframe1Exist;      // 子帧是否存在
		float* sf1;                 // 子帧ADC
	} AdcData;

	// CFAR配置
	typedef struct {
		bool enable;				// 是否使能
		int winlen;					// 窗口长度
		int guardlen;				// 守卫长度
		int mode;					// 模式 0:CA-CFAR, 1:GO-CFAR
		float thold;				// 检出阈值
		bool isPeakdet;				// 是否峰值检测
	} CfarCfg;

	// 单个检测结构体 RD
	typedef struct {
		int rngIdx;					// 距离索引
		int dopIdx;					// 速度索引
		float rngBinIdx;			// 距离单元索引
		float dopBinIdx_dec;		// 速度单元索引（解模糊）
		int subbandIdx;				// 子带索引
		float pwr;					// Power
		float snr;					// SNR
		bool isPeak;				// 是否峰值
	} DetObjRD;

	// 单个检测结构体 点云
	typedef struct {
		int relRDIdx;               // 关联的RD检测索引
		float pwr;                  // 功率
		float snr;                  // 信噪比
		bool isPeak;                // 是否峰值
		float rng;                  // 距离 (m)
		float vlc_amb;              // 模糊速度 (m/s)
		float vlc;                  // 真实速度 (m/s)
		int vlc_disAmb_conf;        // 速度解模糊置信度
		int vlc_disAmb_fac;         // 速度解模糊因子
		float azm_deg;              // 方位角 (deg)
		float x_rcs;                // RCS坐标系 X (m)
		float y_rcs;                // RCS坐标系 Y (m)
		float z_rcs;                // RCS坐标系 Z (m)
		float x_output;             // 输出坐标系 X (m)
		float y_output;             // 输出坐标系 Y (m)
		int motion_state;           // 运动状态
		bool isInStaticZone;        // 是否在静止区域
		int assocStatus;            // 关联状态
		int assocTrkID[2];          // 关联航迹ID[纵向,横向]
		bool isForVlcUpdate[2];     // 是否用于速度更新[纵向,横向]
		float assocTrkVlc[2];       // 关联航迹速度[纵向,横向] (m/s)
		float assocTrkVlcDiff[2];   // 关联速度差[纵向,横向] (m/s)
		float secondAng;            // 第二角度（双角度估计）
	} DetObj;

	// 单帧检测点结构体
	typedef struct {
		int numDets;						// 检测点数量
		int numStaticDets;					// 静态检测点数量
		DetObj detObj[MAX_DETECTIONS];		// 检测点数组
	} DetInfo;

	// RD检测点结构体
	typedef struct {
		int numDets;                        // RD检测点数量
		DetObjRD detObjRD[MAX_DETECTIONS];  // RD检测点数组
	} DetInfoRD;

	// 航迹ID管理
	typedef struct {
		int topIdx;							// 栈顶索引
		int maxTrackID;						// 最大航迹ID
		int trackIDList[MAX_TRACKS];		// 航迹ID列表
	} TrackIDManager;

	// 当前帧航迹框参数
	typedef struct {
		float x_rcs;				// 框中心 RCS-X (m)
		float y_rcs;				// 框中心 RCS-Y (m)
		float x_tcs;				// 框中心 TCS-X (m)
		float y_tcs;				// 框中心 TCS-Y (m)
		float length_tcs;			// 框长度 TCS (m)
		float width_tcs;			// 框宽度 TCS (m)
	} CurFrmBoxParam;

	// 历史关联检测点记录
	typedef struct {
		int numDetsInRepo;						// 记录中检测点数量
		float snr[MAX_ASSOC_DETS_RECORD];		// SNR历史记录
		float azm_deg[MAX_ASSOC_DETS_RECORD];	// 方位角历史记录 (deg)
		float vlc[MAX_ASSOC_DETS_RECORD];		// 速度历史记录 (m/s)
		float x_rcs[MAX_ASSOC_DETS_RECORD];		// RCS-X历史记录 (m)
		float y_rcs[MAX_ASSOC_DETS_RECORD];		// RCS-Y历史记录 (m)
	} AssocDetRec;

	// 航迹历史记录
	typedef struct {
		int numFrmRec;                      // 记录帧数
		int repoIdx;                        // 记录仓库索引
		float vx_rcs[MAX_FRM_TRK_HIST_REC]; // 纵向速度历史 (m/s)
		float vy_rcs[MAX_FRM_TRK_HIST_REC]; // 横向速度历史 (m/s)
	} TrkHistRec;

	// 单航迹结构体
	typedef struct {
		bool isvalid;               // 航迹有效性
		float pScore;               // 航迹概率得分
		int trkID;                  // 航迹ID
		int age;                    // 航迹生存帧数
		float snr;                  // 信噪比
		float pwr;                  // 功率
		float rng;                  // 距离 (m)
		float azm;                  // 方位角 (deg)
		float radVlc;               // 径向速度 (m/s)
		float x_rcs;                // RCS坐标系 X (m)
		float y_rcs;                // RCS坐标系 Y (m)
		float vx_rcs;               // 纵向速度 RCS (m/s)
		float vy_rcs;               // 横向速度 RCS (m/s)
		float ax_rcs;               // 纵向加速度 RCS (m/s²)
		float ay_rcs;               // 横向加速度 RCS (m/s²)
		float length_tcs;           // 长度 TCS (m)
		float width_tcs;            // 宽度 TCS (m)
		float lengthA_tcs;          // 前向长度 TCS (m)
		float lengthB_tcs;          // 后向长度 TCS (m)
		float widthA_tcs;           // 左侧宽度 TCS (m)
		float widthB_tcs;           // 右侧宽度 TCS (m)
		float maxLen_tcs;           // 最大长度 TCS (m)
		float maxWid_tcs;           // 最大宽度 TCS (m)
		int refEdgeLen;             // 长度参考边
		int refEdgeWid;             // 宽度参考边
		float v_mag;                // 速度幅值 (m/s)
		float vx_rcs_win;           // 窗平滑后纵向速度 (m/s)
		float vy_rcs_win;           // 窗平滑后横向速度 (m/s)
		float heading_rcs;          // 航向角 (rad)
		int lastUpdateFrmID;        // 最后更新帧ID
		int curFrmUpdateState;      // 当前帧更新状态
		int track_state;			// 航迹状态 (Header/New/Mature)
		int motion_state;           // 运动状态 (Static/Stop/Move)
		bool isUnstable;            // 是否不稳定
		bool isGoodLongMeas;        // 是否良好纵向测量
		bool isGoodLatMeas;         // 是否良好横向测量
		bool isFOVEdgeInit;         // 是否在FOV边缘初始化
		int MSTransCnt;             // 运动状态转换计数
		int assocStaticOnlyCnt;     // 连续关联静止点帧计数
		int updateMissCnt;          // 更新缺失计数
		int numAssocDets;           // 关联检测点数量
		int numAssocDetsForUpdate;  // 用于更新的关联检测点数量
		float assocVlcMean;         // 关联速度均值 (m/s)
		float innerBox_x_dist;      // 内框纵向距离 (m)
		float innerBox_y_dist;      // 内框横向距离 (m)
		float x_output;             // 输出 X (m)
		float y_output;             // 输出 Y (m)
		float vx_output;            // 输出纵向速度 (m/s)
		float vy_output;            // 输出横向速度 (m/s)
		float heading_output;       // 输出航向角 (rad)
		float tcsTransMat[2][2];    // RCS←→TCS坐标转换矩阵
		float assocDetVlcDiffForUpdate[MAX_DETS_FOR_TRK_UPDATE]; // 关联检测速度差 (m/s)
		int assocDetIDForUpdate[MAX_DETS_FOR_TRK_UPDATE];       // 关联检测ID列表
		float stateErrCov[NUM_TRACKER_STATES][NUM_TRACKER_STATES]; // 状态误差协方差矩阵
		TrkHistRec trkHistRec;      // 航迹速度历史
		AssocDetRec assocDetRec;    // 关联检测记录
		CurFrmBoxParam curFrmBoxParam; // 当前帧框参数
	} TrkObj;

	// 航迹信息
	typedef struct {
		int numTrks;				// 航迹数量
		int trkPriList[MAX_TRACKS];	// 航迹优先级列表
		TrkObj trkObj[MAX_TRACKS];	// 航迹对象数组
	} TrkInfo;

	// 航迹关联结构体
	typedef struct {
		float trk_x_tcs;            // 航迹位置 TCS-X (m)
		float trk_y_tcs;            // 航迹位置 TCS-Y (m)
		float det_x_tcs;            // 检测位置 TCS-X (m)
		float det_y_tcs;            // 检测位置 TCS-Y (m)
		float innerAssocBox_x_dist; // 内关联框纵向半长 (m)
		float innerAssocBox_y_dist; // 内关联框横向半长 (m)
		float outerAssocBox_x_dist; // 外关联框纵向半长 (m)
		float outerAssocBox_y_dist; // 外关联框横向半长 (m)
		float det_inBoxAttr;        // 检测点框属性
	} TrkAssocInfo;

	// 航迹关联信息记录结构体
	typedef struct {
		float trk_x_tcs;                                    // 航迹位置 TCS-X (m)
		float trk_y_tcs;                                    // 航迹位置 TCS-Y (m)
		int assocRel[MAX_ASSOC_DETS_FOR_SINGLE_TRK];        // 关联关系标记
		float det_x_tcs[MAX_ASSOC_DETS_FOR_SINGLE_TRK];     // 检测点 TCS-X (m)
		float det_y_tcs[MAX_ASSOC_DETS_FOR_SINGLE_TRK];     // 检测点 TCS-Y (m)
		int elemAttr[MAX_ASSOC_DETS_FOR_SINGLE_TRK];        // 元素属性
		int assocDetIDs[MAX_ASSOC_DETS_FOR_SINGLE_TRK];     // 关联检测ID列表
		int rejectFlag[MAX_ASSOC_DETS_FOR_SINGLE_TRK];      // 拒绝标记
		float closet_x_tcs;         // 最近点 TCS-X (m)
		float closet_y_tcs;         // 最近点 TCS-Y (m)
		int isGetLongClosest;       // 是否已获取纵向最近点
		int isGetLatClosest;        // 是否已获取横向最近点
		int numOuterDets;           // 外框内检测点数
		int numInnerDets;           // 内框内检测点数
		float innerDetRatio;        // 内框检测点比例
		int isAssocStaticOnly;      // 是否仅关联静态点
		float meas_x_tcs;           // 测量值 TCS-X (m)
		float meas_y_tcs;           // 测量值 TCS-Y (m)
		float maxTrk_x_tcs;         // 航迹最大 TCS-X (m)
		float minTrk_x_tcs;         // 航迹最小 TCS-X (m)
		float maxTrk_y_tcs;         // 航迹最大 TCS-Y (m)
		float minTrk_y_tcs;         // 航迹最小 TCS-Y (m)
	} TrkAssocRecInfo;

	// 航迹起始：聚类结构体
	typedef struct {
		int clusterID;              // 聚类ID
		int numDetsIncluster;       // 聚类内检测点数量
		float snr;                  // SNR
		float pwr;                  // 功率
		float rng;                  // 距离 (m)
		float refPt_x_rcs;          // 参考点 RCS-X (m)
		float refPt_y_rcs;          // 参考点 RCS-Y (m)
		float azm;                  // 方位角 (deg)
		float length;               // 长度 (m)
		float width;                // 宽度 (m)
		float radVlc;               // 径向速度 (m/s)
		bool isVlcEstAvailiable;    // 速度估计是否可用
		bool isInitTrk;             // 是否已初始化航迹
		float vx_rcs;               // 纵向速度 RCS (m/s)
		float vy_rcs;               // 横向速度 RCS (m/s)
		float heading_rcs;          // 航向角 (rad)
	} ClusterObj;


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
		float TTC_min;

		uint8_t BSD_left_IDs[MAX_WARN_IDS];
		uint8_t BSD_left_cnt;
		uint8_t BSD_right_IDs[MAX_WARN_IDS];
		uint8_t BSD_right_cnt;
		uint8_t AOA_left_IDs[MAX_WARN_IDS];
		uint8_t AOA_left_cnt;
		uint8_t AOA_right_IDs[MAX_WARN_IDS];
		uint8_t AOA_right_cnt;
		uint8_t LCA_left_IDs[MAX_WARN_IDS];
		uint8_t LCA_left_cnt;
		uint8_t LCA_right_IDs[MAX_WARN_IDS];
		uint8_t LCA_right_cnt;
		uint8_t RCW_IDs[MAX_WARN_IDS];
		uint8_t RCW_cnt;

		uint8_t LCA_left_level;  /* 0=无警示, 1=常亮(10m-时速分段上限), 2=闪烁(0-10m) */
		uint8_t LCA_right_level; /* 0=无警示, 1=常亮(10m-时速分段上限), 2=闪烁(0-10m) */
	} WarnResult;
	
	typedef struct {
		int numMembers;                                     /* 仓库内有效成员数 */
		int storageIdx;                                     /* 循环写入起始索引 (-1=已满) */
		int age[MAX_STATIC_DET_INFO_REC];                   /* 各槽位存活帧数 (0=空闲) */
		float x_rcs[MAX_STATIC_DET_INFO_REC];               /* 纵向位置 (m) */
		float y_rcs[MAX_STATIC_DET_INFO_REC];               /* 横向位置 (m) */
		int assocID[MAX_STATIC_DET_INFO_REC];               /* 关联的静止带ID (0=未关联) */
	} StaticDetInfo;

	// 单条静止目标带
	typedef struct {
		int missCnt;                        /* 连续未关联计数 */
		bool isValid;                       /* 是否有效 */
		int age;                            /* 已存在帧数 */
		float max_x_rcs;                    /* 纵向范围最大值 (EMA平滑, m) */
		float min_x_rcs;                    /* 纵向范围最小值 (EMA平滑, m) */
		float coeffA;                       /* 直线斜率 y = coeffA*x + coeffB */
		float coeffB;                       /* 直线截距 */
		float errCov[4];                    /* 2x2参数协方差矩阵 (Block RLS递推, row-major) */
	} StaticZoneObj;

	// 静止目标带管理信息
	typedef struct {
		int updateAge;                                      // 距上次更新帧数
		int numStaticZone;                                  // 当前有效静止带数 
		float staticZoneTempBuffer[MAX_STATIC_DET_INFO_REC * 2];	// 工作缓冲区(静止点收集/聚类/RANSAC复用)
		StaticZoneObj staticZoneObj[MAX_STATIC_ZONES];      // 静止目标带数组 
	} StaticZoneInfo;

	// 全局上下文结构体
	typedef struct {
		BasicParams basic_params;       // 基本参数
		WaveParams wave_params;         // 波形参数
		FrmInfo frmInfo;                // 帧信息
		RadarInfo radarInfo;            // 雷达信息
		EgoVehInfo egoVehInfo;          // 自车信息
		AlgInfo algInfo;                // 算法参数
		CfarCfg rangeCfarCfg;           // 距离CFAR配置
		CfarCfg dopplerCfarCfg;         // 速度CFAR配置
		AdcData adcData;                // ADC数据
		DetInfoRD detInfoRD;            // RD域检测信息
		DetInfo detInfo;                // 点云检测信息
		TrkInfo trkInfo;                // 航迹信息
		TrackIDManager trackIDManager;  // 航迹ID管理器
		WarnResult warnResult;          // 预警结果
		StaticDetInfo staticDetInfo;    // 静止检测点仓库
		StaticZoneInfo staticZoneInfo;  // 静止目标带管理信息
	} GlbCtx;

#ifdef __cplusplus
}
#endif

#endif // RADAR_TYPES_H
