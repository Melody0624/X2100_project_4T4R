#ifndef MMW_MSG_PKT_H
#define MMW_MSG_PKT_H

#include <stdint.h>
#include "radar_types.h"
#include "warning_detection.h"

/* 使用1字节对齐，避免结构体填充 */
// #pragma pack(push, 1)

#define FILE_MAGIC_WORD                 0xbade5aa5

#define NEW_PROTOCOL_ENABLE             1
/** @brief Output packet length is a multiple of this value, must be power of 2*/
#define MMWDEMO_OUTPUT_MSG_SEGMENT_LEN 32

// #define MMWDEMO_OUTPUT_PADDING_BYTES

typedef enum Mmw_output_message_type_e
{
    /*! @brief   List of detected points */
    MMW_OUTPUT_MSG_DETECTED_POINTS = 1,

    /*! @brief   Range profile */
    MMW_OUTPUT_MSG_RANGE_PROFILE,

    /*! @brief   Noise floor profile */
    MMW_OUTPUT_MSG_NOISE_PROFILE,

    /*! @brief   Samples to calculate static azimuth  heatmap */
    MMW_OUTPUT_MSG_AZIMUT_STATIC_HEAT_MAP,

    /*! @brief   Range/Doppler detection matrix */
    MMW_OUTPUT_MSG_RANGE_DOPPLER_HEAT_MAP,

    /*! @brief   Stats information */
    MMW_OUTPUT_MSG_STATS,

    /*! @brief   List of detected points */
    MMW_OUTPUT_MSG_DETECTED_POINTS_SIDE_INFO,

    /*! @brief   Samples to calculate static azimuth/elevation heatmap,
                 (all virtual antennas exported) - unused in this demo */
    MMW_OUTPUT_MSG_AZIMUT_ELEVATION_STATIC_HEAT_MAP,

    /*! @brief   temperature stats from Radar front end */
    MMW_OUTPUT_MSG_TEMPERATURE_STATS,

    MMW_OUTPUT_MSG_DETECTED_MATRIX,

    MMW_OUTPUT_MSG_ADC_CHIRP,

    MMW_OUTPUT_MSG_ADC_CHIRP_SLAVE,

    MMW_OUTPUT_MSG_ADC_FRAME,

    MMW_OUTPUT_MSG_ADC_FRAME_SLAVE,

    MMW_OUTPUT_MSG_ADC_FRAME_SLAVE2,

    MMW_OUTPUT_MSG_ADC_FRAME_SLAVE3,

    MMW_OUTPUT_MSG_PARSER_PARAMS,

    MMW_OUTPUT_MSG_PARSED_ADC_DATA,

    MMW_OUTPUT_MSG_PARSED_DOPPLER_DATA,

    MMW_OUTPUT_MSG_PARSED_AOA_DATA,

    /*! @brief   Cheetah MotorCycle Demo Detection Info */
    MMW_OUTPUT_MSG_MOTORCYCLE_DETINFO,

    /*! @brief   Cheetah MotorCycle Demo Trackong Info */
    MMW_OUTPUT_MSG_MOTORCYCLE_TRKINFO,

    /*! @brief   Cheetah MotorCycle Demo Ego Velocity Info */
    MMW_OUTPUT_MSG_MOTORCYCLE_EGOVLCINFO,

    /*! @brief   Cheetah MotorCycle Demo Warning Info */
    MMW_OUTPUT_MSG_MOTORCYCLE_WARNINFO,

    MMW_OUTPUT_MSG_MAX
} Mmw_output_message_type;

typedef struct Mmw_output_message_tl_t
{
    /*! @brief   TLV type */
    uint32_t    type;

    /*! @brief   Length in bytes */
    uint32_t    length;

} Mmw_output_message_tl;

typedef struct Mmw_output_message_header_MotorCycle_t
{
    /*! @brief   Output buffer magic word (sync word). It is initialized to  {0x0102,0x0304,0x0506,0x0708} */
    uint16_t    magicWord[4];

    /*! brief   Version: : MajorNum * 2^24 + MinorNum * 2^16 + BugfixNum * 2^8 + BuildNum   */
    uint32_t     version;

    /*! @brief   Total packet length including header in Bytes */
    uint32_t    totalPacketLen;

    // /*! @brief   platform type (上位机自动对齐为4字节) */
    // uint32_t    platform;

    /*! @brief   platform type */
    uint8_t    platform;

    uint8_t    Reserved_0[3];

    /*! @brief   Frame number */
    uint32_t    frameNumber;

    /*! @brief   Number of TLVs (上位机自动对齐为4字节) */
    // uint32_t    numTLVs;

    /*! @brief   Number of TLVs */
    uint8_t    numTLVs;

    uint8_t    Reserved_1[3];

} Mmw_output_message_header;

