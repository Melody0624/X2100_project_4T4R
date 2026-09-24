#ifndef RADAR_SYNTHETIC_H
#define RADAR_SYNTHETIC_H
#include <stddef.h>
#include <stdint.h>
#include "radar_config.h"
#define RADAR_SYNTHETIC_BYTES RADAR_PAYLOAD_BYTES
#define RADAR_SYNTHETIC_MAX_TARGETS 4u

/* Range/Doppler bins refer to full 512/128 FFTs, not folded DDMA bins.
 * Geometry is an IDEAL half-wavelength 16-element ULA, NOT the unknown PCB. */
struct radar_synthetic_target {
    float range_bin;
    float doppler_bin;
    float azimuth_deg;
    float amplitude;
    float tx_gain[RADAR_NUM_TX];
    float tx_phase_error_deg[RADAR_NUM_TX];
};
int radar_synthetic_frame(unsigned char *payload, size_t length,
    const struct radar_synthetic_target *targets, unsigned int count,
    float noise_std, uint32_t seed, unsigned int bpm_offset);
/* 150-frame looping scene: receding target, empty interval, two crossing
 * ranges, empty interval. Kinematics use the configured frame period.
 * Each frame is coherent internally; inter-frame carrier phase is reset. */
unsigned int radar_synthetic_scene(uint32_t frame_index,
    struct radar_synthetic_target targets[RADAR_SYNTHETIC_MAX_TARGETS]);
#endif
