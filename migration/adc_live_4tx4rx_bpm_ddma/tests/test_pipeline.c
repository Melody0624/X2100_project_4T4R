#include "radar_pipeline.h"
#include "radar_synthetic.h"
#include "radar_rf_profile.h"
#include "motorcycle_output.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct motorcycle_detection host_detections[MOTORCYCLE_OUTPUT_MAX_DETECTIONS];
extern unsigned int host_count;
static unsigned int cases;
static unsigned char *payload;
static float worst_v, worst_r, worst_a;
static struct radar_synthetic_target target(float r, float v, float a, float amp)
{
    struct radar_synthetic_target t = {0};
    t.range_bin = r; t.doppler_bin = v / radar_velocity_bin_mps();
    t.azimuth_deg = a; t.amplitude = amp;
    for (unsigned int i = 0; i < 4; ++i) t.tx_gain[i] = 1.0f - 0.1f * i;
    return t;
}
static void run(const struct radar_synthetic_target *targets, unsigned int count,
                float noise, unsigned int offset)
{
    assert(radar_synthetic_frame(payload, RADAR_SYNTHETIC_BYTES, targets, count,
                                noise, 0xDEADBEEFu + cases, offset) == 0);
    assert(radar_pipeline_process(payload, RADAR_SYNTHETIC_BYTES, ++cases) == 0);
}
static int match(const struct radar_synthetic_target *t, int required)
{
    float r = t->range_bin * radar_range_bin_m();
    float v = t->doppler_bin * radar_velocity_bin_mps();
    for (unsigned int i = 0; i < host_count; ++i) {
        const struct motorcycle_detection *d = &host_detections[i];
        float dr = fabsf(d->range_m - r), dv = fabsf(d->velocity_mps - v);
        float da = fabsf(d->azimuth_deg - t->azimuth_deg);
        if (dr < 0.15f && dv < 0.36f && da < 1.0f) {
            worst_r = fmaxf(worst_r, dr); worst_v = fmaxf(worst_v, dv);
            worst_a = fmaxf(worst_a, da);
            return 1;
        }
    }
    if (required) {
        fprintf(stderr, "Missing target case=%u r=%.3f v=%.3f a=%.3f count=%u\n",
                cases, r, v, t->azimuth_deg, host_count);
        for (unsigned int i = 0; i < host_count; ++i)
            fprintf(stderr, " out r=%.3f v=%.3f a=%.3f\n", host_detections[i].range_m,
                    host_detections[i].velocity_mps, host_detections[i].azimuth_deg);
        struct radar_pipeline_stats stats; radar_pipeline_get_stats(&stats);
        fprintf(stderr, " rejected=%u ambiguous=%u contaminated=%u\n", stats.rejected_peaks,
                stats.status_cells[DDMA_AMBIGUOUS], stats.status_cells[DDMA_CONTAMINATED]);
        abort();
    }
    return 0;
}

