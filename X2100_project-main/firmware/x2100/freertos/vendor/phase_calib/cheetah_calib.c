/* X2100 on-device phase-shifter calibration, ported from capture/cheetah_calib.c. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <driver/cache.h>
#include <os/freertos/include/FreeRTOS.h>
#include <os/freertos/include/task.h>
#include <os/freertos/include/semphr.h>

#include "heap_malloc.h"
#include "memory_pool.h"
#include "../config_manager.h"
#include "cheetah.h"
#include "cheetah_calib.h"
#include "phase_calib.h"
#include "wave2regs_intf.h"
#include "x2100_calib_configs.h"

#define PHASE_CALIB_PROFILE_ADDR       0x00A0
#define PHASE_CALIB_DDMA_ADDR          0x0304
#define PHASE_CALIB_PROFILE_WORDS      PHASE_CALIB_RESULT_PROFILE_WORDS
#define PHASE_CALIB_DDMA_WORDS         PHASE_CALIB_RESULT_DDMA_WORDS
#define PHASE_CALIB_LEGACY_DDMA_WORDS  6U
#define PHASE_CALIB_REG_CAPACITY       128U
#define PHASE_CALIB_TASK_STACK         8192U
#define PHASE_CALIB_TASK_PRIORITY      2U
#define PHASE_CALIB_FLASH_MAGIC        0x5043414cU
#define PHASE_CALIB_FLASH_VERSION      2U
#define X2100_PROFILE_SLOTS            4
#define X2100_CALIB_SCAN_TX            4
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
#define X2100_CALIB_TX_MASK            0x0FU
#define X2100_CALIB_BANDS              8
#else
#define X2100_CALIB_TX_MASK            ((1U << 0) | (1U << 1))
#define X2100_CALIB_BANDS              4
#endif
#define X2100_CALIB_REPEATS            2
#define X2100_PHASE_SWEEP_CODES        64
#define X2100_SF1_BPM_ENABLE_MASK      (1U << 18)
#define X2100_SF1_DDMA_ENABLE_MASK     (1U << 16)
#define X2100_FIRST_CHIRPS             \
    (X2100_PHASE_SWEEP_CODES * X2100_CALIB_SCAN_TX * X2100_CALIB_REPEATS)
#define X2100_WORK_CHIRPS              128
#define X2100_CALIB_ADC_SAMPLES        506
/* 0x0018 encodes FSTEP=200; 200 * 0.09765625 MHz/us. */
#define X2100_CHIRP_SLOPE_HZ_PER_S     19.53125E12
/* 0x1051[7:6]: calibration stages=1 (/2), work stage=2 (/4). */
#define X2100_CALIB_SAMPLE_RATE_HZ     53.33E6
#define X2100_WORK_SAMPLE_RATE_HZ      26.665E6

#define BYTE_ALIGN(size, align) ((((size) + (align) - 1) / (align)) * (align))

extern SemaphoreHandle_t calib_adc_sem;
extern volatile uint8_t g_ps_cal_mode;
extern volatile int g_ps_cal_ready_pool_index;
extern volatile int g_ps_cal_capture_start_error;
extern int enter_local_ps_cal_mode(void);
extern int exit_local_ps_cal_mode(void);
extern int radar_capture_host_stop(void);
extern int radar_capture_host_start(void);
extern int radar_capture_reconfig_stop(void);
extern int host_send_config_info(int chirp_num, int sample_num);
extern void configure_host_frame_count(int input_cnt);

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t profile_words;
    uint16_t ddma_words;
    uint16_t best_iteration;
    uint32_t checksum;
    uint32_t profile[PHASE_CALIB_PROFILE_WORDS];
    uint32_t ddma[PHASE_CALIB_DDMA_WORDS];
    double best_empty_band_dbc;
} PhaseCalibFlashRecord;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t profile_words;
    uint16_t ddma_words;
    uint16_t reserved;
    uint32_t checksum;
    uint32_t profile[PHASE_CALIB_PROFILE_WORDS];
    uint32_t ddma[18];
} PhaseCalibFlashRecordV1;

static volatile int s_phase_calibrating;
static volatile PhaseCalibResultStatus s_phase_calib_last_status =
    PHASE_CALIB_RESULT_NOT_CALIBRATED;
static volatile PhaseCalibFailureCode s_phase_calib_failure_code =
    PHASE_CALIB_FAILURE_NONE;
static char s_phase_calib_failure_reason[PHASE_CALIB_FAILURE_REASON_MAX] =
    "NONE";

static void phase_calib_set_failure(PhaseCalibFailureCode code,
                                    const char *format, ...)
{
    va_list args;

    s_phase_calib_failure_code = code;
    if (format == NULL) {
        snprintf(s_phase_calib_failure_reason,
                 sizeof(s_phase_calib_failure_reason), "NONE");
        return;
    }
    va_start(args, format);
    vsnprintf(s_phase_calib_failure_reason,
              sizeof(s_phase_calib_failure_reason), format, args);
    va_end(args);
}

/*
 * Both calibration waveforms scan TX1..TX4 sequentially and repeat every
 * phase twice: stage 1 uses 64 * N_TX * 2 chirps and stage 2 uses
 * num_bands * N_TX * 2 chirps.  The 2TX build excludes TX3/TX4 from phase
 * processing; the 4TX build calibrates all four physical paths. N_TX
 * is the four-column scan stride; calib_tx_mask identifies the PCB TX channels with valid RF
 * paths; N_TX_PER_MMIC controls the four Profile RAM slots;
 * num_signal_bands is derived from calib_tx_mask for the convergence metric.
 */
static const CalibCfg_t s_phase_calib_template = {
    .cascade = 0,
    .numMMIC = 1,
    .modulation = TDM | DDMA,
    .num_subframe = 1,
    .num_adc = {X2100_CALIB_ADC_SAMPLES},
    .num_chirp = {X2100_FIRST_CHIRPS},
    .ddma_num = {64},
    .ddma_chirp = {X2100_CALIB_SCAN_TX},
    .num_bands = {X2100_CALIB_BANDS},
    .MAX_NUM_BANDS = 16,
    .nominal_phase_table = {
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
        {0,   0,   0,   0,   0,   0,   0,   0},
        {0,  45,  90, 135, 180, 225, 270, 315},
        {0,  90, 180, 270,   0,  90, 180, 270},
        {0, 135, 270,  45, 180, 315,  90, 225},
#else
        {0,   0,   0,   0},
        {0,  90, 180, 270},
#endif
    },
    /* Like the ZU3 calculation, zero-filled unused rows contain one unique
     * phase (0 deg); keep 1 here so four-slot Profile RAM generation is safe. */
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    .NUM_PHASE_PER_TX = {1, 8, 4, 8},
#else
    .NUM_PHASE_PER_TX = {1, 4, 1, 1},
#endif
    .N_TX = X2100_CALIB_SCAN_TX,
    .calib_tx_mask = X2100_CALIB_TX_MASK,
    .num_calib_tx = 0,
    .N_RX = 4,
    .N_TX_PER_MMIC = X2100_PROFILE_SLOTS,
    .MAX_ITERATION = 3,
    .good_empty_dbc = -35.0,
    .DEBUG = 0,
    .filterNumRows = 3,
    .slope = {X2100_CHIRP_SLOPE_HZ_PER_S},
    .Fs = X2100_CALIB_SAMPLE_RATE_HZ,
    .max_mag_diff_db = 1.0,
    .max_empty_dbc = -30.0,
};

