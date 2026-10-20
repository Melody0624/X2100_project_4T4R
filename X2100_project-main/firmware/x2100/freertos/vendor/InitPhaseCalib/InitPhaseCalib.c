/**
 * @file        InitPhaseCalib.c
 * @brief       Initialization phase calibration module source file
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

#include "InitPhaseCalib.h"
#include "radar_types.h"
#include "memory_pool.h"
#include "zero_phase_correction.h"
#include "mapping_gen_processing.h"
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include "config_manager.h"
#include "complex.h"
#include "complex_abs_f32.h"
#include "general_functions.h"
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "uart_trans.h"
#include "gadget_serial.h"

static int peak_rngIdx = 0;
static uint8_t initPhaseFlg = 0;
static uint8_t minRngIdx = 0;
static uint8_t maxRngIdx = 0;
static int targetAngle = 666;
static int l_minAng = 0;
static int l_maxAng = 0;
static int l_AngStep = 0;
static float l_Range = 0.0f;
static float l_rngRes = 0.0f;
static float l_AngleGt[200] = {0.0f};
static float l_AngleMean[200] = {0.0f};
static int l_AngleIndx = 0u;
static int l_AngleSampleCount = 0;
static float l_spacing_wavelengths = 0.5f;
static int l_AngleIndxMax = 0u;
static int l_mappingGenFlg = 0u;
static fft_cpx_f32 l_bv[64];
static float l_pending_angle_map[128];

InitPhaseStep_n initPhaseStep_curr = INIT_PHASE_STEP_IDLE;
InitPhaseStep_n initPhaseStep_last = INIT_PHASE_STEP_IDLE;
static InitPhaseCommChannel_t initPhaseCommChannel = INIT_PHASE_COMM_UART;

typedef int (*SendFunc_t)(char *buf, int size);

static void sendFeedback(InitPhaseCommChannel_t channel, const char *msg, int len)
{
    switch (channel)
    {
        case INIT_PHASE_COMM_UART:
            uart_general_send((char *)msg, len);
            break;
        case INIT_PHASE_COMM_GADGET_SERIAL:
            usb_serial_gerenal_send((char *)msg, len);
            break;
        case INIT_PHASE_COMM_CAN:
            /* TODO: Implement CAN feedback */
            break;
        default:
            break;
    }
}

static void sendFeedbackFormatted(InitPhaseCommChannel_t channel, const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    if (len > 0 && len < (int)sizeof(buf))
    {
        sendFeedback(channel, buf, len);
    }
}

/*
* Function Name: InitPhaseCalibSetCommChannel
* Param Input: channel - communication channel (UART, GADGET_SERIAL, or CAN)
* Param Return: void
* Function Description: Set the communication channel for calibration feedback.
*                       Safe to call from external modules (e.g. UDS CAN handler).
*                       Only updates when calibration is in IDLE state to prevent
*                       overwriting during an active calibration session.
*/
void InitPhaseCalibSetCommChannel(InitPhaseCommChannel_t channel)
{
    if (initPhaseStep_curr == INIT_PHASE_STEP_IDLE)
    {
        initPhaseCommChannel = channel;
    }
}

/*
* Function Name: InitPhaseCalibInit
* Param Input: ctx - pointer to global context structure
* Param Return: void
* Function Description: Initialize the phase calibration module by caching the range resolution
*                       from waveform parameters for later use in range index calculation.
*/
void InitPhaseCalibInit(GlbCtx *ctx)
{
    l_rngRes = LIGHT_SPEED * ctx->waveParams.sampling_freq[0] /
        (2.0f * ctx->waveParams.slope[0] * nextPowerOfTwo(ctx->waveParams.adcLen[0]));
    l_spacing_wavelengths = ctx->basicParams.minElemSpacing[0];
}

