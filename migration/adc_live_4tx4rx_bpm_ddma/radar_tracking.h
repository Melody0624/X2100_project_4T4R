#ifndef RADAR_TRACKING_H
#define RADAR_TRACKING_H

#include <stdint.h>
#include "motorcycle_output.h"

#define RADAR_TRACKING_MAX_TRACKS 16u

struct motorcycle_track {
    uint8_t track_id;
    uint8_t motion_state;
    uint8_t valid;
    uint8_t reserved;
    float x_m;
    float y_m;
    float vx_mps;
    float vy_mps;
    float heading_deg;
    float length_m;
    float width_m;
    uint16_t age;
    uint16_t misses;
};

struct radar_tracking_stats {
    uint32_t created;
    uint32_t confirmed;
    uint32_t deleted;
    uint32_t associations;
};

void radar_tracking_reset(void);
/* Returns 1 only when this frame has a usable radar-derived ego estimate. */
int radar_ego_estimate(const float *azimuth_deg, const float *raw_radial_mps,
                       unsigned int count, float *radar_vx_mps,
                       float *radar_vy_mps);
void radar_tracking_step(const struct motorcycle_detection *detections,
                         uint16_t detection_count, float dt_seconds);
uint16_t radar_tracking_copy_tracks(struct motorcycle_track *tracks,
                                    uint16_t capacity);
void radar_tracking_get_stats(struct radar_tracking_stats *stats);

#endif