typedef struct MmwSaveDataHeader
{
    uint32_t magic;
    uint32_t framesNum;
} MmwSaveDataHeader_t;

#if NEW_PROTOCOL_ENABLE
/* ****************************************************************
 * *******************两轮车雷达下位机TLV包格式***********************
 * ****************************************************************/
// 单航迹结构体
typedef struct {
    uint8_t trkID;
    uint16_t x_output;
    uint16_t y_output;
    uint16_t vx_output;
    uint16_t vy_output;
    uint8_t motion_state;
    uint8_t isvalid;
    uint8_t maxLen_tcs;
    uint8_t maxWid_tcs;
    uint16_t heading_output;
    uint8_t Reserved;
} MotorCycle_TrkObj;

typedef struct {
    uint8_t Reserved[3];
    uint8_t numTrks;
    uint32_t MeasCounter;
} TrkInfo_Header;

// 航迹信息
typedef struct {
    TrkInfo_Header header;
    MotorCycle_TrkObj trkObj[MAX_TRACKS];
} Mmw_output_message_TrkInfo;

// 单个检测结构体 点云
typedef struct {
    uint16_t relRDIdx;
    uint8_t motion_state;
	uint8_t isPeak;
    float vlc;
    float x_output;
    float y_output;
    float rng;
	float azm_deg;
	int16_t vlc_amb;     // 2位小数, (int16)round(vlc_amb_float * 100)
	int16_t x_rcs;       // 2位小数, (int16)round(x_rcs * 100)
	int16_t y_rcs;       // 2位小数, (int16)round(y_rcs * 100)
    uint8_t vlc_disAmb_conf;  // 0位小数, (uint8)round(vlc_disAmb_conf)
	uint8_t vlc_disAmb_fac;   // 0位小数, (uint8)round(vlc_disAmb_fac)
	uint16_t pwr;        // 2位小数, (uint16)round(pwr_float * 100)
	uint16_t snr;        // 2位小数, (uint16)round(snr_float * 100)
    uint32_t Reserved;
} MotorCycle_DetObj;

typedef struct {
    int relRDIdx;            // 第二角度（双角度估计）
    float vlc;
    float x_output;
    float y_output;
    int motion_state;
    float pwr;
    float snr;
    bool isPeak;
    float rng;
    float vlc_amb;
    int vlc_disAmb_conf;
    int vlc_disAmb_fac;
    float azm_deg;
    float x_rcs;
    float y_rcs;
} MotorCycle_DetObj_Read;

// 单帧检测点结构体
typedef struct {
    int numDets;						// 检测点数量
    int numStaticDets;					// 静态检测点数量
    MotorCycle_DetObj_Read detObj[MAX_DETECTIONS];		// 检测点数组
} DetInfo_Read;

typedef struct {
    uint16_t numDets;
    uint16_t Reserved;
} DetInfo_Header;

// 单帧检测点结构体
typedef struct {
    DetInfo_Header header;
    MotorCycle_DetObj detObj[MAX_DETECTIONS];
} Mmw_output_message_DetInfo;
#else
/* ****************************************************************
 * *******************两轮车雷达下位机TLV包格式***********************
 * ****************************************************************/
// 单航迹结构体
typedef struct {
    bool isvalid;
    uint8_t trkID;
    uint8_t motion_state;
    float maxLen_tcs;
    float maxWid_tcs;
    float x_output;
    float y_output;
    float vx_output;
    float vy_output;
    float heading_output;
} MotorCycle_TrkObj;

typedef struct {
    uint8_t numTrks;
} TrkInfo_Header;

// 航迹信息
typedef struct {
    TrkInfo_Header header;
    MotorCycle_TrkObj trkObj[MAX_TRACKS];
} Mmw_output_message_TrkInfo;

// 单个检测结构体 点云
typedef struct {
    uint16_t relRDIdx;
    float vlc;
    float x_output;
    float y_output;
    uint8_t motion_state;
	uint16_t pwr;        // 2位小数, (uint16)round(pwr_float * 100)
	uint16_t snr;        // 2位小数, (uint16)round(snr_float * 100)
	bool isPeak;
	float rng;
	int16_t vlc_amb;     // 2位小数, (int16)round(vlc_amb_float * 100)
	uint8_t vlc_disAmb_conf;  // 0位小数, (uint8)round(vlc_disAmb_conf)
	uint8_t vlc_disAmb_fac;   // 0位小数, (uint8)round(vlc_disAmb_fac)
	float azm_deg;
	int16_t x_rcs;       // 2位小数, (int16)round(x_rcs * 100)
	int16_t y_rcs;       // 2位小数, (int16)round(y_rcs * 100)
} MotorCycle_DetObj;