static int prepare_phase_calib_topology(CalibCfg_t *cfg)
{
    uint32_t allowed_mask;
    int num_bands;
    int active_count = 0;

    /* This on-device path currently persists/applies one Cheetah MMIC and
     * packs four physical TX profile fields per DDMA band.  Different PCB
     * antenna populations are selected by calib_tx_mask; they must not
     * change the four-slot hardware stride. */
    if (cfg == NULL || cfg->numMMIC != 1 || cfg->N_TX_PER_MMIC != 4 ||
        cfg->N_TX != cfg->N_TX_PER_MMIC || cfg->MAX_NUM_BANDS != 16)
        return -1;
    num_bands = cfg->num_bands[0];
    if (num_bands <= 0 || num_bands > cfg->MAX_NUM_BANDS ||
        num_bands > 16)
        return -1;

    allowed_mask = (1U << cfg->N_TX) - 1U;
    if (cfg->calib_tx_mask == 0U ||
        (cfg->calib_tx_mask & ~allowed_mask) != 0U)
        return -1;

    for (int tx = 0; tx < cfg->N_TX; tx++) {
        int unique_count = 0;

        if ((cfg->calib_tx_mask & (1U << tx)) == 0U) {
            cfg->NUM_PHASE_PER_TX[tx] = 1;
            continue;
        }
        active_count++;
        for (int band = 0; band < num_bands; band++) {
            int phase = cfg->nominal_phase_table[tx][band];
            int seen = 0;

            if (phase < 0 || phase >= 360)
                return -1;
            for (int previous = 0; previous < band; previous++) {
                if (cfg->nominal_phase_table[tx][previous] == phase) {
                    seen = 1;
                    break;
                }
            }
            if (!seen)
                unique_count++;
        }
        if (unique_count <= 0 || num_bands % unique_count != 0)
            return -1;
        /* table_gen() calibrates the first unique phase cycle and repeats
         * its result across all bands, so the configured row must consist
         * of complete repetitions of that first cycle. */
        for (int band = 0; band < num_bands; band++) {
            if (cfg->nominal_phase_table[tx][band] !=
                cfg->nominal_phase_table[tx][band % unique_count])
                return -1;
        }
        cfg->NUM_PHASE_PER_TX[tx] = unique_count;
    }

    cfg->num_calib_tx = active_count;
    /* The CHECK waveform assigns one occupied DDMA band to each valid TX. */
    cfg->num_signal_bands = active_count;
    if (cfg->num_signal_bands >= num_bands)
        return -1;
    cfg->num_chirp[0] = X2100_PHASE_SWEEP_CODES * cfg->N_TX *
                        X2100_CALIB_REPEATS;
    cfg->ddma_chirp[0] = cfg->N_TX;
    return 0;
}

static uint32_t phase_calib_checksum(const void *record, size_t record_size,
                                     size_t checksum_offset)
{
    const uint8_t *data = (const uint8_t *)record;
    uint32_t hash = 2166136261U;

    for (size_t i = 0; i < record_size; i++) {
        if (i >= checksum_offset &&
            i < checksum_offset + sizeof(uint32_t))
            continue;
        hash = (hash ^ data[i]) * 16777619U;
    }
    return hash;
}

static const struct reg_line *find_normal_radar_config_line(int16_t addr,
                                                             uint16_t words)
{
    size_t count = 0;
    const struct reg_line *regs = x2100_get_normal_radar_config(&count);

    if (regs == NULL)
        return NULL;
    for (size_t i = 0; i < count; i++) {
        if (regs[i].chipIdx == 0 && regs[i].addr == addr &&
            regs[i].valLen == words)
            return &regs[i];
    }
    return NULL;
}

static int phase_calib_load_record(PhaseCalibFlashRecord *record)
{
    PhaseCalibFlashRecordV1 legacy;

    if (record == NULL ||
        param_get(PARAM_CHEETAH_CALIB_CFG, record, sizeof(*record)) != 0)
        return -1;
    if (record->magic == PHASE_CALIB_FLASH_MAGIC &&
        record->version == PHASE_CALIB_FLASH_VERSION &&
        record->profile_words == PHASE_CALIB_PROFILE_WORDS &&
        record->ddma_words == PHASE_CALIB_DDMA_WORDS &&
        record->checksum == phase_calib_checksum(
            record, sizeof(*record),
            offsetof(PhaseCalibFlashRecord, checksum)))
        return 0;

    /* Version 1 stored only the generated six DDMA words.  Preserve upgrade
     * compatibility by combining them with the verified complete 48-word
     * engineering line.  Its convergence metric was not persisted. */
    if (param_get(PARAM_CHEETAH_CALIB_CFG, &legacy, sizeof(legacy)) != 0 ||
        legacy.magic != PHASE_CALIB_FLASH_MAGIC || legacy.version != 1U ||
        legacy.profile_words != PHASE_CALIB_PROFILE_WORDS ||
        legacy.ddma_words != PHASE_CALIB_LEGACY_DDMA_WORDS ||
        legacy.checksum != phase_calib_checksum(
            &legacy, sizeof(legacy),
            offsetof(PhaseCalibFlashRecordV1, checksum)))
        return -1;

    memset(record, 0, sizeof(*record));
    record->magic = PHASE_CALIB_FLASH_MAGIC;
    record->version = PHASE_CALIB_FLASH_VERSION;
    record->profile_words = PHASE_CALIB_PROFILE_WORDS;
    record->ddma_words = PHASE_CALIB_DDMA_WORDS;
    memcpy(record->profile, legacy.profile, sizeof(record->profile));
    const struct reg_line *line = find_normal_radar_config_line(
        PHASE_CALIB_DDMA_ADDR, PHASE_CALIB_DDMA_WORDS);

    if (line == NULL)
        return -1;
    memcpy(record->ddma, line->value, sizeof(record->ddma));
    memcpy(record->ddma, legacy.ddma,
           PHASE_CALIB_LEGACY_DDMA_WORDS * sizeof(record->ddma[0]));
    record->checksum = phase_calib_checksum(
        record, sizeof(*record),
        offsetof(PhaseCalibFlashRecord, checksum));
    return 0;
}

static int phase_calib_load_saved_record(PhaseCalibFlashRecord *record,
                                         int *has_best_empty_band_dbc)
{
    if (phase_calib_load_record(record) != 0)
        return -1;
    if (has_best_empty_band_dbc != NULL)
        *has_best_empty_band_dbc =
            record->version == PHASE_CALIB_FLASH_VERSION &&
            record->best_iteration != 0;
    return 0;
}

