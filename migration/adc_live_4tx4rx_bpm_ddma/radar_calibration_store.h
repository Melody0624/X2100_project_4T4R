#ifndef RADAR_CALIBRATION_STORE_H
#define RADAR_CALIBRATION_STORE_H

#define RADAR_CALIB_COMPLEX_FLOATS 32u
#define RADAR_CALIB_AXIS_FLOATS 128u

/* Return 1 for a valid saved block, 0 for erased, -1 for corrupt/read error. */
int radar_calibration_load(float complex_values[RADAR_CALIB_COMPLEX_FLOATS],
                           float angle_axis[RADAR_CALIB_AXIS_FLOATS],
                           unsigned int *flags);
/* flags: bit 0 = complex matrix, bit 1 = angle axis. */
int radar_calibration_save(const float *values, unsigned int flag);

#endif