/*
* Function Name: InitPhaseCalibProcess
* Param Input: void
* Param Return: 0 on success
* Function Description: Main state machine for the initialization phase calibration workflow.
*                       Manages the full calibration pipeline: parameter setup, zero calibration,
*                       FFT angle calibration sample collection, mapping generation, and Flash storage.
*/
int InitPhaseCalibProcess(void)
{

    printf("initPhaseStep_curr: %d\n", initPhaseStep_curr);
    switch(initPhaseStep_curr)
    {
        case INIT_PHASE_STEP_IDLE:
        {
            initPhaseFlg = 0;
            targetAngle = 666;
            l_AngleIndx = 0u;
            l_AngleSampleCount = 0;
            l_AngStep = 0;
            l_mappingGenFlg = 0u;
            peak_rngIdx = 0;
            minRngIdx = 0;
            maxRngIdx = 0;
            l_minAng = 0;
            l_maxAng = 0;
            l_Range = 0.0f;
            l_AngleIndxMax = 0u;
            memset(l_AngleGt, 0, sizeof(l_AngleGt));
            memset(l_AngleMean, 0, sizeof(l_AngleMean));
            initPhaseCommChannel = INIT_PHASE_COMM_UART;
            break;
        }
        case INIT_PHASE_STEP_SET_PARA:
        {
            l_AngleIndxMax = (((l_maxAng - l_minAng)/l_AngStep) + 1);
            printf("l_AngleIndxMax: %d\n", l_AngleIndxMax);
            break;
        }
        case INIT_PHASE_STEP_START:
        {
            if(initPhaseStep_last == INIT_PHASE_STEP_SET_PARA)
            {
                initPhaseFlg = 1;
            }
            else
            {
                initPhaseStep_last = INIT_PHASE_STEP_IDLE;
            }

            initPhaseStep_curr = INIT_PHASE_STEP_ZERO_CALIB;
            break;
        }
        case INIT_PHASE_STEP_ZERO_CALIB:
        {
            break;
        }
        case INIT_PHASE_STEP_ZERO_CALIB_DONE:
        {
            if(initPhaseStep_last != INIT_PHASE_STEP_ZERO_CALIB_DONE)
            {
                fft_cpx_f32 candidate[NUM_VX_SF1];
                int valid = 1;
                for (int i = 0; i < NUM_VX_SF1; ++i) {
                    float power = l_bv[i].r*l_bv[i].r + l_bv[i].i*l_bv[i].i;
                    if (!isfinite(power) || power < 1e-12f) valid = 0;
                }
                if (!valid) {
                    sendFeedbackFormatted(initPhaseCommChannel, "Zero Calib Sample Failed: invalid channel power\n");
                    initPhaseStep_curr = INIT_PHASE_STEP_IDLE;
                    break;
                }
                compute_zero_phase_correction(l_bv,
                                   NUM_VX_SF1,
                                   candidate);
#if(INIT_PHASE_DEBUG)
                // for(uint8_t i = 0; i < 8; i++)
                // {
                //     char buf[64];
                //     int len = snprintf(buf, sizeof(buf), "angCalibMat[%d]: %f, %f\n", i, angCalibMat[i].i, angCalibMat[i].r);
                //     if (len > 0 && len < (int)sizeof(buf))
                //     {
                //         usb_serial_send(buf, len);
                //     }
                // }
#endif
                if (param_set(PARAM_ANG_CALIB_MAT, candidate, sizeof(candidate)) == 0) {
                    memcpy(angCalibMat, candidate, sizeof(candidate));
                    memcpy(angCalibMat_back, candidate, sizeof(candidate));
                    sendFeedback(initPhaseCommChannel, "Zero Calib Sample OK\n", strlen("Zero Calib Sample OK\n"));
                } else {
                    sendFeedback(initPhaseCommChannel, "Zero Calib Sample Failed\n", strlen("Zero Calib Sample Failed\n"));
                    initPhaseStep_curr = INIT_PHASE_STEP_IDLE;
                }
            }
            break;
        }
        case INIT_PHASE_STEP_FFT_CALIB_START:
        {
            if(l_AngleSampleCount >= l_AngleIndxMax)
            {
                initPhaseStep_curr = INIT_PHASE_STEP_FFT_CALIB_DONE;
                sendFeedback(initPhaseCommChannel, "FFT Calib Sample Done\n", strlen("FFT Calib Sample Done\n"));
            }
            else
            {
                /*Do nothing */
            }
            break;
        }
        case INIT_PHASE_STEP_FFT_CALIB_DONE:
        {
            if(l_mappingGenFlg == 0u)
            {
                int ret = mappingGen_processing_with_spacing(l_AngleGt, l_AngleIndxMax, l_AngleMean, l_AngleIndxMax, l_pending_angle_map, 128, l_spacing_wavelengths);
                if (ret != 0) {
                    sendFeedbackFormatted(initPhaseCommChannel, "[InitPhaseCalib] ERROR: mappingGen_processing failed (ret=%d).\n", ret);
                    sendFeedbackFormatted(initPhaseCommChannel, "[InitPhaseCalib] l_AngleIndxMax=%d, dumping l_AngleMean:\n", l_AngleIndxMax);
#if(INIT_PHASE_DEBUG)
                    // for (int i = 0; i < l_AngleIndxMax; i++) {
                    //     len = snprintf(buf, sizeof(buf), "  l_AngleGt[%d]=%f, l_AngleMean[%d]=%f\n", i, l_AngleGt[i], i, l_AngleMean[i]);
                    //     if (len > 0 && len < (int)sizeof(buf)) { usb_serial_send(buf, len); }
                    // }
#endif
                    sendFeedback(initPhaseCommChannel, "FFT Calib Sample Failed: monotonic data too short\n",
                                    strlen("FFT Calib Sample Failed: monotonic data too short\n"));
                    initPhaseStep_curr = INIT_PHASE_STEP_IDLE;
                } else {
                    l_mappingGenFlg = 1u;
#if(INIT_PHASE_DEBUG)
                    // for (int i = 0; i < l_AngleIndxMax; i++) {
                    //     char buf[64];
                    //     int len = snprintf(buf, sizeof(buf), "  l_AngleGt[%d]=%f, l_AngleMean[%d]=%f\n", i, l_AngleGt[i], i, l_AngleMean[i]);
                    //     if (len > 0 && len < (int)sizeof(buf))
                    //     {
                    //         usb_serial_send(buf, len);
                    //     }
                    // }
                    // for(uint8_t i = 0; i < 128; i++)
                    // {
                    //     char buf[64];
                    //     int len = snprintf(buf, sizeof(buf), "angFFT_interp[%d]: %f\n", i, angFFT_interp[i]);
                    //     if (len > 0 && len < (int)sizeof(buf))
                    //     {
                    //         usb_serial_send(buf, len);
                    //     }
                    // }
#endif
                    initPhaseStep_curr = INIT_PHASE_STEP_STORE;
                }
            }
            break;
        }
        case INIT_PHASE_STEP_STORE:
        {
            /* Swap halves to match FFT bin order:
             *   Current: [0..63] = neg angles (-90°→0°), [64..127] = pos angles (0°→+90°)
             *   Target:  [0..63] = pos angles (0°→+90°), [64..127] = neg angles (-90°→0°)
             */
            for (int i = 0; i < 64; i++)
            {
                float tmp = l_pending_angle_map[i];
                l_pending_angle_map[i] = l_pending_angle_map[i + 64];
                l_pending_angle_map[i + 64] = tmp;
            }
#if(INIT_PHASE_DEBUG)
            // for(uint8_t i = 0; i < 128; i++)
            // {
            //     char buf[64];
            //     int len = snprintf(buf, sizeof(buf), "angFFT_interp[%d]: %f\n", i, angFFT_interp[i]);
            //     if (len > 0 && len < (int)sizeof(buf))
            //     {
            //         usb_serial_send(buf, len);
            //     }
            // }
#endif
            if (param_set(PARAM_ANG_FFT_CFG, l_pending_angle_map, sizeof(l_pending_angle_map)) == 0) {
                memcpy(angFFT_interp, l_pending_angle_map, sizeof(angFFT_interp));
                for (int p = 0; p < MAXPOOLNUM; ++p) {
                    if (g_memoryPool[p].initialized && g_memoryPool[p].angFFTvec)
                        memcpy(g_memoryPool[p].angFFTvec, angFFT_interp, sizeof(angFFT_interp));
                }
                sendFeedback(initPhaseCommChannel, "FFT Calib Sample OK\n", strlen("FFT Calib Sample OK\n"));
                initPhaseStep_curr = INIT_PHASE_STEP_DONE;
            } else {
                sendFeedback(initPhaseCommChannel, "FFT Calib Sample Failed\n", strlen("FFT Calib Sample Failed\n"));
                initPhaseStep_curr = INIT_PHASE_STEP_IDLE;
            }
            
            break;
        }
        case INIT_PHASE_STEP_DONE:
        {
            initPhaseStep_curr = INIT_PHASE_STEP_IDLE;
            break;
        }
        default:
        {
            break;
        }
    }
    printf("initPhaseStep_curr: %d\n", initPhaseStep_curr);
    initPhaseStep_last = initPhaseStep_curr;
    return 0;
}