static int phase_calib_record_from_regs(const ParamsRegsConfig *config,
                                        double best_empty_band_dbc,
                                        uint16_t iteration,
                                        PhaseCalibFlashRecord *record)
{
    int profile_found = 0;
    int ddma_found = 0;

    if (config == NULL || config->regs == NULL || record == NULL)
        return -1;

    memset(record, 0, sizeof(*record));
    record->magic = PHASE_CALIB_FLASH_MAGIC;
    record->version = PHASE_CALIB_FLASH_VERSION;
    record->best_iteration = iteration;
    record->best_empty_band_dbc = best_empty_band_dbc;

    for (size_t i = 0; i < config->count; i++) {
        const struct reg_line *line = &config->regs[i];

        if (line->chipIdx != 0)
            continue;
        if (line->addr == PHASE_CALIB_PROFILE_ADDR) {
            if (line->valLen != PHASE_CALIB_PROFILE_WORDS)
                return -1;
            record->profile_words = line->valLen;
            memcpy(record->profile, line->value,
                   line->valLen * sizeof(line->value[0]));
            profile_found = 1;
        } else if (line->addr == PHASE_CALIB_DDMA_ADDR) {
            if (line->valLen != PHASE_CALIB_DDMA_WORDS)
                return -1;
            record->ddma_words = line->valLen;
            memcpy(record->ddma, line->value,
                   line->valLen * sizeof(line->value[0]));
            ddma_found = 1;
        }
    }

    if (!profile_found || !ddma_found)
        return -1;
    record->checksum = phase_calib_checksum(
        record, sizeof(*record), offsetof(PhaseCalibFlashRecord, checksum));
    return 0;
}

static int phase_calib_save_record(const PhaseCalibFlashRecord *record)
{
    PhaseCalibFlashRecord verify;

    if (record == NULL ||
        record->checksum != phase_calib_checksum(
            record, sizeof(*record),
            offsetof(PhaseCalibFlashRecord, checksum)) ||
        param_set(PARAM_CHEETAH_CALIB_CFG, record, sizeof(*record)) != 0)
        return -1;

    if (phase_calib_load_record(&verify) != 0 ||
        memcmp(record, &verify, sizeof(*record)) != 0)
        return -1;
    return 0;
}

int phase_calib_restore_record(const uint32_t *profile,
                               uint16_t profile_words,
                               const uint32_t *ddma,
                               uint16_t ddma_words,
                               uint16_t best_iteration,
                               double best_empty_band_dbc)
{
    PhaseCalibFlashRecord record;

    if (s_phase_calibrating || g_ps_cal_mode || profile == NULL || ddma == NULL ||
        profile_words != PHASE_CALIB_PROFILE_WORDS ||
        ddma_words != PHASE_CALIB_DDMA_WORDS || best_iteration == 0 ||
        best_empty_band_dbc != best_empty_band_dbc ||
        best_empty_band_dbc > 1.0e6 || best_empty_band_dbc < -1.0e6)
        return -1;

    memset(&record, 0, sizeof(record));
    record.magic = PHASE_CALIB_FLASH_MAGIC;
    record.version = PHASE_CALIB_FLASH_VERSION;
    record.profile_words = profile_words;
    record.ddma_words = ddma_words;
    record.best_iteration = best_iteration;
    record.best_empty_band_dbc = best_empty_band_dbc;
    memcpy(record.profile, profile, sizeof(record.profile));
    memcpy(record.ddma, ddma, sizeof(record.ddma));
    record.checksum = phase_calib_checksum(
        &record, sizeof(record), offsetof(PhaseCalibFlashRecord, checksum));

    if (phase_calib_save_record(&record) != 0)
        return -1;

    /* A restored, checksum-verified record is a valid calibration result.
     * Clear the status of any earlier failed attempt so CalibResult cannot
     * report FAILED together with a newly restored valid record. */
    phase_calib_set_failure(PHASE_CALIB_FAILURE_NONE, "NONE");
    s_phase_calib_last_status = PHASE_CALIB_RESULT_SUCCESS;
    printf("[PhaseCalib] Restored exported calibration record: "
           "iteration=%u, empty-band=%.2f dBc.\n",
           record.best_iteration, record.best_empty_band_dbc);
    return 0;
}

int phase_calib_apply_saved_regs(struct reg_line *regs, size_t count)
{
    PhaseCalibFlashRecord record;
    int profile_found = 0;
    int ddma_found = 0;

    if (regs == NULL || phase_calib_load_saved_record(&record, NULL) != 0)
        return 0;

    for (size_t i = 0; i < count; i++) {
        if (regs[i].chipIdx != 0)
            continue;
        if (regs[i].addr == PHASE_CALIB_PROFILE_ADDR) {
            memcpy(regs[i].value, record.profile,
                   record.profile_words * sizeof(record.profile[0]));
            regs[i].valLen = record.profile_words;
            profile_found = 1;
        } else if (regs[i].addr == PHASE_CALIB_DDMA_ADDR) {
            memcpy(regs[i].value, record.ddma,
                   record.ddma_words * sizeof(record.ddma[0]));
            regs[i].valLen = record.ddma_words;
            ddma_found = 1;
        }
    }

    if (profile_found && ddma_found)
        printf("[PhaseCalib] Applied saved phase-shifter calibration.\n");
    return profile_found && ddma_found;
}

static int update_phase_regs(CalibCfg_t *cfg, ParamsRegsConfig *config)
{
    size_t generated_ddma_words;
    int profile_found = 0;
    int ddma_found = 0;

    if (cfg == NULL || cfg->num_bands[0] <= 0)
        return -1;
    generated_ddma_words =
        BYTE_ALIGN((size_t)cfg->num_bands[0] * 6U, 4U) / 4U;
    if (generated_ddma_words > PHASE_CALIB_DDMA_WORDS ||
        generated_ddma_words >
            sizeof(cfg->DDMAProfileindexReg[0]) /
            sizeof(cfg->DDMAProfileindexReg[0][0]))
        return -1;

    for (size_t i = 0; i < config->count; i++) {
        struct reg_line *line = &config->regs[i];

        if (line->chipIdx != 0)
            continue;
        if (line->addr == PHASE_CALIB_PROFILE_ADDR) {
            if (line->valLen != PHASE_CALIB_PROFILE_WORDS)
                return -1;
            memcpy(line->value, cfg->PhaseShifterReg[0],
                   PHASE_CALIB_PROFILE_WORDS * sizeof(line->value[0]));
            profile_found = 1;
        } else if (line->addr == PHASE_CALIB_DDMA_ADDR) {
            if (line->valLen != PHASE_CALIB_DDMA_WORDS)
                return -1;
            /* Overlay every generated DDMA band on the verified 48-word
             * stage line.  The complete line is sent, evaluated, snapshotted
             * and finally persisted. */
            memcpy(line->value, cfg->DDMAProfileindexReg[0],
                   generated_ddma_words * sizeof(line->value[0]));
            ddma_found = 1;
        }
    }
    return profile_found && ddma_found ? 0 : -1;
}

