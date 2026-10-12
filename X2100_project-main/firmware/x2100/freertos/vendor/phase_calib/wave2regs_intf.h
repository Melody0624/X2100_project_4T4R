#ifndef WAVE2REGS_INTF_H
#define WAVE2REGS_INTF_H

#include <stdint.h>
#include <stdio.h>

#include "cheetah.h"

#ifdef __cplusplus
extern "C"{
#endif

#define TRUE  1
#define FALSE 0

// Log level
#define LOG_DEBUG                       0
#define LOG_INFO                        1
#define LOG_ERROR                       2

#define TX_CH_NUM_CHEETAH               4                   // The number of transmitting channels of the chip.
#define MAX_SF_NUM_CHEETAH              4                   // Maximum number of subframes for Cheetah
#define MAX_CHIP_NUMBER_SIM             2                   // Maximum number of chips allowed for cascading.
#define MAX_TX_CH_NUM_CHEETAH           (TX_CH_NUM_CHEETAH * MAX_CHIP_NUMBER_SIM)
#define MAX_DDMA_CHIRP_NUM_CHEETAH      16                  // Maximum number of chirp loop in DDMA for Cheetah
#define MAX_BPM_CHIRP_NUM_CHEETAH       512                  // Maximum number of chirp loop in BPM for Cheetah

#define ASSIGN_2DARRAY(dest, type, dim1, dim2, ...) memcpy(&(dest), &(type[dim1][dim2]) __VA_ARGS__, sizeof(dest))

/*************************************************************************
 * simple configuration
 *************************************************************************/
typedef enum {
    TDM  = 0x01,
    DDMA = 0x02,
    BPM  = 0x04
} Modulation;

typedef enum {
    SR_106_67MSPS = 1,  //106.67MSPS
    SR_53_33MSPS = 2,   //53.33MSPS
    SR_26_67MSPS = 4,   //26.67MSPS
    SR_13_33MSPS = 8,   //13.33MSPS
} SamRate;

typedef enum {
    RAW8,
    RAW12,
    RAW16
} CSIForm;

typedef struct {
    uint32_t tx_ena;        // 包含chip_num*4个tx，每一个tx用一个bit表示，master芯片的tx编号为0，1，2，3（以芯片内部编码为依据），依次为4，5，6，7，...
    double fstart;          // GHz
} CW_config_t;

typedef struct {
    uint32_t tx_ena;        // 包含chip_num*4个tx，每一个tx用一个bit表示，master芯片的tx编号为0，1，2，3（以芯片内部编码为依据），依次为4，5，6，7，...
    uint32_t rx_ena;        // 包含chip_num*4个rx，每一个rx用一个bit表示，master芯片的rx编号为0，1，2，3（以芯片内部编码为依据），依次为4，5，6，7，...
    uint32_t chirp_num;     // tdm: tx均分； ddm:
    double slop;            // MHz/us
    double fstart;          // GHz
    double fstep;           // KHz
    uint32_t inter_chirp;   // us
    uint32_t adc_start;     // us
    uint32_t ramp_end;      // us
    uint32_t inter_subframe;// us
    uint32_t sample_num;
    uint32_t ddm_band;
    uint32_t ddm_chirp;                  //
    uint32_t ddm_num;
    uint32_t bpm_len;
    uint32_t ddm_ram[MAX_TX_CH_NUM_CHEETAH][MAX_DDMA_CHIRP_NUM_CHEETAH]; // ddm profile index
    uint8_t tdm_ram[MAX_TX_CH_NUM_CHEETAH];
    uint32_t bpm_ram[MAX_TX_CH_NUM_CHEETAH][MAX_BPM_CHIRP_NUM_CHEETAH]; // ddm profile index
} subframe_t;

typedef struct
{
    int cascade;							//是否是级联模式
    int numMMIC;							//IC数量
    int modulation;							//发送模式
    int num_subframe;						//子帧数量
    int num_adc[MAX_SF_NUM_CHEETAH];		//采样数量
    int num_chirp[MAX_SF_NUM_CHEETAH];		//chirp数
    int ddma_num[MAX_SF_NUM_CHEETAH];		//ddma_num
    int ddma_chirp[MAX_SF_NUM_CHEETAH];		//ddma_chirp
	int num_bands[MAX_SF_NUM_CHEETAH]; 		//待校准的相位数， 单芯片默认值为8，级联芯片默认值为12，  用户可配置
	int num_signal_bands;				//CHECK中实际占用的DDMA子带数，当前由有效TX数推导
    int num_range_bins;						//？
    int MAX_NUM_BANDS;						//最大待校准的相位数
    int nominal_phase_table[8][16];			//默认的TX相位表，如果需要，可以由用户手动修改，如果不修改，就使用下面默认的TX相位表
    int NUM_PHASE_PER_TX[8];				//从TX相位表中计算得出每个TX使用的独立相位的个数
    int N_TX;								//物理TX chirp槽位数
    uint32_t calib_tx_mask;                  //PCB上实际参与移相校准的物理TX，bit0对应TX1
    int num_calib_tx;                       //由calib_tx_mask推导的有效TX数
    int N_RX;								//最大RX通道数量
    int N_TX_PER_MMIC;
    int MAX_ITERATION;						//最大迭代次数
    double good_empty_dbc;              //校准效果较好标准，达到后可提前结束迭代
    int DEBUG;								//调试DEBUG log
    int filterNumRows;                      //过滤距离

    double slope[MAX_SF_NUM_CHEETAH];		//Chirp斜率
    double Fs;   							//ADC 采样率
    double max_mag_diff_db;					//不同相位条件下1dFFT峰值幅度的最大差值
    double max_empty_dbc;					//校准收敛的最低标准，实测值小于等于该值时可接受
    double PHASE_ABS_TOL_DEG;				//相位校准的tolerance
    double empty_band_power_dbc[MAX_SF_NUM_CHEETAH];			//当前空子带能量

    double modphase_tx[8][64];
    double mag_tx_avg[8][64];
    double phase_12[8][64];

    uint32_t PhaseShifterReg[4][32];
    uint32_t DDMAProfileindexReg[4][24];
}CalibCfg_t;

typedef struct {
    // system
    uint8_t cw_mode;       //TRUE, FALSE
    uint8_t chip_num;       // 1, 2
    uint8_t modulation;  // TDM, DDMA,DPM
    CSIForm csi_form;       // RAW8, RAW12, RAW16
    SamRate sample_rate;    // SR_106_67MSPS, SR_53_33MSPS, SR_26_67MSPS, SR_13_33MSPS

    // cw configer
    CW_config_t cw_cfg;

    // frame
    uint32_t inter_frame;   // ms
    uint8_t subframe_num;   // 1, 2, 3, 4

    // subframe
    subframe_t subframe[MAX_SF_NUM_CHEETAH];

    // phase data
    double phase_shift_code[64];
    double phase_calib[64];
} config_cheetah_t;

typedef struct
{
    struct reg_line* regs;
    size_t count;
} ParamsRegsConfig;

typedef enum Cheetah_PhaseShifter_Calibration_Flow_e
{
    FIRST_CALIBRATION = 1,
    SECOND_CALIBRATION,
    CHECK_CONVERGENCE
} Cheetah_PhaseShifter_Calibration_Flow;

typedef struct
{
    int failed;
    int failure_reason;
    int failure_tx;
    double rms;
    double avg_err;
    double max_abs_err;
    double failure_value;
    double failure_limit;
} ErrStart_t;

#define CALIB_PROCESS_FAILURE_NONE       0
#define CALIB_PROCESS_FAILURE_MAG_SPREAD 1

extern const ParamsRegsConfig cheetah_master_default_config;
extern const ParamsRegsConfig cheetah_slave1_default_config;
extern const ParamsRegsConfig cheetah_slave2_default_config;
extern const ParamsRegsConfig cheetah_slave3_default_config;

int convertWaveToRegs(config_cheetah_t *cheetahSimWaveCfg, ParamsRegsConfig *cheetahCfg);
void copyResetCheetahCfg(CalibCfg_t *cfg, ParamsRegsConfig *outCfg);
int calibProcessing(Cheetah_PhaseShifter_Calibration_Flow calibFlow, CalibCfg_t *calib_cfg, ParamsRegsConfig *regsCfg);
ErrStart_t dataProcessing(Cheetah_PhaseShifter_Calibration_Flow *calibFlow, CalibCfg_t *cfg, int16_t *adc_data, int *IQ_idx_table, int sfIdx);

#ifdef USE_RADAR_STUDIO_UI
void c_logi(const char *fmt, ...);
void c_logd(const char *fmt, ...);
void c_loge(const char *fmt, ...);
void updateParamsRegsField(uint32_t chipIdx, uint32_t addr, const uint32_t inVal, uint32_t startBit, uint32_t endBit);
#define LOG(level, format, ...) \
    do { \
        switch (level) { \
        case LOG_DEBUG: \
            c_logd(format, ##__VA_ARGS__); \
        break; \
        case LOG_INFO: \
            c_logi(format, ##__VA_ARGS__); \
            break; \
        case LOG_ERROR: \
            c_loge(format, ##__VA_ARGS__); \
            break; \
        } \
    } while (0)
#else
#define LOG(level, format, ...) \
    do { \
        (void)(level); \
        printf(format, ##__VA_ARGS__); \
        printf("\n"); \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif // C_SIMPLE_CONFIGURATION_H