/*
* Function Name: GetInitPhaseStepFlg
* Param Input: void
* Param Return: Current initialization phase step (InitPhaseStep_n)
* Function Description: Getter for the current calibration state machine step.
*/
InitPhaseStep_n GetInitPhaseStepFlg(void)
{
    return initPhaseStep_curr;
}
/*
* Function Name: SetInitPhaseStepFlg
* Param Input: flg - target state to set (only IDLE, START, or FFT_CALIB_START allowed)
* Param Return: void
* Function Description: Setter for the calibration state machine step. Only accepts transitions
*                       to IDLE, START, or FFT_CALIB_START; other values are silently ignored.
*/
void SetInitPhaseStepFlg(InitPhaseStep_n flg)
{
    if((flg == INIT_PHASE_STEP_IDLE) || (flg == INIT_PHASE_STEP_START) || (flg == INIT_PHASE_STEP_FFT_CALIB_START))
    {
        if(flg == INIT_PHASE_STEP_IDLE)
        {
            initPhaseFlg = 0;
            targetAngle = 666;
            l_AngleIndx = 0u;
            l_AngleSampleCount = 0;
            l_AngStep = 0;
            l_mappingGenFlg = 0u;
            peak_rngIdx = 0;
            minRngIdx = 0;
            maxRngIdx = 0;
            l_minAng = 0;
            l_maxAng = 0;
            l_Range = 0.0f;
            l_AngleIndxMax = 0u;
            memset(l_AngleGt, 0, sizeof(l_AngleGt));
            memset(l_AngleMean, 0, sizeof(l_AngleMean));
            initPhaseCommChannel = INIT_PHASE_COMM_UART;
            printf("[InitPhase] Force abort: all state flags cleared, channel reset to UART\n");
        }
        initPhaseStep_curr = flg;
#if(INIT_PHASE_DEBUG)
        printf("initPhaseStep_curr: %d\n", initPhaseStep_curr);
#endif  
    }
    else
    {
        /*Do nothing */
    }
    
}

/*
* Function Name: SetTargetPara
* Param Input: para - pointer to calibration parameters (Range, minAng, maxAng, AngStep)
* Param Return: void
* Function Description: Configure the calibration target parameters. Computes the range index
*                       window (minRngIdx, maxRngIdx) based on range resolution, calculates
*                       the expected number of angle samples, and transitions to SET_PARA state.
*/
void SetTargetPara(InitPhasePara_t *para)
{
    if (!para || para->Range <= 0 || para->AngStep <= 0 ||
        para->minAng > 0 || para->maxAng < 0 || para->minAng >= para->maxAng ||
        (para->maxAng - para->minAng) % para->AngStep != 0 ||
        (-para->minAng) % para->AngStep != 0 ||
        (para->maxAng - para->minAng) / para->AngStep + 1 > 200) {
        sendFeedbackFormatted(initPhaseCommChannel, "Error: invalid calibration range/angle grid\n");
        return;
    }
    l_AngleIndx = 0;
    l_AngleSampleCount = 0;
    uint8_t tmp_Idx = 0u;
    l_Range = (float)para->Range / 10.0f;
    l_minAng = (int)para->minAng;
    l_maxAng = (int)para->maxAng;
    l_AngStep = (int)para->AngStep;
    tmp_Idx = (uint8_t)(l_Range / l_rngRes);
    if(tmp_Idx < 5)
    {
        minRngIdx = 0;
    }
    else
    {
        minRngIdx = tmp_Idx - 5;
    }

    maxRngIdx = (uint8_t)(tmp_Idx + 5);
    initPhaseStep_curr = INIT_PHASE_STEP_SET_PARA;

    sendFeedbackFormatted(initPhaseCommChannel, "l_Range: %f, l_minAng: %d, l_maxAng: %d, targetAngle: %d, minRngIdx: %d, maxRngIdx: %d\n", l_Range, l_minAng, l_maxAng, targetAngle, minRngIdx, maxRngIdx);
}

