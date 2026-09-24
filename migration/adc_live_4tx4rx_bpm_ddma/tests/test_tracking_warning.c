#include "radar_tracking.h"
#include "radar_warning.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static struct motorcycle_detection detection(float x, float y,
                                              float vx, float vy)
{
    struct motorcycle_detection d;
    float range = sqrtf(x * x + y * y);
    memset(&d, 0, sizeof(d));
    d.x_output_m = x;
    d.y_output_m = y;
    d.range_m = range;
    d.velocity_mps = range > 0.01f ? (x * vx + y * vy) / range : 0.0f;
    d.motion_state = sqrtf(vx * vx + vy * vy) > 1.0f ? 2u : 0u;
    d.is_peak = 1u;
    return d;
}

static void stable_id_and_dropout_test(void)
{
    struct motorcycle_track tracks[RADAR_TRACKING_MAX_TRACKS];
    uint16_t count;
    uint8_t id;
    radar_tracking_reset();
    for (unsigned int frame = 0; frame < 12u; ++frame) {
        struct motorcycle_detection d = detection(-30.0f + 0.4f * frame,
                                                   2.0f, 4.0f, 0.0f);
        radar_tracking_step(&d, 1u, 0.1f);
    }
    count = radar_tracking_copy_tracks(tracks, RADAR_TRACKING_MAX_TRACKS);
    assert(count == 1u);
    id = tracks[0].track_id;
    assert(tracks[0].valid && tracks[0].vx_mps > 2.0f);
    radar_tracking_step(NULL, 0u, 0.1f);
    radar_tracking_step(NULL, 0u, 0.1f);
    count = radar_tracking_copy_tracks(tracks, RADAR_TRACKING_MAX_TRACKS);
    assert(count == 1u && tracks[0].track_id == id && tracks[0].misses == 2u);
    for (unsigned int i = 0; i < 4u; ++i)
        radar_tracking_step(NULL, 0u, 0.1f);
    assert(radar_tracking_copy_tracks(tracks, RADAR_TRACKING_MAX_TRACKS) == 0u);
}

static void crossing_test(void)
{
    struct motorcycle_track tracks[RADAR_TRACKING_MAX_TRACKS];
    uint8_t ids[2] = {0};
    radar_tracking_reset();
    for (unsigned int frame = 0; frame < 25u; ++frame) {
        struct motorcycle_detection d[2];
        d[0] = detection(-20.0f + 0.15f * frame,
                         -3.0f + 0.25f * frame, 1.5f, 2.5f);
        d[1] = detection(-20.0f + 0.15f * frame,
                          3.0f - 0.25f * frame, 1.5f, -2.5f);
        /* Reverse input order every frame to exercise global association. */
        if (frame & 1u) {
            struct motorcycle_detection tmp = d[0]; d[0] = d[1]; d[1] = tmp;
        }
        radar_tracking_step(d, 2u, 0.1f);
        if (frame == 5u) {
            assert(radar_tracking_copy_tracks(tracks, 2u) == 2u);
            ids[0] = tracks[0].track_id; ids[1] = tracks[1].track_id;
        }
    }
    assert(radar_tracking_copy_tracks(tracks, 2u) == 2u);
    assert(tracks[0].track_id == ids[0]);
    assert(tracks[1].track_id == ids[1]);
    assert(tracks[0].vy_mps * tracks[1].vy_mps < 0.0f);
}

static void warning_test(void)
{
    struct motorcycle_track tracks[2];
    struct motorcycle_warning warning;
    memset(tracks, 0, sizeof(tracks));
    tracks[0].valid = 1u; tracks[0].track_id = 7u;
    tracks[0].x_m = -6.0f; tracks[0].y_m = 2.0f;
    tracks[0].vx_mps = 2.0f;
    tracks[1].valid = 1u; tracks[1].track_id = 9u;
    tracks[1].x_m = -6.0f; tracks[1].y_m = 0.0f;
    tracks[1].vx_mps = 2.0f;
    radar_warning_reset();
    radar_warning_evaluate(tracks, 2u, &warning);
    assert(!warning.active[RADAR_WARNING_BSD_LEFT]);
    radar_warning_evaluate(tracks, 2u, &warning);
    assert(warning.active[RADAR_WARNING_BSD_LEFT]);
    assert(warning.active[RADAR_WARNING_AOA_LEFT]);
    assert(warning.active[RADAR_WARNING_LCA_LEFT]);
    assert(warning.active[RADAR_WARNING_RCW]);
    assert(warning.ids[RADAR_WARNING_BSD_LEFT][0] == 7u);
    assert(warning.ids[RADAR_WARNING_RCW][0] == 9u);
    assert(warning.ttc_min_s > 2.0f && warning.ttc_min_s < 3.1f);
    radar_warning_evaluate(NULL, 0u, &warning);
    radar_warning_evaluate(NULL, 0u, &warning);
    assert(warning.active[RADAR_WARNING_RCW]);
    radar_warning_evaluate(NULL, 0u, &warning);
    assert(!warning.active[RADAR_WARNING_RCW]);
}

int main(void)
{
    struct radar_tracking_stats stats;
    stable_id_and_dropout_test();
    radar_tracking_get_stats(&stats);
    assert(stats.created == 1u && stats.confirmed == 1u && stats.deleted == 1u);
    crossing_test();
    warning_test();
    puts("TRACKING_WARNING=PASS confirmation/stable-ID/dropout/crossing/BSD/AOA/LCA/RCW/TTC/debounce");
    return 0;
}
