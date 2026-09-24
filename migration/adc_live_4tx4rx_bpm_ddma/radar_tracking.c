#include "radar_tracking.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define TRACK_CONFIRM_HITS 3u
#define TRACK_TENTATIVE_MAX_MISSES 1u
#define TRACK_CONFIRMED_MAX_MISSES 5u
#define TRACK_ALPHA 0.65f
#define TRACK_BETA 0.10f
#define TRACK_RADIAL_GAIN 0.30f
#define TRACK_MIN_GATE_M 1.50f
#define TRACK_VELOCITY_GATE_MPS 4.0f
#define TRACK_PI 3.14159265358979323846f

struct track_state {
    uint8_t active;
    uint8_t confirmed;
    uint8_t id;
    uint8_t motion_state;
    uint16_t age;
    uint16_t misses;
    uint16_t hit_streak;
    float x;
    float y;
    float vx;
    float vy;
    float length;
    float width;
};

static struct track_state states[RADAR_TRACKING_MAX_TRACKS];
static struct radar_tracking_stats tracking_stats;
static uint8_t next_track_id = 1u;

/* The standalone tracker has no supplier RANSAC library. Keep existing
 * selftest and portable-host output explicitly uncompensated. */
int radar_ego_estimate(const float *azimuth_deg, const float *raw_radial_mps,
                       unsigned int count, float *radar_vx_mps,
                       float *radar_vy_mps)
{
    (void)azimuth_deg;
    (void)raw_radial_mps;
    (void)count;
    if (radar_vx_mps) *radar_vx_mps = 0.0f;
    if (radar_vy_mps) *radar_vy_mps = 0.0f;
    return 0;
}

static float square(float value) { return value * value; }

static float clamp_dt(float dt)
{
    if (!isfinite(dt) || dt < 0.02f)
        return 0.02f;
    if (dt > 0.20f)
        return 0.20f;
    return dt;
}

static float radial_velocity(const struct track_state *track)
{
    float range = sqrtf(square(track->x) + square(track->y));
    if (range < 0.05f)
        return 0.0f;
    return (track->x * track->vx + track->y * track->vy) / range;
}

static uint8_t allocate_id(void)
{
    for (unsigned int attempt = 0; attempt < 254u; ++attempt) {
        uint8_t candidate = next_track_id++;
        int used = 0;
        if (next_track_id == 0u || next_track_id == 255u)
            next_track_id = 1u;
        for (unsigned int i = 0; i < RADAR_TRACKING_MAX_TRACKS; ++i)
            used |= states[i].active && states[i].id == candidate;
        if (!used)
            return candidate;
    }
    return 0u;
}

static void start_track(struct track_state *track,
                        const struct motorcycle_detection *detection)
{
    float range = sqrtf(square(detection->x_output_m) +
                        square(detection->y_output_m));
    memset(track, 0, sizeof(*track));
    track->active = 1u;
    track->id = allocate_id();
    track->age = 1u;
    track->hit_streak = 1u;
    track->x = detection->x_output_m;
    track->y = detection->y_output_m;
    if (range > 0.05f) {
        track->vx = detection->velocity_mps * track->x / range;
        track->vy = detection->velocity_mps * track->y / range;
    }
    track->motion_state = detection->motion_state;
    track->length = 2.0f;
    track->width = 1.0f;
    ++tracking_stats.created;
}

void radar_tracking_reset(void)
{
    memset(states, 0, sizeof(states));
    memset(&tracking_stats, 0, sizeof(tracking_stats));
    next_track_id = 1u;
}

