/**
 * @file        InitPhaseCalib.h
 * @brief       Initialization phase calibration module header file
 * @author      yufeng
 * @company     SenardMicro.Co., Ltd.
 * @date        2026-08-07
 * @version     1.0.0
 * @copyright   Copyright (c) 2026 SenardMicro.Co., Ltd. All rights reserved.
 *
 * @history
 * | Version | Date       | Author | Description            |
 * |---------|------------|--------|------------------------|
 * | V1.0.0  | 2026-08-07 | yufeng | Initial version        |
 */

#ifndef INIT_PHASE_CALIB_H
#define INIT_PHASE_CALIB_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#include <stdint.h>
#include "radar_types.h"
#include "board_calib_config.h"


#define MIN_RANGE 0.0f
#define MAX_RANGE 25.0f
#define MIN_ANG   -90.0f
#define MAX_ANG   90.0f

#define INIT_PHASE_DEBUG 0

typedef enum
{
    INIT_PHASE_COMM_UART = 0,
    INIT_PHASE_COMM_GADGET_SERIAL = 1,
    INIT_PHASE_COMM_CAN = 2,
} InitPhaseCommChannel_t;

typedef enum
{
    INIT_PHASE_STEP_IDLE = 0,
    INIT_PHASE_STEP_SET_PARA = 1,
    INIT_PHASE_STEP_START = 2,
    INIT_PHASE_STEP_ZERO_CALIB = 3,
    INIT_PHASE_STEP_ZERO_CALIB_DONE = 4,
    INIT_PHASE_STEP_FFT_CALIB_START = 5,
    INIT_PHASE_STEP_FFT_CALIB_DONE = 6,
    INIT_PHASE_STEP_STORE = 7,
    INIT_PHASE_STEP_DONE = 8,
} InitPhaseStep_n;

/**************************************************************/
/* Initialization phase calibration module data */
/**************************************************************/
typedef struct
{
    int8_t Range;
    int8_t minAng;
    int8_t maxAng;
    int8_t AngStep;
    InitPhaseCommChannel_t commChannel; /* Communication channel for feedback */
} InitPhasePara_t;

/**************************************************************/
/* Initialization phase calibration module function */
/**************************************************************/
int InitPhaseCalibProcess(void);
void SetInitPhaseStepFlg(InitPhaseStep_n flg);
void SetTargetPara(InitPhasePara_t *para);
void SetTargetAngle(int32_t ang);
int32_t GetTargetAngle(void);
InitPhaseStep_n GetInitPhaseStepFlg(void);
int InitPhaseRDMap(GlbCtx* ctx, const float* RDMap, int numRngBins, int numDoppBinsPerSubBand, unsigned int* maxMetricIndices, int pool_index);
void InitPhaseCalibInit(GlbCtx *ctx);

/* Unified interface for initial phase calibration */
int InitPhaseCalibInterface(InitPhaseCommChannel_t channel, int argc, char *argv[]);
/* Set communication channel for calibration feedback (used by CAN/UDS path) */
void InitPhaseCalibSetCommChannel(InitPhaseCommChannel_t channel);
/*Zero calculation */
void InitPhaseAngCalibCal(
    GlbCtx* glbCtx,
    const fft_cpx_f32* angCalib,   // calibration vector
    const float* winAzimuth,      // FFT window
    const fft_cpx_f32* dopplerFFT_res,  // 2dFFT result
    unsigned int* maxMetricIndices,
    int numRngBins,
    int numDoppBinsPerSubBand,
    int numVirtualAnts,
    int pool_index);

/*FFT calculation */
void InitPhaseAngleSample(
    GlbCtx* glbCtx,
    const fft_cpx_f32* angCalib,   // calibration vector
    const float* winAzimuth,      // FFT window
    const fft_cpx_f32* dopplerFFT_res,  // 2dFFT result
    unsigned int* maxMetricIndices,
    int numRngBins,
    int numDoppBinsPerSubBand,
    int numVirtualAnts,
    int pool_index);
#endif /* INIT_PHASE_CALIB_H */