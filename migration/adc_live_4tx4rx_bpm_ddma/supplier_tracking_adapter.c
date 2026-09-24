/* Experimental bridge from the 4TX detector to the supplier's 2TX-era
 * tracking and warning archives.  Keep the supplier ABI in this translation
 * unit; the RF/DSP and USB protocol do not depend on GlbCtx. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "radar_tracking.h"
#include "radar_warning.h"
#include "motor_cycle_demo/inc/radar_types.h"
#include "motor_cycle_demo/inc/memory_pool.h"
#include "motor_cycle_demo/inc/radar_functions.h"
#include "motor_cycle_demo/inc/algorithm_functions.h"
#include "motor_cycle_demo/inc/ego_vlc_estimation.h"

MemoryPool g_memoryPool[MAXPOOLNUM];
const float th_vcsStatic = 1.0f;
const float th_FOVEdge = 60.0f;
const float th_FOVEdge_bias = 60.0f;
const float th_rngEdge_near = 15.0f;
const float th_rngEdge_far = 50.0f;
const int th_maxAgeAsHeader_near = 5;
const int th_maxAgeAsHeader_far = 3;
const int thr_motionStatusCnt_staticTomove = 6;
const int thr_motionStatusCnt_moveTostop = 4;

static GlbCtx *supplier_ctx;
static struct radar_tracking_stats supplier_stats;
static unsigned int previous_valid[MAX_TRACKS];
static unsigned int previous_mature[MAX_TRACKS];
static unsigned int supplier_frame;
static int ego_was_valid = -1;

static int allocate_workspaces(MemoryPool *p)
{
#define ALLOC(field, count) do { \
    p->field = calloc((count), sizeof(*p->field)); \
    if (!p->field) return -1; \
} while (0)
    ALLOC(trk_measurement, MAX_DETS_FOR_TRK_UPDATE);
    ALLOC(trk_innovation, MAX_DETS_FOR_TRK_UPDATE);
    ALLOC(trk_kalman_gain, MAX_DETS_FOR_TRK_UPDATE);
    ALLOC(trk_validTrkID_list, MAX_TRACKS);
    ALLOC(vlc_azRad, MAX_DETECTIONS);
    ALLOC(vlc_cosVal, MAX_DETECTIONS);
    ALLOC(vlc_sinVal, MAX_DETECTIONS);
    ALLOC(vlc_inlierMask, MAX_DETECTIONS);
    ALLOC(ransac_sampleIndices, 4);
    ALLOC(ransac_tempModel, 2);
    ALLOC(ransac_tempInliers, MAX_DETECTIONS);
    ALLOC(ransac_perm, MAX_DETECTIONS);
    ALLOC(trkInit_clusterMark_vec, MAX_DETECTIONS);
    ALLOC(trkInit_detAzm_vec, MAX_DETECTIONS);
    ALLOC(trkInit_detVlc_vec, MAX_DETECTIONS);
    ALLOC(x_tcs_list, MAX_DETECTIONS);
    ALLOC(y_tcs_list, MAX_DETECTIONS);
    ALLOC(kalman_ZVec, MAX_DETS_FOR_TRK_UPDATE + 2);
    ALLOC(kalman_HMat, (MAX_DETS_FOR_TRK_UPDATE + 2) * NUM_TRACKER_STATES);
    ALLOC(kalman_RMat, (MAX_DETS_FOR_TRK_UPDATE + 2) * (MAX_DETS_FOR_TRK_UPDATE + 2));
    ALLOC(kalman_S, 49);
    ALLOC(kalman_Sinv, 49);
    ALLOC(kalman_K, 28);
    ALLOC(kalman_PH_T, 28);
    ALLOC(kalman_HP, 28);
    ALLOC(kalman_innovation, 7);
    ALLOC(kalman_KH, 16);
    ALLOC(kalman_I_KH, 16);
    ALLOC(kalman_tempP, 16);
    ALLOC(kalman_aug, 98);
    ALLOC(pca_pson_vec, 2 * MAX_ASSOC_DETS_RECORD);
    ALLOC(pca_cov, 4);
    ALLOC(assoc_x_tcs_list, MAX_DETECTIONS);
    ALLOC(assoc_y_tcs_list, MAX_DETECTIONS);
    ALLOC(cov_mean, 2);
    ALLOC(cov_diff, 2);
#undef ALLOC
    p->initialized = true;
    return 0;
}

void radar_tracking_reset(void)
{
    GlbCtx *ctx = supplier_ctx;
    memset(&supplier_stats, 0, sizeof(supplier_stats));
    memset(previous_valid, 0, sizeof(previous_valid));
    memset(previous_mature, 0, sizeof(previous_mature));
    supplier_frame = 0;
    ego_was_valid = -1;
    if (ctx) {
        memset(ctx, 0, sizeof(*ctx));
    } else {
        ctx = calloc(1, sizeof(*ctx));
        if (!ctx || allocate_workspaces(&g_memoryPool[0]) != 0) {
            supplier_ctx = NULL;
            printf("[SUPPLIER-TRACK] workspace allocation failed; no tracks\n");
            return;
        }
    }
    supplier_ctx = ctx;
    supplier_ctx->basic_params.numTxs = 4;
    supplier_ctx->basic_params.numRxs = 4;
    supplier_ctx->basic_params.numRXPerMMIC = 4;
    supplier_ctx->basic_params.minElemSpacing = 0.5f;
    supplier_ctx->wave_params.sampling_freq = 26665000.0f;
    supplier_ctx->wave_params.frame_period = 0.050328f;
    supplier_ctx->wave_params.numSubFrms = 1;
    supplier_ctx->wave_params.numChirps = 128;
    supplier_ctx->wave_params.adcLen = 506;
    supplier_ctx->wave_params.numEmptyBands = 4;
    supplier_ctx->wave_params.central_freq = 76.5e9f;
    supplier_ctx->wave_params.chirp_period = 26e-6f;
    supplier_ctx->wave_params.slope = 19.531e12f;
    supplier_ctx->wave_params.wavelength =
        LIGHT_SPEED / supplier_ctx->wave_params.central_freq;
    supplier_ctx->wave_params.rngRes =
        LIGHT_SPEED / (2.0f * supplier_ctx->wave_params.slope *
        (supplier_ctx->wave_params.adcLen /
         supplier_ctx->wave_params.sampling_freq));
    supplier_ctx->wave_params.dopRes = 0.588770330f;
    supplier_ctx->wave_params.maxUmAmbVlc =
        supplier_ctx->wave_params.dopRes * 64.0f;
    supplier_ctx->radarInfo.installAngComp_deg = 180.0f;
    supplier_ctx->radarInfo.longOffset = 0.5f;
    supplier_ctx->radarInfo.isDetVlcPreCompensated = true;
    supplier_ctx->trackIDManager.maxTrackID = MAX_TRACKS;
    for (int i = 0; i < MAX_TRACKS; ++i)
        supplier_ctx->trackIDManager.trackIDList[i] = i;
    printf("[SUPPLIER-TRACK] original tracking+warning libraries active; 4T4R interface experimental\n");
}

int radar_ego_estimate(const float *azimuth_deg, const float *raw_radial_mps,
                       unsigned int count, float *radar_vx_mps,
                       float *radar_vy_mps)
{
    EgoVlcResult result;
    int valid = 0;
    if (!radar_vx_mps || !radar_vy_mps) return 0;
    *radar_vx_mps = 0.0f;
    *radar_vy_mps = 0.0f;
    if (!supplier_ctx) return 0;
    if (azimuth_deg && raw_radial_mps && count >= 5u &&
        count <= MAX_DETECTIONS) {
        result = ego_vlc_estimation(azimuth_deg, raw_radial_mps,
                                    (int)count, 0);
        valid = result.isAvailable && result.numInliers >= 5 &&
                isfinite(result.vx_rcs);
    }
    if (valid) {
        /* Preserve the supplier's 180-degree mounting convention and EMA.
         * Its current no-IMU path applies longitudinal motion only. */
        supplier_ctx->egoVehInfo.egoSpeed =
            0.6f * result.vx_rcs + 0.4f * supplier_ctx->egoVehInfo.egoSpeed;
        *radar_vx_mps = supplier_ctx->egoVehInfo.egoSpeed;
    } else {
        supplier_ctx->egoVehInfo.egoSpeed = 0.0f;
    }
    supplier_ctx->egoVehInfo.egoSpeedLat = 0.0f;
    if (valid != ego_was_valid || supplier_frame % 100u == 0u) {
        printf("[EGO] valid=%d candidates=%u vx=%.3f m/s applied=%d; 4T4R velocity/angle unverified\n",
               valid, count, *radar_vx_mps, valid);
        ego_was_valid = valid;
    }
    return valid;
}