typedef struct {
		int relRDIdx;            // 第二角度（双角度估计）
        float vlc;
        float x_output;
        float y_output;
        int motion_state;
        float pwr;
        float snr;
        bool isPeak;
        float rng;
        float vlc_amb;
        int vlc_disAmb_conf;
        int vlc_disAmb_fac;
        float azm_deg;
        float x_rcs;
        float y_rcs;
} MotorCycle_DetObj_Read;

// 单帧检测点结构体
typedef struct {
		int numDets;						// 检测点数量
		int numStaticDets;					// 静态检测点数量
		MotorCycle_DetObj_Read detObj[MAX_DETECTIONS];		// 检测点数组
} DetInfo_Read;

typedef struct {
    uint16_t numDets;
} DetInfo_Header;

// 单帧检测点结构体
typedef struct {
    DetInfo_Header header;
    MotorCycle_DetObj detObj[MAX_DETECTIONS];
} Mmw_output_message_DetInfo;
#endif


/* ------------------------------------------------------------------ */
/*  EgoVlcResult — ego 速度估计输出                                     */
/* ------------------------------------------------------------------ */
typedef struct
{
    int16_t egoVelocity_mps; // 2位小数, (int16)round(egoVelocity_mps_float * 100)
    // float egoVelocity_mps; //读取文件测试时打开
} Mmw_output_message_EgoVlcInfo;

/* ------------------------------------------------------------------ */
/*  WarnResult — 预警输出                                               */
/*  对应 MATLAB warningDetection 返回的 warnResult struct               */
/* ------------------------------------------------------------------ */
typedef struct
{
    WarnResult warnResult;
} Mmw_output_message_WarnInfo;

typedef struct Mmw_pkt_info_t
{
    Mmw_output_message_DetInfo detInfo;
    Mmw_output_message_TrkInfo trkInfo;
    Mmw_output_message_EgoVlcInfo egoVlcInfo;
    Mmw_output_message_WarnInfo warnInfo;
} Mmw_pkt_info;

/* ****************************************************************
 * *******************两轮车雷达目标输出格式***********************
 * ****************************************************************/

// 雷达目标结构体
typedef struct {
    uint8_t Obj_Valid : 8;
    uint16_t Obj_LatPos : 13;
    uint16_t Obj_LongPos : 13;
    uint16_t Obj_Velo : 13;
    uint8_t Obj_PeakVal : 8;
} RadarTargetMsg;

/* *********************************************************************************************************
 * ***************************************两轮车雷达状态输出格式**********************************************
 * *********************************************************************************************************/

// BSD盲区功能开关
#define BSD_FUNC_DISABLE              0x0
#define BSD_FUNC_ENABLE               0x1

// BSD系统状态
#define BSD_STATUS_OFF                0x00
#define BSD_STATUS_SELFTEST           0x01
#define BSD_STATUS_READY              0x02
#define BSD_STATUS_ACTIVATED          0x03
#define BSD_STATUS_SYS_ERROR          0x04
#define BSD_STATUS_SELFTEST_ERROR     0x05

// 报警状态通用定义
#define ALARM_NONE                    0x00
#define ALARM_STANDARD                0x01
#define ALARM_ENHANCED                0x02
#define ALARM_RESERVED                0x03

// FCW功能开关
#define FCW_FUNC_DISABLE              0x0
#define FCW_FUNC_ENABLE               0x1

// FCW系统状态
#define FCW_STATUS_OFF                0x00
#define FCW_STATUS_SELFTEST           0x01
#define FCW_STATUS_READY              0x02
#define FCW_STATUS_ACTIVATED          0x03
#define FCW_STATUS_SYS_ERROR          0x04
#define FCW_STATUS_SELFTEST_ERROR     0x05

// SWA蜂鸣器/LED开关
#define SWA_FUNC_DISABLE              0x0
#define SWA_FUNC_ENABLE               0x1

// SWA预警目标距离最大值
#define SWA_DIST_MAX                  0x46

// SWA雷达故障状态
#define SWA_ERROR_NONE                0x00
#define SWA_ERROR_CURRENT             0x01
#define SWA_ERROR_HISTORY             0x02
#define SWA_ERROR_RESERVED            0x03