void radar_tracking_step(const struct motorcycle_detection *detections,
                         uint16_t detection_count, float dt_seconds)
{
    uint8_t track_used[RADAR_TRACKING_MAX_TRACKS] = {0};
    uint8_t detection_used[RADAR_MAX_DETECTIONS] = {0};
    float dt = clamp_dt(dt_seconds);

    if (detections == NULL)
        detection_count = 0u;
    if (detection_count > RADAR_MAX_DETECTIONS)
        detection_count = RADAR_MAX_DETECTIONS;

    for (unsigned int i = 0; i < RADAR_TRACKING_MAX_TRACKS; ++i) {
        if (!states[i].active)
            continue;
        states[i].x += states[i].vx * dt;
        states[i].y += states[i].vy * dt;
        if (states[i].age != UINT16_MAX)
            ++states[i].age;
    }

    /* Global greedy nearest-neighbour assignment. Re-selecting the smallest
     * remaining pair avoids dependence on detection order. */
    while (1) {
        float best_cost = FLT_MAX;
        int best_track = -1;
        int best_detection = -1;
        for (unsigned int ti = 0; ti < RADAR_TRACKING_MAX_TRACKS; ++ti) {
            const struct track_state *track = &states[ti];
            float gate;
            if (!track->active || track_used[ti])
                continue;
            gate = TRACK_MIN_GATE_M +
                0.05f * sqrtf(square(track->vx) + square(track->vy)) +
                0.50f * (float)track->misses;
            for (unsigned int di = 0; di < detection_count; ++di) {
                float dx, dy, distance2, velocity_error, cost;
                if (detection_used[di])
                    continue;
                dx = detections[di].x_output_m - track->x;
                dy = detections[di].y_output_m - track->y;
                distance2 = square(dx) + square(dy);
                if (distance2 > square(gate))
                    continue;
                velocity_error = fabsf(detections[di].velocity_mps -
                                       radial_velocity(track));
                if (velocity_error > TRACK_VELOCITY_GATE_MPS)
                    continue;
                cost = distance2 / square(gate) +
                       0.15f * square(velocity_error /
                                      TRACK_VELOCITY_GATE_MPS);
                if (cost < best_cost) {
                    best_cost = cost;
                    best_track = (int)ti;
                    best_detection = (int)di;
                }
            }
        }
        if (best_track < 0)
            break;
        track_used[best_track] = 1u;
        detection_used[best_detection] = 1u;
        {
            struct track_state *track = &states[best_track];
            const struct motorcycle_detection *d = &detections[best_detection];
            float rx = d->x_output_m - track->x;
            float ry = d->y_output_m - track->y;
            float range;
            float radial_error;
            track->x += TRACK_ALPHA * rx;
            track->y += TRACK_ALPHA * ry;
            track->vx += TRACK_BETA * rx / dt;
            track->vy += TRACK_BETA * ry / dt;
            range = sqrtf(square(track->x) + square(track->y));
            radial_error = d->velocity_mps - radial_velocity(track);
            if (range > 0.05f) {
                track->vx += TRACK_RADIAL_GAIN * radial_error * track->x / range;
                track->vy += TRACK_RADIAL_GAIN * radial_error * track->y / range;
            }
            track->misses = 0u;
            if (track->hit_streak != UINT16_MAX)
                ++track->hit_streak;
            track->motion_state =
                sqrtf(square(track->vx) + square(track->vy)) > 1.0f ? 2u :
                d->motion_state;
            if (!track->confirmed && track->hit_streak >= TRACK_CONFIRM_HITS) {
                track->confirmed = 1u;
                ++tracking_stats.confirmed;
            }
            ++tracking_stats.associations;
        }
    }

    for (unsigned int i = 0; i < RADAR_TRACKING_MAX_TRACKS; ++i) {
        struct track_state *track = &states[i];
        unsigned int max_misses;
        if (!track->active || track_used[i])
            continue;
        if (track->misses != UINT16_MAX)
            ++track->misses;
        track->hit_streak = 0u;
        max_misses = track->confirmed ? TRACK_CONFIRMED_MAX_MISSES :
                                       TRACK_TENTATIVE_MAX_MISSES;
        if (track->misses > max_misses) {
            memset(track, 0, sizeof(*track));
            ++tracking_stats.deleted;
        }
    }

    for (unsigned int di = 0; di < detection_count; ++di) {
        if (detection_used[di])
            continue;
        for (unsigned int ti = 0; ti < RADAR_TRACKING_MAX_TRACKS; ++ti) {
            if (!states[ti].active) {
                start_track(&states[ti], &detections[di]);
                break;
            }
        }
    }
}

uint16_t radar_tracking_copy_tracks(struct motorcycle_track *tracks,
                                    uint16_t capacity)
{
    uint16_t count = 0u;
    if (!tracks)
        return 0u;
    for (unsigned int i = 0; i < RADAR_TRACKING_MAX_TRACKS && count < capacity;
         ++i) {
        const struct track_state *source = &states[i];
        struct motorcycle_track *target;
        if (!source->active || !source->confirmed)
            continue;
        target = &tracks[count++];
        memset(target, 0, sizeof(*target));
        target->track_id = source->id;
        target->motion_state = source->motion_state;
        target->valid = 1u;
        target->x_m = source->x;
        target->y_m = source->y;
        target->vx_mps = source->vx;
        target->vy_mps = source->vy;
        target->heading_deg = atan2f(source->vy, source->vx) *
                              (180.0f / TRACK_PI);
        target->length_m = source->length;
        target->width_m = source->width;
        target->age = source->age;
        target->misses = source->misses;
    }
    return count;
}

void radar_tracking_get_stats(struct radar_tracking_stats *stats)
{
    if (stats)
        *stats = tracking_stats;
}
