#ifndef MOTORCYCLE_OUTPUT_H
#define MOTORCYCLE_OUTPUT_H

#include <stdint.h>

#define MOTORCYCLE_OUTPUT_MAX_DETECTIONS 16u

struct motorcycle_detection {
    uint16_t rel_rd_index;
    uint8_t motion_state;
    uint8_t is_peak;
    float velocity_mps;
    float x_output_m;
    float y_output_m;
    float range_m;
    float azimuth_deg;
    float velocity_ambiguous_mps;
    float x_rcs_m;
    float y_rcs_m;
    uint8_t velocity_disamb_confidence;
    uint8_t velocity_disamb_factor;
    float power;
    float snr;
};

struct motorcycle_output_stats {
    uint32_t produced;
    uint32_t sent;
    uint32_t dropped;
    uint32_t errors;
    uint32_t connected;
};

int motorcycle_output_init(void);
void motorcycle_output_publish(uint32_t frame_id,
                               const struct motorcycle_detection *detections,
                               uint16_t detection_count);
void motorcycle_output_get_stats(struct motorcycle_output_stats *stats);

#endif
