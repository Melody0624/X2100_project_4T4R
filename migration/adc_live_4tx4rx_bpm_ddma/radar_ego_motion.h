#ifndef RADAR_EGO_MOTION_H
#define RADAR_EGO_MOTION_H

#include <math.h>

/* Project the radar's vehicle-frame motion onto a detection's line of sight.
 * Keep the raw ambiguous radial speed separately for diagnostics. */
static inline float radar_ego_correct_velocity(float raw_radial_mps,
                                                float azimuth_deg,
                                                float radar_vx_mps,
                                                float radar_vy_mps,
                                                float install_angle_deg,
                                                int estimate_valid)
{
    if (!estimate_valid || !isfinite(raw_radial_mps) ||
        !isfinite(azimuth_deg) || !isfinite(radar_vx_mps) ||
        !isfinite(radar_vy_mps))
        return raw_radial_mps;
    const float angle = (azimuth_deg + install_angle_deg) *
                        (3.14159265358979323846f / 180.0f);
    return raw_radial_mps + radar_vx_mps * cosf(angle) +
           radar_vy_mps * sinf(angle);
}

#endif