/*
* Function Name: SetTargetAngle
* Param Input: ang - target angle value in degrees (666 is treated as invalid and ignored)
* Param Return: void
* Function Description: Set the target angle for FFT calibration sampling. Invalid values (666)
*                       are silently rejected. Duplicate consecutive angles are deduplicated.
*                       Only accepts input when the state machine is in ZERO_CALIB_DONE or
*                       FFT_CALIB_START state.
*/
void SetTargetAngle(int32_t ang)
{
    if (ang == 666)
    {
        return;
    }

    if((initPhaseStep_curr == INIT_PHASE_STEP_ZERO_CALIB_DONE) || (initPhaseStep_curr == INIT_PHASE_STEP_FFT_CALIB_START))
    {
        if (l_AngleIndx > 0 && ang == (int32_t)l_AngleGt[l_AngleIndx - 1]) return;
        if (l_AngleIndx != l_AngleSampleCount || l_AngleIndx >= l_AngleIndxMax ||
            ang != l_minAng + l_AngleIndx * l_AngStep) {
            sendFeedbackFormatted(initPhaseCommChannel, "Error: wait for Angle Sample OK and send next grid angle\n");
            return;
        }
        targetAngle = ang;
        initPhaseStep_curr = INIT_PHASE_STEP_FFT_CALIB_START;

        if (l_AngleIndx == 0 || targetAngle != (int32_t)l_AngleGt[l_AngleIndx - 1])
        {
            l_AngleGt[l_AngleIndx] = targetAngle;
            l_AngleIndx++;
        }

        sendFeedbackFormatted(initPhaseCommChannel, "targetAngle: %d, l_AngleIndx: %d\n", targetAngle, l_AngleIndx);
    }
    else
    {
        /*Do nothing */
    }

}

/*
* Function Name: GetTargetAngle
* Param Input: void
* Param Return: Current target angle value in degrees (int32_t)
* Function Description: Getter for the current target angle used in FFT calibration sampling.
*/
int32_t GetTargetAngle(void)
{
    return (int32_t)targetAngle;
}

/*
* Function Name: InitPhaseRDMap
* Param Input: ctx - pointer to global context
*              RDMap - range-Doppler map magnitude array
*              numRngBins - number of range bins
*              numDoppBinsPerSubBand - number of Doppler bins per sub-band
*              maxMetricIndices - output array for max metric indices
*              pool_index - memory pool index
* Param Return: 0 on success, -1 if multiple peaks with equal power are found
* Function Description: Search for the peak range bin within the configured range window
*                       (minRngIdx to maxRngIdx). Stores the peak index and reports an error
*                       if multiple equal-power peaks are detected.
*/
int InitPhaseRDMap(GlbCtx* ctx, const float* RDMap, int numRngBins, int numDoppBinsPerSubBand, unsigned int* maxMetricIndices, int pool_index)
{
    int rngIdx = 0;
    float maxPower = -1.0f;
    int maxCount = 0;

    // {
    //     char buf[64];
    //     int len = snprintf(buf, sizeof(buf), "RDMap[%d]:", numRngBins * numDoppBinsPerSubBand);
    //     if (len > 0 && len < (int)sizeof(buf)) { usb_serial_send(buf, len); }
    //     for (int i = 0; i < numRngBins * numDoppBinsPerSubBand; i++) {
    //         len = snprintf(buf, sizeof(buf), "%.2f,", RDMap[i]);
    //         if (len > 0 && len < (int)sizeof(buf)) { usb_serial_send(buf, len); }
    //     }
    //     usb_serial_send((char *)"\n", 1);
    // }

	for (rngIdx = minRngIdx; rngIdx < maxRngIdx; rngIdx++) {
	    float power = RDMap[rngIdx];
	    if (power > maxPower) {
	        maxPower = power;
	        peak_rngIdx = rngIdx;
	        maxCount = 1;
	    } else if (power == maxPower) {
	        maxCount++;
	    }
	}

    if (maxCount > 1) {
        printf("Error: Multiple peak indices found in RDMap (count=%d)\n", maxCount);
        return -1;
    }
#if(INIT_PHASE_DEBUG)
    {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "peak_rngIdx: %d, maxPower: %f\n", peak_rngIdx, maxPower);
        if (len > 0 && len < (int)sizeof(buf))
        {
            usb_serial_send(buf, len);
        }
    }
#endif
    return 0;
}

/*
* Function Name: InitPhaseAngCalibCal
* Param Input: glbCtx - pointer to global context
*              angCalib - angle calibration vector for phase compensation
*              winAzimuth - azimuth FFT window coefficients
*              dopplerFFT_res - 2D FFT result (range-Doppler complex data)
*              maxMetricIndices - max metric indices from detection
*              numRngBins - number of range bins
*              numDoppBinsPerSubBand - number of Doppler bins per sub-band
*              numVirtualAnts - number of virtual antennas
*              pool_index - memory pool index
* Param Return: void
* Function Description: Compute the zero-phase calibration. Extracts the beamforming vector (BV)
*                       from the 2D FFT result at the peak range bin, applies calibration
*                       compensation, and transitions to ZERO_CALIB_DONE state.
*/

