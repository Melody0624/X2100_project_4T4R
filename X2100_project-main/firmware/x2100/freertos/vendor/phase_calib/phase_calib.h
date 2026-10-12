/*
 * parser.h
 *
 *  Created on: 4 16, 2025
 *      Author: lichunlai
 */

#ifndef PHASE_CALIB_H_
#define PHASE_CALIB_H_

#include <stdint.h>

#include "wave2regs_intf.h"
#include "calib_misc.h"

#ifdef __cplusplus
extern "C" {
#endif

ErrStart_t firstCalibrationProcess(CalibCfg_t *cfg, int16_t *adc_Data, int *IQ_idx_table);
ErrStart_t secondCalibrationProcess(CalibCfg_t *cfg, int16_t *adc_data, int *IQ_idx_table);
ErrStart_t checkConvergenceProcess(CalibCfg_t *cfg, int16_t *adc_data, int *IQ_idx_table, int sfIdx);

#ifdef __cplusplus
}
#endif

#endif /* PARSER_H_ */
