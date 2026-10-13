#include <math.h>
#include <string.h>
#include "rcs_estimation.h"
#include "memory_pool.h"

static RcsFrame frames[MAXPOOLNUM];
#if BOARD_ANTENNA_CONFIG == BOARD_ANTENNA_2T4R
/* Provisional B 2T4R reference: Record_20261013_111210.dat, 113 frames.
 * User RCS=4.6 dBsm; radar-estimated range=1.18993322 m.
 * Offset=4.6-mean(pwr+40*log10(range))=-136.526320837 dB.
 * Distance is not independent ground truth; recheck at other distances.
 * Runtime reference/clear APIs override this until the next reboot.
 * Do not transfer this reference to another waveform, gain or TX count. */
static float calibration_offset_db = -136.526320837f;
static bool calibration_valid = true;
#else
static float calibration_offset_db;
static bool calibration_valid;
#endif

RcsEstimate rcs_calculate(float pwr_db, float slant_range_m)
{
    RcsEstimate result = {NAN, NAN, false, false};
    if (!isfinite(pwr_db) || !isfinite(slant_range_m) || slant_range_m <= 0.0f)
        return result;
    /* RDMap is 20*log10(sum of complex magnitudes). For a point target,
     * amplitude falls as 1/R^2, so its dB range correction is 40*log10(R).
     * The reference offset absorbs the fixed RF/FFT/channel gain. */
    result.relative_db = pwr_db + 40.0f * log10f(slant_range_m);
    if (!isfinite(result.relative_db))
        return result;
    result.valid = true;
    if (calibration_valid) {
        result.dbsm = result.relative_db + calibration_offset_db;
        result.calibrated = isfinite(result.dbsm);
        if (!result.calibrated)
            result.dbsm = NAN;
    }
    return result;
}

bool rcs_set_reference(float reference_dbsm, float reference_range_m,
                       float reference_pwr_db)
{
    if (!isfinite(reference_dbsm))
        return false;
    RcsEstimate reference = rcs_calculate(reference_pwr_db, reference_range_m);
    if (!reference.valid)
        return false;
    float offset = reference_dbsm - reference.relative_db;
    if (!isfinite(offset))
        return false;
    calibration_offset_db = offset;
    calibration_valid = true;
    /* Do not expose results computed using the previous calibration. */
    memset(frames, 0, sizeof(frames));
    return true;
}

void rcs_clear_calibration(void)
{
    calibration_offset_db = 0.0f;
    calibration_valid = false;
    memset(frames, 0, sizeof(frames));
}

void rcs_clear_frame(int pool_index)
{
    if (pool_index >= 0 && pool_index < MAXPOOLNUM)
        memset(&frames[pool_index], 0, sizeof(frames[pool_index]));
}

int rcs_process_frame(const GlbCtx *ctx, int pool_index)
{
    if (pool_index < 0 || pool_index >= MAXPOOLNUM)
        return -1;
    RcsFrame *frame = &frames[pool_index];
    /* Clear unused entries, including the tail of a shorter next frame. */
    rcs_clear_frame(pool_index);
    if (ctx == NULL || ctx->detInfo.numDets > MAX_DETECTIONS)
        return -1;
    frame->frame_id = ctx->frmInfo.frmID;
    for (unsigned int i = 0; i < ctx->detInfo.numDets; ++i) {
        const DetObj *point = &ctx->detInfo.detObj[i];
        float range_m = point->rng;
        if (ctx->radarInfo.isInHighAlt) {
            /* AoA stores sqrt(slant^2-height^2) in rng for high installation. */
            float height_sqr = ctx->radarInfo.mountingHeightSqr;
            if (!isfinite(height_sqr) || height_sqr < 0.0f ||
                !isfinite(range_m) || range_m < 0.0f)
                range_m = NAN;
            else
                range_m = sqrtf(range_m * range_m + height_sqr);
        }
        frame->points[i] = rcs_calculate(point->pwr, range_m);
    }
    frame->count = ctx->detInfo.numDets;
    return 0;
}

const RcsFrame *rcs_get_frame(int pool_index)
{
    return pool_index >= 0 && pool_index < MAXPOOLNUM ? &frames[pool_index] : NULL;
}
