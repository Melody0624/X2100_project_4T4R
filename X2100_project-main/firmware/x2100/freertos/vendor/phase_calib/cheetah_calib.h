#ifndef X2100_CHEETAH_CALIB_H
#define X2100_CHEETAH_CALIB_H

#include <stddef.h>

#include "cheetah.h"

/* Complete 64-entry DDMA Profile RAM: 64 * 16 bits = 32 words. */
#define PHASE_CALIB_RESULT_PROFILE_WORDS 32U
/* Complete configuration block starting at 0x0304: DDMA indices 0..31.
 * This is one 48-word block, not the complete 512-index DDMA RAM. */
#define PHASE_CALIB_RESULT_DDMA_WORDS    48U
#define PHASE_CALIB_FAILURE_REASON_MAX   128U
#define PHASE_CALIB_RESULT_PROTOCOL_VERSION 1U

/* Restore the normal radar configuration and restart point-cloud capture
 * after local phase calibration.  Keep disabled by default so the radar
 * remains stopped after calibration; host commands may configure/start it
 * later.  Set to 1 to retain the previous automatic restart behavior. */
#ifndef PHASE_CALIB_AUTO_RESTART_RADAR
#define PHASE_CALIB_AUTO_RESTART_RADAR   0
#endif

typedef enum {
    PHASE_CALIB_RESULT_NOT_CALIBRATED = 0,
    PHASE_CALIB_RESULT_CALIBRATING,
    PHASE_CALIB_RESULT_SUCCESS,
    PHASE_CALIB_RESULT_FAILED,
} PhaseCalibResultStatus;

typedef enum {
    PHASE_CALIB_FAILURE_NONE = 0,
    PHASE_CALIB_FAILURE_START = 1,
    PHASE_CALIB_FAILURE_PREPARE = 2,
    PHASE_CALIB_FAILURE_STEP1 = 3,
    PHASE_CALIB_FAILURE_STEP2 = 4,
    PHASE_CALIB_FAILURE_STEP2_MAG_SPREAD = 5,
    PHASE_CALIB_FAILURE_STEP3 = 6,
    PHASE_CALIB_FAILURE_NOT_CONVERGED = 7,
    PHASE_CALIB_FAILURE_FLASH_SAVE = 8,
} PhaseCalibFailureCode;

typedef struct {
    PhaseCalibResultStatus status;
    PhaseCalibFailureCode failure_code;
    char failure_reason[PHASE_CALIB_FAILURE_REASON_MAX];
    int has_saved_result;
    int has_best_empty_band_dbc;
    uint16_t best_iteration;
    uint16_t profile_words;
    uint16_t ddma_words;
    double acceptance_threshold_dbc;
    double good_threshold_dbc;
    double best_empty_band_dbc;
    uint32_t profile[PHASE_CALIB_RESULT_PROFILE_WORDS];
    uint32_t ddma[PHASE_CALIB_RESULT_DDMA_WORDS];
} PhaseCalibQueryResult;

int phase_calib_start(void);
int phase_calib_is_running(void);
int phase_calib_apply_saved_regs(struct reg_line *regs, size_t count);
int phase_calib_get_result(PhaseCalibQueryResult *result);
/* Restore a previously exported result.  The firmware reconstructs the
 * record metadata and checksum; the host supplies only the measured result
 * and the complete 0x00A0/0x0304 payloads. */
int phase_calib_restore_record(const uint32_t *profile,
                              uint16_t profile_words,
                              const uint32_t *ddma,
                              uint16_t ddma_words,
                              uint16_t best_iteration,
                              double best_empty_band_dbc);

/* The convergence stage uses the compiled cheetah_128_512_config waveform
 * from vendor.c as its single source of truth.  The caller must copy it
 * before overlaying the calibration result or inserting capture commands. */
const struct reg_line *x2100_get_normal_radar_config(size_t *count);

#endif