void radar_tracking_step(const struct motorcycle_detection *detections,
                         uint16_t detection_count, float dt_seconds)
{
    GlbCtx *ctx = supplier_ctx;
    if (!ctx) return;
    if (detection_count > MAX_DETECTIONS) detection_count = MAX_DETECTIONS;
    ctx->frmInfo.lastFrmID = ctx->frmInfo.frmID;
    ctx->frmInfo.frmID = (int)++supplier_frame;
    if (dt_seconds > 0.0f) ctx->wave_params.frame_period = dt_seconds;
    compute_ego_motion(ctx);
    ctx->detInfo.numDets = detection_count;
    ctx->detInfo.numStaticDets = 0;
    for (unsigned int i = 0; i < detection_count; ++i) {
        const struct motorcycle_detection *src = &detections[i];
        DetObj *dst = &ctx->detInfo.detObj[i];
        memset(dst, 0, sizeof(*dst));
        dst->relRDIdx = src->rel_rd_index;
        dst->pwr = src->power;
        dst->snr = src->snr;
        dst->isPeak = src->is_peak != 0;
        dst->rng = src->range_m;
        dst->vlc = src->velocity_mps;
        dst->vlc_amb = src->velocity_ambiguous_mps;
        dst->vlc_disAmb_conf = src->velocity_disamb_confidence;
        dst->vlc_disAmb_fac = src->velocity_disamb_factor;
        dst->azm_deg = src->azimuth_deg;
        dst->x_rcs = src->x_rcs_m;
        dst->y_rcs = src->y_rcs_m;
        dst->x_output = src->x_output_m;
        dst->y_output = src->y_output_m;
        dst->motion_state = src->motion_state;
        if (dst->motion_state == MOTION_STATUS_STATIC)
            ++ctx->detInfo.numStaticDets;
    }
    /* Supplier tracking_processing also calls handle_warnings. */
    if (tracking_processing(ctx, 0) != 0) {
        memset(&ctx->trkInfo, 0, sizeof(ctx->trkInfo));
        memset(&ctx->warnResult, 0, sizeof(ctx->warnResult));
        memset(previous_valid, 0, sizeof(previous_valid));
        memset(previous_mature, 0, sizeof(previous_mature));
        return;
    }
    for (int i = 0; i < MAX_TRACKS; ++i) {
        const TrkObj *t = &ctx->trkInfo.trkObj[i];
        if (t->isvalid && !previous_valid[i]) ++supplier_stats.created;
        if (!t->isvalid && previous_valid[i]) ++supplier_stats.deleted;
        if (t->isvalid && t->track_state == TRACK_STATE_MATURE &&
            !previous_mature[i]) ++supplier_stats.confirmed;
        supplier_stats.associations += t->numAssocDets;
        previous_valid[i] = t->isvalid;
        previous_mature[i] = t->isvalid && t->track_state == TRACK_STATE_MATURE;
    }
}

