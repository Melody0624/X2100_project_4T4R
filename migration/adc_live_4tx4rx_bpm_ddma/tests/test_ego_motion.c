#include "radar_ego_motion.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.001f);
}

int main(void)
{
    /* With a rear-facing radar, static clutter has positive apparent speed. */
    near(radar_ego_correct_velocity(10.0f, 0.0f, 10.0f, 0.0f, 180.0f, 1), 0.0f);
    near(radar_ego_correct_velocity(14.0f, 0.0f, 10.0f, 0.0f, 180.0f, 1), 4.0f);
    near(radar_ego_correct_velocity(5.0f, 60.0f, 10.0f, 0.0f, 180.0f, 1), 0.0f);
    near(radar_ego_correct_velocity(14.0f, 0.0f, 10.0f, 0.0f, 180.0f, 0), 14.0f);
    puts("EGO_MOTION=PASS stationary/moving/angle/unavailable");
    return 0;
}