/* *********************************************************************************************************
 *    SIGNAL              |    Description                |       Unit                                     *
 * *********************************************************************************************************
 * BSD_LCMA_SW_Resp       | BSD盲区功能开关                | 0x0: disable, 0x1: enable
 * SYSTEM_STATUS_BSD      | BSD系统状态                    | 0x00(BSD off):功能关闭, 0x01(Self-test):自检, 0x02(Ready):功能待机, 0x03(Activated):功能激活, 0x04(System error):系统故障, 0x05(Self-test error):自检故障, 0x06、0x07(Reserved):保留, 
 * BSD_STATUS_L           | 左侧盲点监测功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * BSD_STATUS_R           | 右侧盲点监测功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * LCW_STATUS_L           | 左侧变道辅助功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * LCW_STATUS_R           | 右侧变道辅助功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * DOW_STATUS_L           | 左侧起步预警功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * DOW_STATUS_R           | 右侧起步预警功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * RCW_STATUS             | 后向碰撞预警功能报警状态        | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03(Reserved):保留, 
 * FCW_swt_state          | FCW功能开关                    | 0x0: disable, 0x1: enable
 * SYSTEM_STATUS_FCW      | FCW系统状态                    | 0x00(FCW off):功能关闭, 0x01(Self-test):自检, 0x02(Ready):功能待机, 0x03(Activated):功能激活, 0x04(System error):系统故障, 0x05(Self-test error):自检故障
 * FCW_STATUS             | 前向碰撞预警功能报警状态         | 0x00(No alarm):无报警, 0x01(Standard alarm):标准型报警, 0x02(Enhanced alarm):增强型报警, 0x03、0x06、0x07(Reserved):保留
 * SWA_Left_dis           | 左后侧预警目标距离              | 最大值0x46米
 * SWA_Right_dis          | 右后侧预警目标距离              | 最大值0x46米
 * SWA_Func_voice         | 蜂鸣器开关(输出)                | 0x0: disable, 0x1: enable
 * SWA_Func_LED_L         | 后视镜LED开关_左(输出)          | 0x0: disable, 0x1: enable
 * SWA_Func_LED_R         | 后视镜LED开关_右(输出)          | 0x0: disable, 0x1: enable
 * SWA_ERROR_state_R      | 雷达故障状态_后                 | 0x00: 无故障, 0x01: 当前故障, 0x02: 历史故障, 0x03: 保留
 * SWA_ERROR_state_F      | 雷达故障状态_前                 | 0x00: 无故障, 0x01: 当前故障, 0x02: 历史故障, 0x03: 保留
 * ***********************************************************************************************************/
typedef struct {
    uint8_t BSD_LCMA_SW_Rsp : 1;
    uint8_t SYSTEM_STATUS_BSD : 3;
    uint8_t BSD_STATUS_L : 2;
    uint8_t BSD_STATUS_R : 2;
    uint8_t LCW_STATUS_L : 2;
    uint8_t LCW_STATUS_R : 2;
    uint8_t DOW_STATUS_L : 2;
    uint8_t DOW_STATUS_R : 2;
    uint8_t RCW_STATUS : 2;
    uint8_t FCW_swt_state : 1;
    uint8_t SYSTEM_STATUS_FCW : 3;
    uint8_t FCW_STATUS : 3;
    uint8_t SWA_Left_dis : 8;
    uint8_t SWA_Right_dis : 8;
    uint8_t SWA_voice : 1;
    uint8_t SWA_Func_LED_L : 1;
    uint8_t SWA_Func_LED_R : 1;
    uint8_t SWA_ERROR_state_R : 2;
    uint8_t SWA_ERROR_state_F : 2;
} RadarStatusMsg;

// 將GlbCtx中的算法结果复制mmw_pkt_info结构体
Mmw_pkt_info* update_mmw_pkt_info_from_ctx(const GlbCtx *ctx, int pool_index);

/**
 * @brief 將算法结果(Mmw_pkt_info)按TLV协议封包到缓冲区
 * @param pktInfo       算法结果数据指针
 * @param frameID       帧ID
 * @param extraTLVData  额外TLV数据(如原始ADC数据)，可为NULL
 * @param extraTLVLen   额外TLV数据长度，为0则不添加
 * @param extraTLVType  额外TLV类型(如 MMW_OUTPUT_MSG_ADC_FRAME)
 * @param outBuf        输出缓冲区，由调用者预分配
 * @param bufSize       输出缓冲区大小
 * @return >0 封包总字节数；-1 pktInfo为空；
 */

int32_t mmw_packetize_results(Mmw_pkt_info *pktInfo, uint32_t frameID, char *outBuf,
							  const void *extraTLVData, uint32_t extraTLVLen, uint32_t extraTLVType);
int32_t adc_packetize_results(char *adcInput, uint32_t adcSize, uint32_t frameID, char *outBuf, uint32_t outBufSize,
							  const void *extraTLVData, uint32_t extraTLVLen, uint32_t extraTLVType);
/* 结束1字节对齐设置 */
// #pragma pack(pop)

#endif // MMW_MSG_PKT_H