void InitPhaseAngCalibCal(
    GlbCtx* glbCtx,
    const fft_cpx_f32* angCalib,   // calibration vector
    const float* winAzimuth,      // FFT window
    const fft_cpx_f32* dopplerFFT_res,  // 2dFFT result
    unsigned int* maxMetricIndices,
    int numRngBins,
    int numDoppBinsPerSubBand,
    int numVirtualAnts,
    int pool_index)
{
	int rngIdx = peak_rngIdx;
    int dopIdx = 0;
    int numDets = glbCtx->detInfoRD.numDets;
    // float d_azm = glbCtx->basic_params.minElemSpacing;
    unsigned int numTxs = glbCtx->basicParams.numTxs[0];
    unsigned int numRxs = glbCtx->basicParams.numRxs[0];
    unsigned int numTotalSubBands = numTxs + glbCtx->waveParams.numEmptyBands[0];
    unsigned int NFFT_doppler = numDoppBinsPerSubBand * numTotalSubBands;
    /* ---------- 取 BV ---------- */
	fft_cpx_f32* bv = l_bv;
    unsigned int subBandIdx, dopBinIdx_ddmaDec_tx, idx, bv_idx, bv_logIdx;
#if(INIT_PHASE_DEBUG)
    // {
    //     char buf[64];
    //     int len;
    //     int totalSize = numRngBins * NFFT_doppler * numRxs;
    //     len = snprintf(buf, sizeof(buf), "numRngBins=%d, NFFT_doppler=%d, numRxs=%d, totalSize=%d\n", numRngBins, NFFT_doppler, numRxs, totalSize);
    //     if (len > 0 && len < (int)sizeof(buf)) { usb_serial_send(buf, len); }
    //     len = snprintf(buf, sizeof(buf), "dopplerFFT_res[%d]:\n", totalSize);
    //     if (len > 0 && len < (int)sizeof(buf)) { usb_serial_send(buf, len); }
    //     for (int i = 0; i < totalSize; i++) {
    //         len = snprintf(buf, sizeof(buf), "%.2f,%.2f,", dopplerFFT_res[i].r, dopplerFFT_res[i].i);
    //         if (len > 0 && len < (int)sizeof(buf)) { usb_serial_send(buf, len); }
    //         if ((i + 1) % 8 == 0) {
    //             vTaskDelay(pdMS_TO_TICKS(70));
    //         }
    //     }
    //     usb_serial_send((char *)"\n", 1);
        
    // }
#endif
    for (unsigned int rxIdx = 0; rxIdx < numRxs; rxIdx ++)
    {
        for (unsigned int txIdx = 0; txIdx < numTxs; txIdx++)
        {
            subBandIdx = maxMetricIndices[dopIdx * numRngBins + rngIdx] + txIdx;
            if (subBandIdx >= numTotalSubBands)
            {
		    	subBandIdx -= numTotalSubBands;
            }
            dopBinIdx_ddmaDec_tx = dopIdx + subBandIdx * numDoppBinsPerSubBand;
            idx = rxIdx * numRngBins * NFFT_doppler + dopBinIdx_ddmaDec_tx * numRngBins + rngIdx;
            bv_idx = txIdx * numRxs + rxIdx;
            bv_logIdx = glbCtx->radarInfo.logicIdx_reorder_sf1[bv_idx]; // 调整逻辑顺序
            // bv[bv_idx].r = (float)(dopplerFFT_res[idx].r * angCalib[bv_idx].r - dopplerFFT_res[idx].i * angCalib[bv_idx].i); // calibration real
            // bv[bv_idx].i = (float)(dopplerFFT_res[idx].r * angCalib[bv_idx].i + dopplerFFT_res[idx].i * angCalib[bv_idx].r); // calibration imag
            bv[bv_idx].r = (float)(dopplerFFT_res[idx].r);
            bv[bv_idx].i = (float)(dopplerFFT_res[idx].i);
#if(INIT_PHASE_DEBUG)
            printf("bv[%d]: %f + %fi\n", bv_idx, bv[bv_idx].r, bv[bv_idx].i);
#endif
        }
    }  
#if(INIT_PHASE_DEBUG)
    char buf[64];
    int len;
    len = snprintf(buf, sizeof(buf), "Calib mat FrameID=%d, \n", glbCtx->frmInfo.frmID);
    usb_serial_send(buf, len);
#endif
    initPhaseStep_curr = INIT_PHASE_STEP_ZERO_CALIB_DONE;
}

/* 校准专用FFT缓冲区（2048点） */
static fft_cpx_f32 l_azimFFT_in[2048] = {0.0f};
static fft_cpx_f32 l_azimFFT_out[2048] = {0.0f};
static float l_azimFFTMag[2048] = {0.0f};

/* 校准专用FFT配置（2048点） */
static ne10_fft_r2c_cfg_float32_t calib_fft_cfg = NULL;
static int calib_fft_cfg_initialized = 0;

/* 校准专用FFT静态工作数组（2048点，避免MAX_C2C_NFFT限制） */
static ne10_float32_t calib_c2c_a[2048];
static ne10_float32_t calib_c2c_b[2048];
static ne10_fft_cpx_float32_t calib_c2c_A_cpx[1025];  /* 2048/2 + 1 */
static ne10_fft_cpx_float32_t calib_c2c_B_cpx[1025];