static int update_capture_geometry(const CalibCfg_t *cfg,
                                   ParamsRegsConfig *config)
{
    int found = 0;

    if (cfg == NULL || config == NULL || cfg->num_chirp[0] <= 0 ||
        cfg->num_adc[0] <= 0)
        return -1;
    for (size_t i = 0; i < config->count; i++) {
        struct reg_line *line = &config->regs[i];

        if (line->chipIdx != 0 || line->addr != CMD_SEND_CONFIGINFO)
            continue;
        if (line->valLen != 3 || found)
            return -1;
        line->value[1] = (uint32_t)cfg->num_chirp[0];
        line->value[2] = (uint32_t)cfg->num_adc[0];
        found = 1;
    }
    return found ? 0 : -1;
}

static int copy_normal_work_stage_config(ParamsRegsConfig *out)
{
    static const struct reg_line capture_geometry = {
        0, CMD_SEND_CONFIGINFO, 3, {1, X2100_WORK_CHIRPS,
                                    X2100_CALIB_ADC_SAMPLES}
    };
    static const struct reg_line radar_start = {
        0, CMD_RADAR_START, 1, {0}
    };
    size_t source_count = 0;
    size_t out_count = 0;
    int geometry_inserted = 0;
    int waveform_mode_found = 0;
    const struct reg_line *source = x2100_get_normal_radar_config(&source_count);

    if (out == NULL || out->regs == NULL || source == NULL ||
        source_count + 2U > PHASE_CALIB_REG_CAPACITY)
        return -1;

    for (size_t i = 0; i < source_count; i++) {
        out->regs[out_count] = source[i];
        if (source[i].chipIdx == 0 && source[i].addr == 0x1029) {
            if (waveform_mode_found || source[i].valLen != 1)
                return -1;
            /* The normal engineering waveform may enable BPM, but the
             * calibration convergence algorithm evaluates the configured DDMA bands
             * and does not decode BPM.  Modify only this private stage copy:
             * normal radar operation keeps the vendor BPM setting. */
            out->regs[out_count].value[0] &= ~X2100_SF1_BPM_ENABLE_MASK;
            if ((out->regs[out_count].value[0] &
                 X2100_SF1_DDMA_ENABLE_MASK) == 0)
                return -1;
            waveform_mode_found = 1;
        }
        out_count++;
        if (source[i].chipIdx == 0 && source[i].addr == 0x10A0) {
            if (geometry_inserted)
                return -1;
            out->regs[out_count++] = capture_geometry;
            geometry_inserted = 1;
        }
    }
    if (!geometry_inserted || !waveform_mode_found)
        return -1;
    out->regs[out_count++] = radar_start;
    out->count = out_count;
    return 0;
}

static int copy_stage_config(Cheetah_PhaseShifter_Calibration_Flow flow,
                             CalibCfg_t *cfg, ParamsRegsConfig *out)
{
    const ParamsRegsConfig *source = NULL;

    if (flow == FIRST_CALIBRATION)
        source = &X2100_first_phaseCalib_default_config;
    else if (flow == SECOND_CALIBRATION)
        source = &X2100_second_phaseCalib_default_config;
    else if (flow == CHECK_CONVERGENCE) {
        if (copy_normal_work_stage_config(out) != 0)
            return -1;
    }
    else
        return -1;

    if (source != NULL) {
        if (source->count > PHASE_CALIB_REG_CAPACITY)
            return -1;
        memcpy(out->regs, source->regs,
               source->count * sizeof(out->regs[0]));
        out->count = source->count;
    }
    if (update_capture_geometry(cfg, out) != 0)
        return -1;
    if (flow != FIRST_CALIBRATION && update_phase_regs(cfg, out) != 0)
        return -1;
    return 0;
}

void copyResetCheetahCfg(CalibCfg_t *cfg, ParamsRegsConfig *outCfg)
{
    (void)cfg;
    if (outCfg != NULL)
        *outCfg = X2100_reset_phaseCalib_default_config;
}

static int reset_cheetah_calibration(void)
{
    int ret;

    /* Wake Cheetah only when the next stage is ready to be configured. */
    ret = set_regs_to_target_checked(
        X2100_reset_phaseCalib_default_config.regs,
        X2100_reset_phaseCalib_default_config.count);
    if (ret != 0)
        printf("[PhaseCalib] Cheetah reset configuration failed: %d.\n", ret);
    return ret;
}

static int prepare_stage_capture(ParamsRegsConfig *regsCfg)
{
    int geometry_index = -1;
    int start_index = -1;
    int chirps;
    int samples;
    int ret;

    if (regsCfg == NULL || regsCfg->regs == NULL || regsCfg->count == 0)
        return -1;

    for (size_t i = 0; i < regsCfg->count; i++) {
        if (regsCfg->regs[i].addr == CMD_SEND_CONFIGINFO) {
            if (geometry_index >= 0 || regsCfg->regs[i].valLen != 3)
                return -1;
            geometry_index = (int)i;
        } else if (regsCfg->regs[i].addr == CMD_RADAR_START) {
            if (start_index >= 0)
                return -1;
            start_index = (int)i;
        }
    }
    if (geometry_index < 0 || start_index < 0)
        return -1;

    chirps = regsCfg->regs[geometry_index].value[1];
    samples = regsCfg->regs[geometry_index].value[2];
    ret = host_send_config_info(chirps, samples);
    if (ret != 0)
        return ret;

    /* These are X2100 host commands, not Cheetah registers.  Apply the
     * geometry now, but defer host start until the configured Cheetah has
     * returned to its ready/idle state.  Remove from the larger index first
     * so the remaining index is unchanged. */
    int remove_index[2] = {geometry_index, start_index};
    if (remove_index[0] < remove_index[1]) {
        int tmp = remove_index[0];
        remove_index[0] = remove_index[1];
        remove_index[1] = tmp;
    }
    for (int command = 0; command < 2; command++) {
        int index = remove_index[command];

        if ((size_t)index + 1U < regsCfg->count) {
            memmove(&regsCfg->regs[index], &regsCfg->regs[index + 1],
                    (regsCfg->count - (size_t)index - 1U) *
                    sizeof(regsCfg->regs[0]));
        }
        regsCfg->count--;
    }
    return 0;
}