uint16_t radar_tracking_copy_tracks(struct motorcycle_track *tracks,
                                    uint16_t capacity)
{
    unsigned int n = 0;
    if (!supplier_ctx || !tracks) return 0;
    for (int i = 0; i < MAX_TRACKS && n < capacity; ++i) {
        const TrkObj *src = &supplier_ctx->trkInfo.trkObj[i];
        struct motorcycle_track *dst;
        /* Match the supplier packetizer: only mature, valid tracks are sent. */
        if (!src->isvalid || src->track_state != TRACK_STATE_MATURE) continue;
        dst = &tracks[n++];
        memset(dst, 0, sizeof(*dst));
        dst->track_id = src->trkID;
        dst->motion_state = src->motion_state;
        dst->valid = 1u;
        dst->x_m = src->x_output;
        dst->y_m = src->y_output;
        dst->vx_mps = src->vx_output;
        dst->vy_mps = src->vy_output;
        /* The original packetizer encodes heading_output as degrees even
         * though the type comment calls it radians; preserve wire behavior. */
        dst->heading_deg = src->heading_output;
        dst->length_m = src->maxLen_tcs;
        dst->width_m = src->maxWid_tcs;
        dst->age = src->age;
        dst->misses = src->updateMissCnt;
    }
    return n;
}

void radar_tracking_get_stats(struct radar_tracking_stats *stats)
{
    if (stats) *stats = supplier_stats;
}

void radar_warning_reset(void) { }

const struct radar_warning_config *radar_warning_get_config(void)
{
    return NULL;
}

void radar_warning_evaluate(const struct motorcycle_track *tracks,
                            uint16_t track_count,
                            struct motorcycle_warning *warning)
{
    const WarnResult *src;
    (void)tracks;
    (void)track_count;
    if (!warning) return;
    memset(warning, 0, sizeof(*warning));
    if (!supplier_ctx) return;
    src = &supplier_ctx->warnResult;
#define COPY_WARNING(idx, name) do { \
    warning->active[idx] = src->name; \
    warning->id_count[idx] = src->name##_cnt > RADAR_WARNING_MAX_IDS ? \
        RADAR_WARNING_MAX_IDS : src->name##_cnt; \
    memcpy(warning->ids[idx], src->name##_IDs, warning->id_count[idx]); \
} while (0)
    COPY_WARNING(RADAR_WARNING_BSD_LEFT, BSD_left);
    COPY_WARNING(RADAR_WARNING_BSD_RIGHT, BSD_right);
    COPY_WARNING(RADAR_WARNING_AOA_LEFT, AOA_left);
    COPY_WARNING(RADAR_WARNING_AOA_RIGHT, AOA_right);
    COPY_WARNING(RADAR_WARNING_LCA_LEFT, LCA_left);
    COPY_WARNING(RADAR_WARNING_LCA_RIGHT, LCA_right);
    COPY_WARNING(RADAR_WARNING_RCW, RCW);
#undef COPY_WARNING
    warning->ttc_min_s = isfinite(src->TTC_min) && src->TTC_min > 0.0f &&
        src->TTC_min < 999.0f ? src->TTC_min : 999.0f;
    warning->lca_left_level = src->LCA_left_level;
    warning->lca_right_level = src->LCA_right_level;
}