/*
* Function Name: azim_fft_ne10_calib
* Param Input: bv - beamforming vector (complex input data)
*              winAzimuth - azimuth FFT window coefficients
*              numVirtualAnts - number of virtual antennas (valid data length)
*              NFFT_azim - FFT size (including zero-padding)
*              azimFFTMag - output magnitude array
*              pool_index - memory pool index
* Param Return: void
* Function Description: 校准专用2048点方位角FFT，使用独立的静态工作数组避免越界。
*/
static void azim_fft_ne10_calib(fft_cpx_f32* bv, const float* winAzimuth, int numVirtualAnts, int NFFT_azim, float* azimFFTMag, int pool_index) {
    if (!bv || !winAzimuth || !azimFFTMag) {
        fprintf(stderr, "azim_fft_ne10_calib: ERROR: NULL pointer\n");
        return;
    }
    
    /* 初始化2048点FFT配置（仅一次） */
    if (!calib_fft_cfg_initialized) {
        calib_fft_cfg = ne10_fft_alloc_r2c_float32(NFFT_azim);
        if (calib_fft_cfg == NULL) {
            fprintf(stderr, "azim_fft_ne10_calib: 2048点FFT配置分配失败\n");
            return;
        }
        calib_fft_cfg_initialized = 1;
#if(INIT_PHASE_DEBUG)
        // char dbg_buf[64];
        // int dbg_len = snprintf(dbg_buf, sizeof(dbg_buf), "[CALIB] 2048-pt FFT cfg allocated\n");
        // if (dbg_len > 0 && dbg_len < (int)sizeof(dbg_buf)) usb_serial_send(dbg_buf, dbg_len);
#endif
    }
    
    ne10_fft_r2c_cfg_float32_t cfg = calib_fft_cfg;

    fft_cpx_f32 *in = l_azimFFT_in;
    fft_cpx_f32 *out = l_azimFFT_out;
    
    if (!g_memoryPool[pool_index].initialized) {
        fprintf(stderr, "azim_fft_ne10_calib: 内存池未初始化\n");
        return;
    }
    
    /* 加窗 */
    for (int bvIdx = 0; bvIdx < numVirtualAnts; bvIdx ++) {
        in[bvIdx].r = (float)(bv[bvIdx].r * winAzimuth[bvIdx]);
        in[bvIdx].i = (float)(bv[bvIdx].i * winAzimuth[bvIdx]);
    }

    /* zero padding */
    for (int bvIdx = numVirtualAnts; bvIdx < NFFT_azim; bvIdx ++) {
        in[bvIdx].r = 0.0f;
        in[bvIdx].i = 0.0f;
    }

    /* 手动执行2048点C2C FFT（使用校准专用静态数组） */
    ne10_int32_t nfft = NFFT_azim;
    ne10_int32_t half_nfft = nfft / 2;
    
    ne10_float32_t *a = calib_c2c_a;
    ne10_float32_t *b = calib_c2c_b;
    ne10_fft_cpx_float32_t *A_cpx = calib_c2c_A_cpx;
    ne10_fft_cpx_float32_t *B_cpx = calib_c2c_B_cpx;

    /* 步骤1：拆分复序列 */
    for (int k = 0; k < nfft; k++) {
        a[k] = in[k].r;
        b[k] = in[k].i;
    }

    /* 步骤2：实部和虚部分别执行R2C FFT（使用2048点配置） */
    ne10_fft_r2c_1d_float32_mxu(A_cpx, a, cfg);
    ne10_fft_r2c_1d_float32_mxu(B_cpx, b, cfg);

    /* 步骤3：合并为C2C结果 */
    for (int k = 0; k <= half_nfft; k++) {
        out[k].r = A_cpx[k].r - B_cpx[k].i;
        out[k].i = A_cpx[k].i + B_cpx[k].r;
    }

    for (int k = 1; k < half_nfft; k++) {
        ne10_int32_t sym_k = nfft - k;
        ne10_float32_t A_sym_r = A_cpx[k].r;
        ne10_float32_t A_sym_i = -A_cpx[k].i;
        ne10_float32_t B_sym_r = B_cpx[k].r;
        ne10_float32_t B_sym_i = -B_cpx[k].i;

        out[sym_k].r = A_sym_r - B_sym_i;
        out[sym_k].i = A_sym_i + B_sym_r;
    }

    /* 计算幅度 */
    complex_abs_f32_simd(azimFFTMag, (complex float *)out, NFFT_azim);
}

/*
* Function Name: azim_fft_ne10
* Param Input: bv - beamforming vector (complex input data)
*              winAzimuth - azimuth FFT window coefficients
*              numVirtualAnts - number of virtual antennas (valid data length)
*              NFFT_azim - FFT size (including zero-padding)
*              azimFFTMag - output magnitude array
*              pool_index - memory pool index
* Param Return: void
* Function Description: Perform azimuth FFT using NE10 C2C FFT. Applies windowing to the input
*                       beamforming vector, zero-pads to NFFT_azim, executes the FFT, and
*                       computes the magnitude spectrum using SIMD-optimized absolute value.
*/
static void azim_fft_ne10(fft_cpx_f32* bv, const float* winAzimuth, int numVirtualAnts, int NFFT_azim, float* azimFFTMag, int pool_index) {
    if (!bv || !winAzimuth || !azimFFTMag) {
        fprintf(stderr, "azim_fft_ne10: ERROR: NULL pointer (bv=%p, winAzimuth=%p, azimFFTMag=%p)\n",
                (void*)bv, (void*)winAzimuth, (void*)azimFFTMag);
        return;
    }
    // 使用全局内存池中的FFT配置结构体
    ne10_fft_r2c_cfg_float32_t cfg = g_memoryPool[pool_index].azim_fft_cfg;
    if (cfg == NULL) {
        fprintf(stderr, "azim FFT: NE10 FFT配置未初始化\n");
        return;
    }

    // 使用全局内存池中的输入输出缓冲区
    fft_cpx_f32 *in = l_azimFFT_in;
    fft_cpx_f32 *out = l_azimFFT_out;
    
    // 检查内存池是否已初始化
    if (!g_memoryPool[pool_index].initialized) {
        fprintf(stderr, "azim_fft_ne10: 内存池未初始化\n");
        return;
    }
    
    // 加窗
    for (int bvIdx = 0; bvIdx < numVirtualAnts; bvIdx ++) {
        in[bvIdx].r = (float)(bv[bvIdx].r * winAzimuth[bvIdx]);  // real part
        in[bvIdx].i = (float)(bv[bvIdx].i * winAzimuth[bvIdx]);  // imag part
    }

    // zero padding
    for (int bvIdx = numVirtualAnts; bvIdx < NFFT_azim; bvIdx ++) {
        in[bvIdx].r = 0.0f;
        in[bvIdx].i = 0.0f;
    }

    // 执行C2C FFT（正变换）
    ne10_fft_c2c_1d_float32_mxu_ai(out, in, cfg);

    // 存储幅度
    // 版本1（原型）
    // for (int d = 0; d < NFFT_azim; d ++) {
    //     float mag = (float)sqrtf(out[d].r * out[d].r + out[d].i * out[d].i);
    //     azimFFTMag[d] = mag;
    // }
    // 版本2（使用MSA批量优化）
    complex_abs_f32_simd(azimFFTMag, (complex float *)out, NFFT_azim);
}