static int wait_stage_radar_ready(int flow)
{
    const uint32_t frame_en_mask = 0x10000000U;
    const uint32_t dd_mode_mask = 0x00000018U;
    const uint32_t system_ready_mask = 0x00000004U;
    uint32_t frame_mode = 0;
    uint32_t int_status = 0;
    int mode_ret = -1;
    int int_ret;

    for (int poll = 0; poll < 50; poll++) {
        mode_ret = cheetah_reg_read_single(0, 0x1053, &frame_mode,
                                           CHEETAH_CHECK);
        if (mode_ret == 0 && (frame_mode & frame_en_mask) == 0 &&
            (frame_mode & dd_mode_mask) == 0 &&
            (frame_mode & system_ready_mask) != 0)
            return 0;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    int_ret = cheetah_reg_read_single(0, 0x10D2, &int_status,
                                      CHEETAH_CHECK);
    printf("[PhaseCalib] Stage %d radar not ready before capture: "
           "0x1053=0x%08x (ret=%d), 0x10D2=0x%08x (ret=%d).\n",
           flow, frame_mode, mode_ret, int_status, int_ret);
    return mode_ret != 0 ? mode_ret : -1;
}

int calibProcessing(Cheetah_PhaseShifter_Calibration_Flow flow,
                    CalibCfg_t *cfg, ParamsRegsConfig *regsCfg)
{
    int ret = -1;

    if (cfg == NULL || regsCfg == NULL) {
        printf("[PhaseCalib] Invalid stage configuration.\n");
        return -1;
    }

    /*
     * enter_local_ps_cal_mode() and collect_calibration_adc() both leave
     * Cheetah in the low-power state entered by cheetah_stop().  Keep it in
     * that state while ADC processing is running, then wake it only when the
     * next stage is ready to be configured.
     *
     * The reset/wake sequence must precede the reconfiguration reset.  Do not
     * call cheetah_stop() again here: doing so would put Cheetah back into low
     * power immediately before the stage registers are applied.
     */
    for (int attempt = 1; attempt <= 2; attempt++) {
        if (copy_stage_config(flow, cfg, regsCfg) != 0) {
            printf("[PhaseCalib] Invalid stage configuration.\n");
            return -1;
        }

        ret = radar_capture_host_stop();
        if (ret == 0)
            ret = prepare_stage_capture(regsCfg);
        if (ret == 0)
            ret = reset_cheetah_calibration();
        if (ret == 0)
            ret = radar_capture_reconfig_stop();
        if (ret == 0)
            vTaskDelay(pdMS_TO_TICKS(500));
        if (ret != 0) {
            printf("[PhaseCalib] Stage %d preparation failed: %d.\n",
                   flow, ret);
            return ret;
        }
        while (xSemaphoreTake(calib_adc_sem, 0) == pdTRUE) { }
        g_ps_cal_capture_start_error = 0;
        configure_host_frame_count(1);
        ret = set_regs_to_target_checked(regsCfg->regs, regsCfg->count);
        if (ret == 0)
            ret = wait_stage_radar_ready(flow);
        if (ret == 0)
            return radar_capture_host_start();

        if (attempt == 1) {
            printf("[PhaseCalib] Retry stage %d configuration once because "
                   "Cheetah did not return ready.\n", flow);
            continue;
        }
        printf("[PhaseCalib] Stage %d configuration failed: %d.\n", flow, ret);
    }
    return ret;
}

static int16_t *collect_calibration_adc(const CalibCfg_t *cfg, const char *stage,
                                        int *timed_out)
{
    const int chirps = cfg->num_chirp[0];
    const int samples = cfg->num_adc[0];
    const int rxs = cfg->N_RX;
    int pool_index;
    int ret;
    char *frame;
    int16_t *adc;
    size_t src_offset = 0;

    if (timed_out != NULL)
        *timed_out = 0;
    if (xSemaphoreTake(calib_adc_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
        printf("[PhaseCalib] ADC timeout in %s.\n", stage);
        if (timed_out != NULL)
            *timed_out = 1;
        return NULL;
    }
    if (g_ps_cal_capture_start_error != 0) {
        printf("[PhaseCalib] Capture failed for %s: %d.\n",
               stage, g_ps_cal_capture_start_error);
        if (timed_out != NULL)
            *timed_out = 2;
        return NULL;
    }

    /*
     * The semaphore is published immediately after the raw frame memcpy.
     * At that point the capture thread still owns the camera frame and the
     * radar can still be streaming.  Wait until that thread has returned the
     * frame, stopped Cheetah and completed camera_stream_off() before the
     * calibration task consumes the pool or changes radar registers.
     */
    ret = radar_capture_host_stop();
    if (ret != 0) {
        printf("[PhaseCalib] Failed to stop capture after %s ADC: %d.\n",
               stage, ret);
        return NULL;
    }

    pool_index = g_ps_cal_ready_pool_index;
    if (pool_index < 0 || pool_index >= MAXPOOLNUM ||
        !g_memoryPool[pool_index].initialized ||
        g_memoryPool[pool_index].saver_pkt_buf_raw == NULL) {
        printf("[PhaseCalib] Invalid ADC pool after %s: index=%d.\n",
               stage, pool_index);
        return NULL;
    }
    frame = g_memoryPool[pool_index].saver_pkt_buf_raw;

    adc = malloc((size_t)chirps * samples * rxs * sizeof(*adc));
    if (adc == NULL)
    {  
        printf("[PhaseCalib] malloc adc buffer error %s.\n", stage);
        return NULL;
    }

    for (int chirp = 0; chirp < chirps; chirp++) {
        CheetahChirpHeadInfo header;
        const uint16_t *raw;

        memcpy(&header, frame + src_offset, sizeof(header));
        if (cfg->ddma_num[0] == cfg->num_bands[0] &&
            cfg->ddma_chirp[0] == cfg->N_TX &&
            chirps == cfg->num_bands[0] * cfg->N_TX *
                          X2100_CALIB_REPEATS) {
            // printf("[PhaseCalib] chirp[%02d] hdr: idx=%u tx=%u beam=%u "
            //        "ddma=%u/%u/%u/%u\n",
            //        chirp, header.ChirpInfo_1.Chirp_Index,
            //        header.ChirpInfo_1.TX_ACTIVE,
            //        header.ChirpInfo_1.Beam_Profile_Index,
            //        header.DDMAInfo_12.Tx_DDMA_1,
            //        header.DDMAInfo_12.Tx_DDMA_2,
            //        header.DDMAInfo_34.Tx_DDMA_1,
            //        header.DDMAInfo_34.Tx_DDMA_2);
        }
        src_offset += sizeof(CheetahChirpHeadInfo);
        raw = (const uint16_t *)(frame + src_offset);
        for (int sample = 0; sample < samples; sample++) {
            for (int rx = 0; rx < rxs; rx++) {
                size_t dst = ((size_t)chirp * samples + sample) * rxs + rx;
                size_t src = (size_t)sample * rxs + rx;
                adc[dst] = (int16_t)((raw[src] >> 4) - 2048);
            }
        }
        src_offset += (size_t)samples * rxs * sizeof(*raw) + CHIRP_END_BYTES;
    }

    printf("[PhaseCalib] %s ADC ready: %d chirps, %d samples, %d RX.\n",
           stage, chirps, samples, rxs);
    return adc;
}

typedef enum {
    PHASE_CALIB_ADC_FAILURE_OTHER = 0,
    PHASE_CALIB_ADC_FAILURE_TIMEOUT,
    PHASE_CALIB_ADC_FAILURE_RETRY_CONFIG,
} PhaseCalibAdcFailure;

static int16_t *collect_calibration_adc_retry(
    Cheetah_PhaseShifter_Calibration_Flow flow, CalibCfg_t *cfg,
    ParamsRegsConfig *regs, const char *stage,
    PhaseCalibAdcFailure *failure)
{
    int first_timed_out = 0;
    int retry_timed_out = 0;
    int16_t *adc;

    if (failure != NULL)
        *failure = PHASE_CALIB_ADC_FAILURE_OTHER;
    adc = collect_calibration_adc(cfg, stage, &first_timed_out);

    if (adc != NULL || !first_timed_out)
        return adc;

    /* A first-frame timeout leaves the local capture loop stopped and the
     * camera stream off.  Reapply this stage once; a second timeout remains
     * fatal so a hardware/configuration fault cannot loop indefinitely. */
    if (first_timed_out == 2)
        printf("[PhaseCalib] Retry %s once after capture failure.\n",
               stage);
    else
        printf("[PhaseCalib] Retry %s once after first-frame timeout.\n",
               stage);
    if (calibProcessing(flow, cfg, regs) != 0) {
        printf("[PhaseCalib] Retry configuration failed for %s.\n", stage);
        if (failure != NULL)
            *failure = PHASE_CALIB_ADC_FAILURE_RETRY_CONFIG;
        return NULL;
    }
    adc = collect_calibration_adc(cfg, stage, &retry_timed_out);
    if (adc == NULL && retry_timed_out && failure != NULL)
        *failure = PHASE_CALIB_ADC_FAILURE_TIMEOUT;
    return adc;
}

ErrStart_t dataProcessing(Cheetah_PhaseShifter_Calibration_Flow *flow,
                          CalibCfg_t *cfg, int16_t *adc_data,
                          int *IQ_idx_table, int sfIdx)
{
    ErrStart_t result = {0};

    if (*flow == FIRST_CALIBRATION) {
        result = firstCalibrationProcess(cfg, adc_data, IQ_idx_table);
        if (!result.failed)
            *flow = SECOND_CALIBRATION;
    } else if (*flow == SECOND_CALIBRATION) {
        result = secondCalibrationProcess(cfg, adc_data, IQ_idx_table);
        if (!result.failed)
            *flow = CHECK_CONVERGENCE;
    } else {
        result = checkConvergenceProcess(cfg, adc_data, IQ_idx_table, sfIdx);
    }
    return result;
}

static void prepare_work_stage(CalibCfg_t *cfg, int chirps, int ddma_chirp,
                               double sample_rate_hz)
{
    cfg->num_subframe = 1;
    cfg->num_adc[0] = X2100_CALIB_ADC_SAMPLES;
    cfg->num_chirp[0] = chirps;
    cfg->ddma_num[0] = cfg->num_bands[0];
    cfg->ddma_chirp[0] = ddma_chirp;
    cfg->slope[0] = X2100_CHIRP_SLOPE_HZ_PER_S;
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_4T4R
    /* Coarse/fine scan tables retain FSTEP=200. CHECK uses the normal
     * four-TX table, whose FSTEP=171; match its actual quantized slope. */
    if (sample_rate_hz == X2100_WORK_SAMPLE_RATE_HZ)
        cfg->slope[0] = 16.69921875E12;
#endif
    cfg->Fs = sample_rate_hz;
}

static void format_calibration_reg_line(char *buf, size_t size, uint16_t addr,
                                        uint16_t words,
                                        const uint32_t *values)
{
    size_t offset = 0;
    int written;

    if (buf == NULL || size == 0 || values == NULL)
        return;

    written = snprintf(buf, size, "{0, 0x%04X, %u, {",
                       (unsigned int)addr, (unsigned int)words);
    if (written < 0 || (size_t)written >= size) {
        buf[size - 1] = '\0';
        return;
    }
    offset = (size_t)written;

    for (uint16_t i = 0; i < words; i++) {
        written = snprintf(buf + offset, size - offset, "%s0x%08X",
                           i == 0 ? "" : ", ", values[i]);
        if (written < 0 || (size_t)written >= size - offset) {
            buf[size - 1] = '\0';
            return;
        }
        offset += (size_t)written;
    }
    snprintf(buf + offset, size - offset, "}},");
}

static void print_calibration_record(const PhaseCalibFlashRecord *record)
{
    char profile_line[448];
    char ddma_line[640];

    format_calibration_reg_line(profile_line, sizeof(profile_line),
                                PHASE_CALIB_PROFILE_ADDR,
                                record->profile_words, record->profile);
    format_calibration_reg_line(ddma_line, sizeof(ddma_line),
                                PHASE_CALIB_DDMA_ADDR,
                                record->ddma_words, record->ddma);
    printf("[PhaseCalib] Configuration register replacements:\n%s\n%s\n",
           profile_line, ddma_line);
}

static void phase_calib_task(void *arg)
{
    CalibCfg_t *first = NULL;
    CalibCfg_t *second = NULL;
    CalibCfg_t *third = NULL;
    ParamsRegsConfig regs = {0};
    PhaseCalibFlashRecord best_record;
    int *iq_table = NULL;
    int have_acceptable_result = 0;
    int completed = 0;
    int entered = 0;
    int restore_ret = 0;
    PhaseCalibAdcFailure adc_failure = PHASE_CALIB_ADC_FAILURE_OTHER;
    Cheetah_PhaseShifter_Calibration_Flow flow = FIRST_CALIBRATION;
    char stage_name[96];

    (void)arg;
    printf("[PhaseCalib] =======================Start X2100 phase-shifter calibration=======================.\n");

    if (enter_local_ps_cal_mode() != 0) {
        printf("[PhaseCalib] Failed to enter calibration mode.\n");
        phase_calib_set_failure(PHASE_CALIB_FAILURE_START,
                                "ENTER_CALIBRATION_MODE_FAILED");
        goto done;
    }
    entered = 1;

    first = malloc(sizeof(*first));
    second = malloc(sizeof(*second));
    third = malloc(sizeof(*third));
    regs.regs = malloc(PHASE_CALIB_REG_CAPACITY * sizeof(regs.regs[0]));
    if (!first || !second || !third || !regs.regs) {
        printf("[PhaseCalib] Insufficient RAM.\n");
        phase_calib_set_failure(PHASE_CALIB_FAILURE_PREPARE,
                                "INSUFFICIENT_RAM");
        goto done;
    }

    memcpy(first, &s_phase_calib_template, sizeof(*first));
    if (prepare_phase_calib_topology(first) != 0) {
        printf("[PhaseCalib] Invalid TX/phase/band topology.\n");
        phase_calib_set_failure(PHASE_CALIB_FAILURE_PREPARE,
                                "INVALID_CALIBRATION_TOPOLOGY");
        goto done;
    }
    iq_table = calloc((size_t)first->numMMIC * first->N_TX_PER_MMIC *
                      first->MAX_NUM_BANDS, sizeof(*iq_table));
    if (iq_table == NULL) {
        printf("[PhaseCalib] Insufficient IQ table RAM.\n");
        phase_calib_set_failure(PHASE_CALIB_FAILURE_PREPARE,
                                "INSUFFICIENT_RAM");
        goto done;
    }
    if (first->good_empty_dbc > first->max_empty_dbc) {
        printf("[PhaseCalib] Invalid thresholds: good %.2f dBc must be no "
               "greater than minimum %.2f dBc.\n",
               first->good_empty_dbc, first->max_empty_dbc);
        phase_calib_set_failure(PHASE_CALIB_FAILURE_PREPARE,
                                "INVALID_CALIBRATION_THRESHOLDS");
        goto done;
    }
    printf("[PhaseCalib] Step 1/3 start: 64-phase coarse scan "
           "(one-time, %dTX scan/%d valid, mask=0x%02x).\n",
           first->N_TX, first->num_calib_tx,
           (unsigned int)first->calib_tx_mask);
    if (calibProcessing(FIRST_CALIBRATION, first, &regs) != 0) {
        printf("[PhaseCalib] step1 configuration failed.\n");
        phase_calib_set_failure(PHASE_CALIB_FAILURE_STEP1,
                                "STEP1_CONFIGURATION_FAILED");
        goto done;
    }
    snprintf(stage_name, sizeof(stage_name),
             "step1/64phase/%dTX-scan/%dvalid",
             first->N_TX, first->num_calib_tx);
    int16_t *adc = collect_calibration_adc_retry(
        FIRST_CALIBRATION, first, &regs, stage_name, &adc_failure);
    if (adc == NULL) {
        if (adc_failure == PHASE_CALIB_ADC_FAILURE_RETRY_CONFIG)
            phase_calib_set_failure(PHASE_CALIB_FAILURE_STEP1,
                                    "STEP1_RETRY_CONFIGURATION_FAILED");
        else
            phase_calib_set_failure(
                PHASE_CALIB_FAILURE_STEP1,
                adc_failure == PHASE_CALIB_ADC_FAILURE_TIMEOUT ?
                "STEP1_ADC_TIMEOUT" : "STEP1_ADC_ACQUISITION_FAILED");
        goto done;
    }
    ErrStart_t result = dataProcessing(&flow, first, adc, iq_table, 0);
    free(adc);
    if (result.failed) {
        printf("[PhaseCalib] step1 processing failed: rms=%.4f, avg=%.4f, max=%.4f.\n",
               result.rms, result.avg_err, result.max_abs_err);
        phase_calib_set_failure(PHASE_CALIB_FAILURE_STEP1,
                                "STEP1_PROCESSING_FAILED");
        goto done;
    }
    memcpy(second, first, sizeof(*second));

    for (int iteration = 0; iteration < second->MAX_ITERATION; iteration++) {
        /* Keep the generated phase/DDMA tables.  Fine-scan chirps follow
         * num_bands * physical TX scan slots * repeat count. */
        int second_chirps = second->num_bands[0] * second->N_TX *
                            X2100_CALIB_REPEATS;
        printf("[PhaseCalib] Iteration %d/%d, step 2/3 start: "
               "%d-band fine scan (%dTX scan/%d valid).\n",
               iteration + 1, second->MAX_ITERATION,
               second->num_bands[0], second->N_TX,
               second->num_calib_tx);
        prepare_work_stage(second, second_chirps,
                           second->N_TX,
                           X2100_CALIB_SAMPLE_RATE_HZ);
        flow = SECOND_CALIBRATION;
        if (calibProcessing(SECOND_CALIBRATION, second, &regs) != 0) {
            printf("[PhaseCalib] step2 configuration failed at iteration %d.\n",
                   iteration + 1);
            phase_calib_set_failure(PHASE_CALIB_FAILURE_STEP2,
                                    "STEP2_CONFIGURATION_FAILED_ITERATION_%d",
                                    iteration + 1);
            goto done;
        }
        adc_failure = PHASE_CALIB_ADC_FAILURE_OTHER;
        snprintf(stage_name, sizeof(stage_name),
                 "step2/%dband/%dTX-scan/%dvalid",
                 second->num_bands[0], second->N_TX,
                 second->num_calib_tx);
        adc = collect_calibration_adc_retry(
            SECOND_CALIBRATION, second, &regs, stage_name, &adc_failure);
        if (adc == NULL) {
            if (adc_failure == PHASE_CALIB_ADC_FAILURE_RETRY_CONFIG)
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP2,
                    "STEP2_RETRY_CONFIGURATION_FAILED_ITERATION_%d",
                    iteration + 1);
            else
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP2,
                    adc_failure == PHASE_CALIB_ADC_FAILURE_TIMEOUT ?
                    "STEP2_ADC_TIMEOUT" :
                    "STEP2_ADC_ACQUISITION_FAILED");
            goto done;
        }
        result = dataProcessing(&flow, second, adc, iq_table, 0);
        free(adc);
        if (result.failed) {
            printf("[PhaseCalib] step2 processing failed at iteration %d: "
                   "rms=%.4f, avg=%.4f, max=%.4f.\n",
                   iteration + 1, result.rms, result.avg_err,
                   result.max_abs_err);
            if (result.failure_reason ==
                CALIB_PROCESS_FAILURE_MAG_SPREAD) {
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP2_MAG_SPREAD,
                    "STEP2_TX%d_MAG_SPREAD_%.2f_DB_EXCEEDS_%.2f_DB",
                    result.failure_tx, result.failure_value,
                    result.failure_limit);
            } else {
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP2,
                    "STEP2_PROCESSING_FAILED_ITERATION_%d", iteration + 1);
            }
            goto done;
        }
        memcpy(third, second, sizeof(*third));
        prepare_work_stage(third, X2100_WORK_CHIRPS, 1,
                           X2100_WORK_SAMPLE_RATE_HZ);
        printf("[PhaseCalib] Iteration %d/%d, step 3/3 start: "
               "convergence check (normal waveform, DDMA on/BPM off).\n",
               iteration + 1, second->MAX_ITERATION);
        if (calibProcessing(CHECK_CONVERGENCE, third, &regs) != 0) {
            printf("[PhaseCalib] step3 configuration failed at iteration %d.\n",
                   iteration + 1);
            phase_calib_set_failure(PHASE_CALIB_FAILURE_STEP3,
                                    "STEP3_CONFIGURATION_FAILED_ITERATION_%d",
                                    iteration + 1);
            goto done;
        }
        adc_failure = PHASE_CALIB_ADC_FAILURE_OTHER;
        snprintf(stage_name, sizeof(stage_name),
                 "step3/normal/tx-mask-0x%02x",
                 (unsigned int)third->calib_tx_mask);
        adc = collect_calibration_adc_retry(
            CHECK_CONVERGENCE, third, &regs, stage_name, &adc_failure);
        if (adc == NULL) {
            if (adc_failure == PHASE_CALIB_ADC_FAILURE_RETRY_CONFIG)
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP3,
                    "STEP3_RETRY_CONFIGURATION_FAILED_ITERATION_%d",
                    iteration + 1);
            else
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP3,
                    adc_failure == PHASE_CALIB_ADC_FAILURE_TIMEOUT ?
                    "STEP3_ADC_TIMEOUT" :
                    "STEP3_ADC_ACQUISITION_FAILED");
            goto done;
        }
        result = checkConvergenceProcess(third, adc, iq_table, 0);
        free(adc);
        if (result.failed) {
            printf("[PhaseCalib] step3 processing failed at iteration %d.\n",
                   iteration + 1);
            phase_calib_set_failure(PHASE_CALIB_FAILURE_STEP3,
                                    "STEP3_PROCESSING_FAILED_ITERATION_%d",
                                    iteration + 1);
            goto done;
        }

        printf("[PhaseCalib] iteration=%d, empty-band=%.2f dBc, "
               "minimum=%.2f dBc, good=%.2f dBc.\n",
               iteration + 1, third->empty_band_power_dbc[0],
               third->max_empty_dbc, third->good_empty_dbc);

        if (third->empty_band_power_dbc[0] <=
            third->max_empty_dbc) {
            PhaseCalibFlashRecord candidate;

            if (phase_calib_record_from_regs(
                    &regs, third->empty_band_power_dbc[0],
                    (uint16_t)(iteration + 1), &candidate) != 0) {
                printf("[PhaseCalib] Failed to snapshot complete calibration "
                       "register lines at iteration %d.\n", iteration + 1);
                phase_calib_set_failure(
                    PHASE_CALIB_FAILURE_STEP3,
                    "RESULT_SNAPSHOT_FAILED_ITERATION_%d", iteration + 1);
                goto done;
            }
            if (!have_acceptable_result ||
                candidate.best_empty_band_dbc <
                best_record.best_empty_band_dbc) {
                memcpy(&best_record, &candidate, sizeof(best_record));
                have_acceptable_result = 1;
                printf("[PhaseCalib] iteration=%d is the best acceptable "
                       "result so far (%.2f dBc).\n",
                       iteration + 1, candidate.best_empty_band_dbc);
            }
            if (third->empty_band_power_dbc[0] <= third->good_empty_dbc) {
                printf("[PhaseCalib] Good-result threshold reached; stopping "
                       "after iteration %d.\n", iteration + 1);
                break;
            }
        }
    }

    if (!have_acceptable_result) {
        printf("[PhaseCalib] No iteration reached the minimum %.2f dBc; "
               "keeping the previous saved result.\n",
               second->max_empty_dbc);
        phase_calib_set_failure(PHASE_CALIB_FAILURE_NOT_CONVERGED,
                                "NOT_CONVERGED_AFTER_%d_ITERATIONS",
                                second->MAX_ITERATION);
        goto done;
    }

    printf("[PhaseCalib] Selected iteration %u, best_empty_band_dbc=%.2f "
           "dBc.\n", best_record.best_iteration,
           best_record.best_empty_band_dbc);
    print_calibration_record(&best_record);
    if (phase_calib_save_record(&best_record) != 0) {
        printf("[PhaseCalib] Failed to save calibration result.\n");
        phase_calib_set_failure(PHASE_CALIB_FAILURE_FLASH_SAVE,
                                "FLASH_SAVE_OR_VERIFY_FAILED");
        goto done;
    }
    completed = 1;

