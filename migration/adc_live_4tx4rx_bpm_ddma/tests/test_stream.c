#include "radar_pipeline.h"
#include "radar_synthetic.h"
#include "radar_4tx4rx_profile.h"
#include "radar_tracking.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *payload;
static struct radar_diagnostic_snapshot f;
static struct radar_stream_diagnostic s;
static struct motorcycle_detection out[RADAR_MAX_DETECTIONS];
extern unsigned int host_track_count;
static void check_error(enum radar_frame_error error)
{
    radar_diagnostics_get(&f, &s);
    assert(f.error == (unsigned int)error);
    assert(radar_pipeline_copy_detections(out, RADAR_MAX_DETECTIONS) == 0);
}
static void config_tests(void)
{
    struct radar_waveform w = *radar_waveform_get();
    assert(!radar_waveform_error(&w));
    w.sample_rate_hz = NAN; assert(radar_waveform_error(&w));
    w = *radar_waveform_get(); w.chirp_s = 9e-6f; assert(radar_waveform_error(&w));
    w = *radar_waveform_get(); w.frame_us = 1000; assert(radar_waveform_error(&w));
    w = *radar_waveform_get(); w.chirps = 512; assert(radar_waveform_error(&w));
    w = *radar_waveform_get(); w.bpm_reset_each_frame = 0; assert(radar_waveform_error(&w));
    w = *radar_waveform_get(); w.bpm_start = RADAR_BPM_LENGTH; assert(radar_waveform_error(&w));
    assert(radar_diagnostics_set_level(3) < 0);
    puts("CONFIG_GUARDS=PASS invalid scalars/layout/timing/BPM epoch");
}
static void diagnostic_tests(void)
{
    struct radar_synthetic_target t[RADAR_SYNTHETIC_MAX_TARGETS];
    struct motorcycle_detection baseline[RADAR_MAX_DETECTIONS];
    unsigned int count = radar_synthetic_scene(0, t), n;
    assert(radar_synthetic_frame(payload, RADAR_SYNTHETIC_BYTES, t, count, 2, 12345, 0) == 0);
    radar_diagnostics_reset_stream();
    assert(radar_diagnostics_set_level(0) == 0);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 1, 1000) == 0);
    n = radar_pipeline_copy_detections(baseline, RADAR_MAX_DETECTIONS);
    assert(n > 0);
    assert(radar_diagnostics_set_level(2) == 0);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 2, 1000 + RADAR_FRAME_PERIOD_US) == 0);
    assert(radar_pipeline_copy_detections(out, RADAR_MAX_DETECTIONS) == n);
    assert(memcmp(out, baseline, n * sizeof(*out)) == 0);
    radar_diagnostics_get(&f, &s);
    assert(f.input_interval_us == RADAR_FRAME_PERIOD_US && f.cell_count == RADAR_DIAGNOSTIC_CELLS);
    for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
        int min = 2047, max = -2048;
        int64_t sum = 0; uint64_t squares = 0;
        for (unsigned int c = 0; c < RADAR_CHIRPS; ++c)
            for (unsigned int i = 0; i < RADAR_ADC_SAMPLES; ++i) {
                size_t pos = c * RADAR_CHIRP_BYTES + RADAR_CHIRP_HEADER_BYTES +
                    (i * RADAR_NUM_RX + rx) * 2u;
                int v = ((payload[pos] | ((unsigned int)payload[pos + 1] << 8)) >> 4) - 2048;
                if (v < min) min = v;
                if (v > max) max = v;
                sum += v; squares += (uint64_t)((int64_t)v * v);
            }
        assert(f.rx[rx].samples == RADAR_CHIRPS * RADAR_ADC_SAMPLES);
        assert(f.rx[rx].minimum == min && f.rx[rx].maximum == max);
        assert(f.rx[rx].sum == sum && f.rx[rx].sum_squares == squares);
        assert(!f.rx[rx].clipped && !f.rx[rx].bad_low_bits);
    }
    for (unsigned int i = 1; i < f.cell_count; ++i)
        assert(f.cells[i - 1].energy >= f.cells[i].energy);
    radar_diagnostics_print();
    puts("DIAGNOSTICS=PASS bitwise output equivalence; per-RX independent reference; bounded DDMA snapshot");
}
static void metadata_tests(void)
{
    radar_diagnostics_reset_stream();
    radar_diagnostics_set_level(1);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, UINT32_MAX, 1000) == 0);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 0, 2000) == 0);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 0, 3000) < 0);
    check_error(RADAR_FRAME_SEQUENCE);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, UINT32_MAX, 3000) < 0);
    check_error(RADAR_FRAME_SEQUENCE);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 1, 1000) < 0);
    check_error(RADAR_FRAME_TIMESTAMP);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 2, 4000) == 0);
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES - 1, 3, 5000) < 0);
    check_error(RADAR_FRAME_LENGTH);
    payload[RADAR_CHIRP_HEADER_BYTES] |= 1u;
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 4, 6000) < 0);
    check_error(RADAR_FRAME_ADC);
    payload[RADAR_CHIRP_HEADER_BYTES] &= 0xf0u;
    assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES, 5, 7000) == 0);
    radar_diagnostics_get(&f, &s);
    assert(s.attempted == 9 && s.succeeded == 4 && s.failed == 5);
    assert(s.missing_ids == 1 && s.duplicate_ids == 1 && s.out_of_order_ids == 1 && s.timestamp_errors == 1);
    puts("STREAM_RECOVERY=PASS ID wrap/drop/duplicate/reorder/time regression/bad payload/recovery");
}
static void sequence_tests(void)
{
    struct radar_synthetic_target t[RADAR_SYNTHETIC_MAX_TARGETS];
    float worst_range = 0, worst_velocity = 0, worst_angle = 0;
    unsigned int targets_checked = 0, empty = 0;
    unsigned int tracking_checks = 0;
    radar_diagnostics_reset_stream();
    radar_tracking_reset();
    for (unsigned int frame = 0; frame < 150u; ++frame) {
        unsigned int count = radar_synthetic_scene(frame, t), n;
        radar_diagnostics_set_level(frame % 3u);
        assert(radar_synthetic_frame(payload, RADAR_SYNTHETIC_BYTES, t, count, 2, 70000 + frame, 0) == 0);
        assert(radar_pipeline_process_timed(payload, RADAR_SYNTHETIC_BYTES,
            frame, 1000000ull + frame * RADAR_FRAME_PERIOD_US) == 0);
        n = radar_pipeline_copy_detections(out, RADAR_MAX_DETECTIONS);
        if (!count) { assert(n == 0); ++empty; }
        unsigned int used = 0;
        for (unsigned int j = 0; j < count; ++j) {
            int matched = 0;
            for (unsigned int i = 0; i < n; ++i) {
                float dr = fabsf(out[i].range_m - t[j].range_bin * radar_range_bin_m());
                float dv = fabsf(out[i].velocity_mps - t[j].doppler_bin * radar_velocity_bin_mps());
                float da = fabsf(out[i].azimuth_deg - t[j].azimuth_deg);
                if (!(used & (1u << i)) && dr < 0.15f && dv < 0.36f && da < 1.0f) {
                    worst_range = fmaxf(worst_range, dr); worst_velocity = fmaxf(worst_velocity, dv);
                    worst_angle = fmaxf(worst_angle, da); used |= 1u << i; matched = 1; break;
                }
            }
            if (!matched) fprintf(stderr, "sequence missing frame=%u target=%u detections=%u r=%.4f v=%.4f a=%.2f\n",
                frame, j, n, t[j].range_bin * radar_range_bin_m(),
                t[j].doppler_bin * radar_velocity_bin_mps(), t[j].azimuth_deg);
            assert(matched); ++targets_checked;
        }
        if ((frame >= 3u && frame < 40u) ||
            (frame >= 54u && frame < 130u)) {
            assert(host_track_count > 0u);
            ++tracking_checks;
        }
        if ((frame >= 46u && frame < 50u) || frame >= 136u)
            assert(host_track_count == 0u);
    }
    radar_diagnostics_get(&f, &s);
    assert(s.attempted == 150 && s.succeeded == 150 && !s.failed && !s.missing_ids);
    assert(targets_checked == 200 && empty == 30);
    printf("CONTINUOUS_SCENE=PASS frames=150 target_checks=%u tracking_checks=%u empty_frames=%u worst_r=%.6f worst_v=%.6f worst_a=%.6f (synthetic integration)\n",
        targets_checked, tracking_checks, empty, worst_range, worst_velocity,
        worst_angle);
}
int main(void)
{
    payload = malloc(RADAR_SYNTHETIC_BYTES);
    assert(payload && radar_pipeline_init() == 0);
    config_tests(); diagnostic_tests(); metadata_tests(); sequence_tests();
    free(payload);
    return 0;
}