/*
* Function Name: InitPhaseAngleSample
* Param Input: glbCtx - pointer to global context
*              angCalib - angle calibration vector for phase compensation
*              winAzimuth - azimuth FFT window coefficients
*              dopplerFFT_res - 2D FFT result (range-Doppler complex data)
*              maxMetricIndices - max metric indices from detection
*              numRngBins - number of range bins
*              numDoppBinsPerSubBand - number of Doppler bins per sub-band
*              numVirtualAnts - number of virtual antennas
*              pool_index - memory pool index
* Param Return: void
* Function Description: Sample the measured angle for the current target angle during FFT
*                       calibration. Extracts the BV, performs azimuth FFT, finds the peak,
*                       applies parabolic interpolation to estimate the angle in degrees,
*                       and stores the result in l_AngleMean at the current sample index.
*/
void InitPhaseAngleSample(
    GlbCtx* glbCtx,
    const fft_cpx_f32* angCalib,   // calibration vector
    const float* winAzimuth,      // FFT window
    const fft_cpx_f32* dopplerFFT_res,  // 2dFFT result
    unsigned int* maxMetricIndices,
    int numRngBins,
    int numDoppBinsPerSubBand,
    int numVirtualAnts,
    int pool_index)
{
    if (l_AngleIndx < 1 || l_AngleIndx > l_AngleIndxMax ||
        l_AngleSampleCount >= l_AngleIndx) return;
    if (!glbCtx || !angCalib || !winAzimuth || !dopplerFFT_res || !maxMetricIndices) {
        fprintf(stderr, "[InitPhaseAngleSample] ERROR: NULL pointer (glbCtx=%p, angCalib=%p, winAzimuth=%p, dopplerFFT_res=%p, maxMetricIndices=%p)\n",
                (void*)glbCtx, (void*)angCalib, (void*)winAzimuth, (void*)dopplerFFT_res, (void*)maxMetricIndices);
        return;
    }
    if (pool_index < 0 || pool_index >= MAXPOOLNUM) {
        fprintf(stderr, "[InitPhaseAngleSample] ERROR: invalid pool_index=%d\n", pool_index);
        return;
    }
    if (!g_memoryPool[pool_index].initialized) {
        fprintf(stderr, "[InitPhaseAngleSample] ERROR: memory pool %d not initialized\n", pool_index);
        return;
    }

    unsigned int numTxs = glbCtx->basicParams.numTxs[0];
    unsigned int numRxs = glbCtx->basicParams.numRxs[0];
    unsigned int numTotalSubBands = numTxs + glbCtx->waveParams.numEmptyBands[0];
    unsigned int NFFT_doppler = numDoppBinsPerSubBand * numTotalSubBands;

    int rngIdx = peak_rngIdx;
    int dopIdx = 0;
	// int fft_length = AZM_FFT_SIZE;
    int fft_length = 2048;
    /* ---------- 取 BV ---------- */
	fft_cpx_f32* bv = l_bv;
    if (!bv) {
        fprintf(stderr, "[InitPhaseAngleSample] ERROR: bv is NULL in pool %d\n", pool_index);
        return;
    }
    unsigned int subBandIdx, dopBinIdx_ddmaDec_tx, idx, bv_idx, bv_logIdx;
    for (unsigned int rxIdx = 0; rxIdx < numRxs; rxIdx ++)
    {
        for (unsigned int txIdx = 0; txIdx < numTxs; txIdx++)
        {
        subBandIdx = maxMetricIndices[dopIdx * numRngBins + rngIdx] + txIdx;
            if (subBandIdx >= numTotalSubBands)
            {
		        subBandIdx -= numTotalSubBands;
            }
            dopBinIdx_ddmaDec_tx = dopIdx + subBandIdx * numDoppBinsPerSubBand;
            idx = rxIdx * numRngBins * NFFT_doppler + dopBinIdx_ddmaDec_tx * numRngBins + rngIdx;
            bv_idx = txIdx * numRxs + rxIdx;
            bv_logIdx = glbCtx->radarInfo.logicIdx_reorder_sf1[bv_idx]; // 调整逻辑顺序
            bv[bv_idx].r = (float)(dopplerFFT_res[idx].r * angCalib[bv_idx].r - dopplerFFT_res[idx].i * angCalib[bv_idx].i); // calibration real
            bv[bv_idx].i = (float)(dopplerFFT_res[idx].r * angCalib[bv_idx].i + dopplerFFT_res[idx].i * angCalib[bv_idx].r); // calibration imag
        }
    }

    (void)bv_logIdx;

    /* ---------- FFT（占位） ---------- */
    // float *azmFFT_abs = g_memoryPool[pool_index].azimFFTMag;
    float *azmFFT_abs = l_azimFFTMag;
    /* TODO: FFT use library */
    memset(azmFFT_abs, 0, sizeof(l_azimFFTMag));

    /* 校准模式使用2048点FFT，调用专用函数避免MAX_C2C_NFFT限制 */
    azim_fft_ne10_calib(bv, winAzimuth, numVirtualAnts, fft_length, azmFFT_abs, pool_index);

    /* ---------- Peak Search ---------- */
    int peakIdx = find_max_peak(azmFFT_abs, fft_length);

    /* ---------- Interpolation ---------- */
    // 校准模式使用2048点FFT，需要临时创建对应的角度向量
    static float angFFTvec_calib[2048] = {0.0f};
    static int angFFTvec_calib_initialized = 0;
    if (!angFFTvec_calib_initialized)
    {
        // char dbg_buf[128];
        // int dbg_len = snprintf(dbg_buf, sizeof(dbg_buf), "[CALIB] minElemSpacing=%f, fft_length=%d\n", glbCtx->basic_params.minElemSpacing, fft_length);
        // if (dbg_len > 0 && dbg_len < (int)sizeof(dbg_buf)) usb_serial_send(dbg_buf, dbg_len);
        
        for (int index = 0; index < fft_length; index++)
        {
            int shiftIdx = (index < fft_length / 2) ? index : (index - fft_length);
            float sin_theta = (float)shiftIdx / (fft_length * glbCtx->basicParams.minElemSpacing[0]);
            if (sin_theta > 1.0f)  sin_theta = 1.0f;
            if (sin_theta < -1.0f) sin_theta = -1.0f;
            angFFTvec_calib[index] = rad2deg(asinf(sin_theta));
        }
        angFFTvec_calib_initialized = 1;
        
        // dbg_len = snprintf(dbg_buf, sizeof(dbg_buf), "[CALIB] angFFTvec_calib init done, peakIdx=%d, azimFFT_abs[peak]=%f\n", peakIdx, azmFFT_abs[peakIdx]);
        // if (dbg_len > 0 && dbg_len < (int)sizeof(dbg_buf)) usb_serial_send(dbg_buf, dbg_len);
        // dbg_len = snprintf(dbg_buf, sizeof(dbg_buf), "[CALIB] angFFTvec_calib[0]=%f, [1024]=%f, [2047]=%f\n", angFFTvec_calib[0], angFFTvec_calib[1024], angFFTvec_calib[2047]);
        // if (dbg_len > 0 && dbg_len < (int)sizeof(dbg_buf)) usb_serial_send(dbg_buf, dbg_len);
    }

    float azm_deg = parab_peak(angFFTvec_calib, azmFFT_abs, fft_length, peakIdx, 1);

    l_AngleMean[l_AngleIndx - 1] = azm_deg;
    if (isfinite(azm_deg) && l_AngleSampleCount < l_AngleIndx) {
        l_AngleSampleCount = l_AngleIndx;
        sendFeedbackFormatted(initPhaseCommChannel, "Angle Sample OK: angle=%d, measured=%.3f\n", targetAngle, azm_deg);
    }
#if(INIT_PHASE_DEBUG)
    char buf[64];
    int len;
    len = snprintf(buf, sizeof(buf), "angFFT[%d] FrameID=%d, \n", l_AngleIndx - 1, glbCtx->frmInfo.frmID);
    usb_serial_send(buf, len);
#endif
}