done:
    if (entered) {
        int stop_ret = radar_capture_host_stop();

        if (stop_ret != 0)
            printf("[PhaseCalib] Final capture stop failed: %d.\n", stop_ret);
    }
    if (iq_table)
        free(iq_table);
    if (regs.regs)
        free(regs.regs);
    if (first)
        free(first);
    if (second)
        free(second);
    if (third)
        free(third);

    if (entered) {
        restore_ret = exit_local_ps_cal_mode();
        if (restore_ret != 0)
            printf("[PhaseCalib] Failed to restore normal radar mode.\n");
    }
    if (completed && restore_ret == 0)
        printf("[PhaseCalib] Calibration completed and saved.\n");
    else if (completed)
        printf("[PhaseCalib] Calibration saved, but normal mode restore failed.\n");
    else
        printf("[PhaseCalib] Calibration attempt finished: FAILED "
               "(code=%d, reason=%s). No new result was saved.\n",
               s_phase_calib_failure_code, s_phase_calib_failure_reason);
    /* Query status describes the calibration result itself.  Restoring the
     * normal capture path is a separate operation: a restore error must be
     * reported, but must not downgrade a result already verified and saved. */
    s_phase_calib_last_status = completed ?
        PHASE_CALIB_RESULT_SUCCESS : PHASE_CALIB_RESULT_FAILED;
    if (completed)
        phase_calib_set_failure(PHASE_CALIB_FAILURE_NONE, "NONE");
    s_phase_calibrating = 0;
    vTaskDelete(NULL);
}

