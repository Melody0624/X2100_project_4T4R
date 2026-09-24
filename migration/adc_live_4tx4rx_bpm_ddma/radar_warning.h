#ifndef RADAR_WARNING_H
#define RADAR_WARNING_H

#include <stdint.h>
#include "radar_tracking.h"

#define RADAR_WARNING_MAX_IDS 32u

enum radar_warning_class {
    RADAR_WARNING_BSD_LEFT = 0,
    RADAR_WARNING_BSD_RIGHT,
    RADAR_WARNING_AOA_LEFT,
    RADAR_WARNING_AOA_RIGHT,
    RADAR_WARNING_LCA_LEFT,
    RADAR_WARNING_LCA_RIGHT,
    RADAR_WARNING_RCW,
    RADAR_WARNING_CLASS_COUNT
};

struct motorcycle_warning {
    uint8_t active[RADAR_WARNING_CLASS_COUNT];
    float ttc_min_s;
    uint8_t ids[RADAR_WARNING_CLASS_COUNT][RADAR_WARNING_MAX_IDS];
    uint8_t id_count[RADAR_WARNING_CLASS_COUNT];
    uint8_t lca_left_level;
    uint8_t lca_right_level;
};

struct radar_warning_config {
    float bsd_rear_max_m;
    float aoa_rear_max_m;
    float lca_rear_max_m;
    float rcw_rear_max_m;
    float side_inner_m;
    float side_outer_m;
    float rcw_half_width_m;
    float approach_min_mps;
    float lca_ttc_max_s;
    float rcw_ttc_max_s;
    uint8_t assert_frames;
    uint8_t clear_frames;
};

void radar_warning_reset(void);
const struct radar_warning_config *radar_warning_get_config(void);
void radar_warning_evaluate(const struct motorcycle_track *tracks,
                            uint16_t track_count,
                            struct motorcycle_warning *warning);

#endif