/*
* Function Name: InitPhaseCalibInterface
* Param Input: channel - communication channel (UART, GADGET_SERIAL, or CAN)
*              argc - argument count
*              argv - argument vector
* Param Return: 0 on success, -1 on failure
* Function Description: Unified interface for initial phase calibration commands.
*                       Determines the command type from argv[0] and routes to the
*                       appropriate handler. All feedback messages are sent through
*                       the specified communication channel.
*
* Supported commands:
*   - "InitPhaseCalibPara" <Range> <minAng> <maxAng> <AngStep>
*   - "InitPhaseCalibStart" <0|1|2>
*   - "SetTargetAngle" <angle>
*/
int InitPhaseCalibInterface(InitPhaseCommChannel_t channel, int argc, char *argv[])
{
    if (argc < 2 || argv == NULL || argv[0] == NULL)
    {
        sendFeedback(channel, "Error: Invalid arguments\n", strlen("Error: Invalid arguments\n"));
        return -1;
    }

    /* Save the communication channel for all subsequent feedback */
    /* Only update channel if calibration is in IDLE state to prevent overwriting during calibration */
    if (initPhaseStep_curr == INIT_PHASE_STEP_IDLE)
    {
        initPhaseCommChannel = channel;
    }

    /* Route to appropriate handler based on command name */
    if (strcmp(argv[0], "InitPhaseCalibPara") == 0 || strcmp(argv[0], "setTargetPara") == 0)
    {
        if (argc != 5)
        {
            sendFeedback(channel, "Error: Usage: setTargetPara <Range> <minAng> <maxAng> <AngStep>\n",
                        strlen("Error: Usage: setTargetPara <Range> <minAng> <maxAng> <AngStep>\n"));
            return -1;
        }

        InitPhasePara_t local_para;
        local_para.Range = (int8_t)strtof(argv[1], NULL);
        local_para.minAng = (int8_t)strtof(argv[2], NULL);
        local_para.maxAng = (int8_t)strtof(argv[3], NULL);
        local_para.AngStep = (int8_t)strtof(argv[4], NULL);

        SetTargetPara(&local_para);
        return 0;
    }
    else if (strcmp(argv[0], "InitPhaseCalibStart") == 0 || strcmp(argv[0], "startPhaseCalib") == 0)
    {
        if (argc != 2)
        {
            sendFeedback(channel, "Error: Usage: startPhaseCalib <0|1|2>\n",
                        strlen("Error: Usage: startPhaseCalib <0|1|2>\n"));
            return -1;
        }

        uint8_t local_start = (uint8_t)strtol(argv[1], NULL, 10);
        SetInitPhaseStepFlg((InitPhaseStep_n)local_start);
        sendFeedbackFormatted(channel, "startPhaseCalib %d executed\r\nOK\r\n", local_start);
        return 0;
    }
    else if (strcmp(argv[0], "SetTargetAngle") == 0)
    {
        if (argc != 2)
        {
            sendFeedback(channel, "Error: Usage: SetTargetAngle <angle>\n",
                        strlen("Error: Usage: SetTargetAngle <angle>\n"));
            return -1;
        }

        int32_t angle = (int32_t)strtol(argv[1], NULL, 10);
        SetTargetAngle(angle);
        sendFeedbackFormatted(channel, "Angle set to: %d\r\nOK\r\n", angle);
        return 0;
    }
    else if (channel == INIT_PHASE_COMM_CAN)
    {
        /* TODO: Implement CAN-specific command parsing */
        sendFeedback(channel, "CAN protocol not yet implemented\n",
                    strlen("CAN protocol not yet implemented\n"));
        return -1;
    }
    else
    {
        sendFeedbackFormatted(channel, "Error: Unknown command '%s'\n", argv[0]);
        return -1;
    }
}