int phase_calib_start(void)
{
    if (s_phase_calibrating)
        return -1;
    if (g_ps_cal_mode || calib_adc_sem == NULL) {
        phase_calib_set_failure(
            PHASE_CALIB_FAILURE_START,
            g_ps_cal_mode ? "CALIBRATION_MODE_ALREADY_IN_USE" :
                            "CALIBRATION_ADC_SEMAPHORE_NOT_READY");
        s_phase_calib_last_status = PHASE_CALIB_RESULT_FAILED;
        return -1;
    }

    phase_calib_set_failure(PHASE_CALIB_FAILURE_NONE, "NONE");
    s_phase_calibrating = 1;
    s_phase_calib_last_status = PHASE_CALIB_RESULT_CALIBRATING;
    if (xTaskCreate(phase_calib_task, "phaseCalib", PHASE_CALIB_TASK_STACK,
                    NULL, PHASE_CALIB_TASK_PRIORITY, NULL) != pdPASS) {
        phase_calib_set_failure(PHASE_CALIB_FAILURE_START,
                                "CALIBRATION_TASK_CREATE_FAILED");
        s_phase_calib_last_status = PHASE_CALIB_RESULT_FAILED;
        s_phase_calibrating = 0;
        return -1;
    }
    return 0;
}

int phase_calib_is_running(void)
{
    return s_phase_calibrating != 0;
}

