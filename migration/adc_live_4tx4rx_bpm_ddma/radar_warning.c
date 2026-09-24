#include "radar_warning.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* Engineering defaults for the rear-facing installation used by this branch:
 * vehicle-rear targets have x_output < 0 and vehicle-left has y_output > 0.
 * These limits are deliberately centralized; road-test tuning must not be
 * confused with verification of the still-unconfirmed 4TX/AoA configuration. */
static const struct radar_warning_config warning_config = {
    8.0f, 25.0f, 70.0f, 100.0f,
    0.5f, 4.5f, 1.75f,
    1.0f, 5.0f, 4.0f,
    2u, 3u
};

struct warning_latch {
    uint8_t active;
    uint8_t assert_count;
    uint8_t clear_count;
    uint8_t id_count;
    uint8_t ids[RADAR_WARNING_MAX_IDS];
    float ttc_s;
};

static struct warning_latch latches[RADAR_WARNING_CLASS_COUNT];

static float track_ttc(const struct motorcycle_track *track)
{
    float speed2 = track->vx_mps * track->vx_mps +
                   track->vy_mps * track->vy_mps;
    float dot;
    float ttc;
    if (speed2 < 0.01f)
        return FLT_MAX;
    dot = track->x_m * track->vx_mps + track->y_m * track->vy_mps;
    if (dot >= 0.0f)
        return FLT_MAX;
    ttc = -dot / speed2;
    return ttc >= 0.0f && ttc <= 20.0f ? ttc : FLT_MAX;
}

static void add_candidate(uint8_t ids[RADAR_WARNING_CLASS_COUNT]
                                     [RADAR_WARNING_MAX_IDS],
                          uint8_t counts[RADAR_WARNING_CLASS_COUNT],
                          float ttc[RADAR_WARNING_CLASS_COUNT],
                          enum radar_warning_class type,
                          uint8_t id, float value)
{
    uint8_t count = counts[type];
    if (count < RADAR_WARNING_MAX_IDS) {
        ids[type][count] = id;
        counts[type] = count + 1u;
    }
    if (value < ttc[type])
        ttc[type] = value;
}

void radar_warning_reset(void)
{
    memset(latches, 0, sizeof(latches));
    for (unsigned int i = 0; i < RADAR_WARNING_CLASS_COUNT; ++i)
        latches[i].ttc_s = FLT_MAX;
}

const struct radar_warning_config *radar_warning_get_config(void)
{
    return &warning_config;
}

void radar_warning_evaluate(const struct motorcycle_track *tracks,
                            uint16_t track_count,
                            struct motorcycle_warning *warning)
{
    uint8_t candidate_ids[RADAR_WARNING_CLASS_COUNT]
                         [RADAR_WARNING_MAX_IDS] = {{0}};
    uint8_t candidate_counts[RADAR_WARNING_CLASS_COUNT] = {0};
    float candidate_ttc[RADAR_WARNING_CLASS_COUNT];

    if (!warning)
        return;
    if (!tracks)
        track_count = 0u;
    for (unsigned int i = 0; i < RADAR_WARNING_CLASS_COUNT; ++i)
        candidate_ttc[i] = FLT_MAX;

    for (unsigned int i = 0; i < track_count; ++i) {
        const struct motorcycle_track *track = &tracks[i];
        float rear = -track->x_m;
        float lateral = fabsf(track->y_m);
        float ttc = track_ttc(track);
        float closing = track->vx_mps;
        int left = track->y_m >= 0.0f;
        if (!track->valid || rear < 0.5f)
            continue;
        if (lateral >= warning_config.side_inner_m &&
            lateral <= warning_config.side_outer_m) {
            enum radar_warning_class bsd = left ? RADAR_WARNING_BSD_LEFT :
                                                  RADAR_WARNING_BSD_RIGHT;
            enum radar_warning_class aoa = left ? RADAR_WARNING_AOA_LEFT :
                                                  RADAR_WARNING_AOA_RIGHT;
            enum radar_warning_class lca = left ? RADAR_WARNING_LCA_LEFT :
                                                  RADAR_WARNING_LCA_RIGHT;
            if (rear <= warning_config.bsd_rear_max_m)
                add_candidate(candidate_ids, candidate_counts, candidate_ttc,
                              bsd, track->track_id, ttc);
            if (rear <= warning_config.aoa_rear_max_m &&
                closing >= warning_config.approach_min_mps)
                add_candidate(candidate_ids, candidate_counts, candidate_ttc,
                              aoa, track->track_id, ttc);
            if (rear <= warning_config.lca_rear_max_m &&
                closing >= warning_config.approach_min_mps &&
                ttc <= warning_config.lca_ttc_max_s)
                add_candidate(candidate_ids, candidate_counts, candidate_ttc,
                              lca, track->track_id, ttc);
        }
        if (rear <= warning_config.rcw_rear_max_m &&
            lateral <= warning_config.rcw_half_width_m &&
            closing >= warning_config.approach_min_mps &&
            ttc <= warning_config.rcw_ttc_max_s)
            add_candidate(candidate_ids, candidate_counts, candidate_ttc,
                          RADAR_WARNING_RCW, track->track_id, ttc);
    }

    for (unsigned int type = 0; type < RADAR_WARNING_CLASS_COUNT; ++type) {
        struct warning_latch *latch = &latches[type];
        if (candidate_counts[type]) {
            latch->clear_count = 0u;
            if (latch->assert_count != UINT8_MAX)
                ++latch->assert_count;
            latch->id_count = candidate_counts[type];
            memcpy(latch->ids, candidate_ids[type], latch->id_count);
            latch->ttc_s = candidate_ttc[type];
            if (latch->assert_count >= warning_config.assert_frames)
                latch->active = 1u;
        } else {
            latch->assert_count = 0u;
            if (latch->clear_count != UINT8_MAX)
                ++latch->clear_count;
            if (latch->clear_count >= warning_config.clear_frames) {
                latch->active = 0u;
                latch->id_count = 0u;
                latch->ttc_s = FLT_MAX;
            }
        }
    }

    memset(warning, 0, sizeof(*warning));
    warning->ttc_min_s = FLT_MAX;
    for (unsigned int type = 0; type < RADAR_WARNING_CLASS_COUNT; ++type) {
        const struct warning_latch *latch = &latches[type];
        warning->active[type] = latch->active;
        if (!latch->active)
            continue;
        warning->id_count[type] = latch->id_count;
        memcpy(warning->ids[type], latch->ids, latch->id_count);
        if (latch->ttc_s < warning->ttc_min_s)
            warning->ttc_min_s = latch->ttc_s;
    }
    warning->lca_left_level = warning->active[RADAR_WARNING_LCA_LEFT] ?
        (latches[RADAR_WARNING_LCA_LEFT].ttc_s <= 2.0f ? 2u : 1u) : 0u;
    warning->lca_right_level = warning->active[RADAR_WARNING_LCA_RIGHT] ?
        (latches[RADAR_WARNING_LCA_RIGHT].ttc_s <= 2.0f ? 2u : 1u) : 0u;
    if (!isfinite(warning->ttc_min_s) || warning->ttc_min_s == FLT_MAX)
        warning->ttc_min_s = 999.0f;
}
