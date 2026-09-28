#ifndef RADAR_4TX4RX_PROFILE_H
#define RADAR_4TX4RX_PROFILE_H

/*
 * Software-side 4TX4RX profile.
 *
 * The ADC transport still contains four physical RX channels and 128 chirps.
 * Four transmit channels are separated in Doppler/DDMA space, producing
 * 4 x 4 = 16 virtual antenna samples for angle processing.
 *
 * The common 512-entry BPM sequence, frame start index 0 and per-frame reset
 * are confirmed.  The remaining open items are the per-TX DDMA/code meaning,
 * chip-channel-to-antenna routing and per-channel complex calibration.  The
 * values below are isolated so those hardware details can be replaced without
 * rewriting the signal-processing pipeline.
 */
#include "radar_config.h"

struct radar_waveform {
    float sample_rate_hz, slope_hz_per_s, center_hz, chirp_s;
    unsigned long long frame_us;
    unsigned int samples, chirps, range_fft, tx, rx, bands;
    unsigned int bpm_start, bpm_reset_each_frame;
};
const struct radar_waveform *radar_waveform_get(void);
/* Returns a stable error reason or NULL. Does not apply a new configuration. */
const char *radar_waveform_error(const struct radar_waveform *waveform);

int radar_4tx4rx_profile_validate(void);
/* Document coordinates in metres, TX+RX virtual phase centre. Channel IDs
 * are user-confirmed right-to-left on 2026-09-28. */
float radar_4tx4rx_virtual_position_m(unsigned int tx, unsigned int rx);
float radar_4tx4rx_spacing_wavelengths(void);
void radar_4tx4rx_profile_log(void);
unsigned int radar_4tx4rx_virtual_index(unsigned int tx,
                                        unsigned int rx);
unsigned int radar_4tx4rx_tx_subband(unsigned int anchor_subband,
                                     unsigned int tx);
void radar_4tx4rx_get_calibration(unsigned int virtual_index,
                                  float *real,
                                  float *imag);
void radar_4tx4rx_set_calibration(const float values[32]);
void radar_4tx4rx_get_angle_axis(float values[128]);
void radar_4tx4rx_set_angle_axis(const float values[128]);
int radar_4tx4rx_has_angle_axis_override(void);
float radar_4tx4rx_chirp_code(unsigned int chirp, float legacy_bpm_code);
unsigned int radar_4tx4rx_tx_phase_word(unsigned int chirp,
                                        unsigned int tx,
                                        int legacy_bpm_sign);

#endif