int phase_calib_get_result(PhaseCalibQueryResult *result)
{
    PhaseCalibFlashRecord record;
    int has_best_empty_band_dbc = 0;

    if (result == NULL)
        return -1;
    memset(result, 0, sizeof(*result));
    result->acceptance_threshold_dbc = s_phase_calib_template.max_empty_dbc;
    result->good_threshold_dbc = s_phase_calib_template.good_empty_dbc;

    if (s_phase_calibrating) {
        result->status = PHASE_CALIB_RESULT_CALIBRATING;
        return 0;
    }

    result->status = s_phase_calib_last_status;
    result->failure_code = s_phase_calib_failure_code;
    snprintf(result->failure_reason, sizeof(result->failure_reason), "%s",
             s_phase_calib_failure_reason);
    if (phase_calib_load_saved_record(
            &record, &has_best_empty_band_dbc) == 0) {
        result->has_saved_result = 1;
        result->has_best_empty_band_dbc = has_best_empty_band_dbc;
        result->best_iteration = record.best_iteration;
        result->profile_words = record.profile_words;
        result->ddma_words = record.ddma_words;
        result->best_empty_band_dbc = record.best_empty_band_dbc;
        memcpy(result->profile, record.profile, sizeof(result->profile));
        memcpy(result->ddma, record.ddma, sizeof(result->ddma));
        if (result->status == PHASE_CALIB_RESULT_NOT_CALIBRATED)
            result->status = PHASE_CALIB_RESULT_SUCCESS;
    }
    return 0;
}