int main(void)
{
    payload = malloc(RADAR_SYNTHETIC_BYTES);
    assert(payload && radar_pipeline_init() == 0);
    assert(!radar_rf_profile_ready() && radar_rf_apply_verified_profile() < 0);
    assert(fabsf(radar_range_bin_m() - 0.3997034108f) < 1.0e-7f);
    assert(fabsf(radar_velocity_bin_mps() - 0.5887703816f) < 1.0e-6f);
    /* Full ADC/C chain: all are nontrivial velocities, not just FFT-bin centres. */
    for (int k = -50; k <= 50; ++k) {
        struct radar_synthetic_target t = target(50.25f, k * 0.5f, 15.0f, 100.0f);
        run(&t, 1, 2.0f, 0); match(&t, 1);
        /* Extra detections near this range with a band-shifted velocity are forbidden. */
        for (unsigned int i = 0; i < host_count; ++i)
            if (fabsf(host_detections[i].range_m - t.range_bin * radar_range_bin_m()) < 0.5f)
                assert(fabsf(host_detections[i].velocity_mps - k * 0.5f) < 1.0f);
    }
    for (int a = -30; a <= 30; a += 15) {
        struct radar_synthetic_target t = target(110.4f, -18.84f, (float)a, 100.0f);
        run(&t, 1, 3.0f, 0); match(&t, 1);
    }
    /* Dense checks across folded-bin boundaries, including signed velocity. */
    for (int boundary = -2; boundary <= 2; ++boundary) {
        for (int step = -5; step <= 5; ++step) {
            float velocity = boundary * 16.0f * radar_velocity_bin_mps() + step * 0.02f;
            struct radar_synthetic_target t = target(90.3f, velocity, -15, 80);
            run(&t, 1, 2.0f, 0); match(&t, 1);
        }
    }
    {
        /* Two velocities at one range but distinct folded bins remain separate. */
        struct radar_synthetic_target t[2] = {
            target(70.2f, 2.0f, 0.0f, 100.0f), target(70.2f, 5.0f, 0.0f, 60.0f)};
        run(t, 2, 1.0f, 0); match(&t[0], 1); match(&t[1], 1);
    }
    {
        /* Previously silently kept one speed. Same folded bin, overlapping TX
         * replicas: reject rather than claim this is uniquely separable. */
        struct radar_synthetic_target t[2] = {
            target(70.0f, 7.0f * radar_velocity_bin_mps(), 0, 100),
            target(70.0f, 23.0f * radar_velocity_bin_mps(), 0, 100)};
        run(t, 2, 0, 0);
        struct radar_pipeline_stats s; radar_pipeline_get_stats(&s);
        assert(s.rejected_peaks > 0);
        for (unsigned int i = 0; i < host_count; ++i)
            assert(fabsf(host_detections[i].range_m - 70 * radar_range_bin_m()) > 0.5f);
        puts("FULL_ADC_OVERLAP=REJECTED_AS_AMBIGUOUS (not claimed resolved)");
    }
    {
        struct radar_synthetic_target t[2] = {
            target(40.0f, 25.0f, -15, 100), target(140.0f, -25.0f, 30, 10)};
        run(t, 2, 1.0f, 0); match(&t[0], 1); match(&t[1], 1);
    }
    {
        /* A wrong common-BPM epoch must not count as a successful target test. */
        struct radar_synthetic_target t = target(50.25f, 25, 15, 100);
        run(&t, 1, 2, 1);
        assert(!match(&t, 0));
        puts("WRONG_COMMON_BPM_EPOCH=NOT_ACCEPTED_AS_CORRECT_TARGET");
    }
    {
        struct radar_synthetic_target t = target(50.25f, -10, 15, 100);
        t.tx_gain[2] = 0; /* Do not make a 4TX claim with a missing TX. */
        run(&t, 1, 2, 0); assert(!match(&t, 0));
        puts("MISSING_TX=NOT_ACCEPTED_AS_CORRECT_TARGET");
    }
    /* Reset/failure isolation and exact input size. */
    run(NULL, 0, 0, 0); assert(host_count == 0);
    for (unsigned int i = 0; i < 20; ++i) {
        run(NULL, 0, 5.0f, 0); assert(host_count == 0);
    }
    assert(radar_pipeline_process(NULL, RADAR_SYNTHETIC_BYTES, ++cases) < 0);
    assert(radar_pipeline_process(payload, RADAR_SYNTHETIC_BYTES - 1u, ++cases) < 0);
    assert(radar_pipeline_process(payload, RADAR_SYNTHETIC_BYTES + 1u, ++cases) < 0);
    payload[32] |= 1u;
    assert(radar_pipeline_process(payload, RADAR_SYNTHETIC_BYTES, ++cases) < 0);
    assert(host_count == 0);
    memset(payload, 0, RADAR_SYNTHETIC_BYTES);
    assert(radar_pipeline_process(payload, RADAR_SYNTHETIC_BYTES, ++cases) < 0);
    assert(host_count == 0);
    {
        struct radar_synthetic_target t = target(50.25f, 25, 15, 100);
        for (unsigned int n = 0; n < 30; ++n) { run(&t, 1, 2, 0); match(&t, 1); }
    }
    printf("FULL_C_PIPELINE=PASS cases=%u worst_velocity_error=%.6f m/s worst_range_error=%.6f m ideal_array_angle_error=%.6f deg\n",
           cases, worst_v, worst_r, worst_a);
    puts("Backend=portable host FFT; not an X2100 timing or RF acceptance test.");
    free(payload);
    return 0;
}
