#ifndef RADAR_4TX4RX_BPM_PROFILE_H
#define RADAR_4TX4RX_BPM_PROFILE_H

/* Software candidate for Cheetah4401M 4TX binary phase modulation.
 * Four consecutive chirps form one orthogonal code block. */
#define RADAR_NUM_TX                       4u
#define RADAR_NUM_RX                       4u
#define RADAR_NUM_VIRTUAL_ANTS             16u
#define RADAR_ADC_CHIRPS_PER_FRAME         128u
#define RADAR_BPM_CODE_LENGTH              4u
#define RADAR_BPM_SLOW_TIME_SAMPLES        \
    (RADAR_ADC_CHIRPS_PER_FRAME / RADAR_BPM_CODE_LENGTH)
#define RADAR_DOPPLER_FFT_SIZE             RADAR_BPM_SLOW_TIME_SAMPLES

#define RADAR_FRAME_PERIOD_US              50328ull
#define RADAR_CHIRP_PERIOD_SECONDS          26.0e-6f
#define RADAR_BPM_SLOW_TIME_PERIOD_SECONDS \
    (RADAR_CHIRP_PERIOD_SECONDS * (float)RADAR_BPM_CODE_LENGTH)
#define RADAR_CENTER_FREQUENCY_HZ           76.5e9f

/* Remains zero until Cheetah is programmed and verified with the same matrix. */
#define RADAR_FRONTEND_4TX_BPM_READY        0

int radar_4tx4rx_bpm_profile_validate(void);
void radar_4tx4rx_bpm_profile_log(void);
unsigned int radar_4tx4rx_bpm_virtual_index(unsigned int tx,
                                            unsigned int rx);
float radar_4tx4rx_bpm_demod_coefficient(unsigned int chirp,
                                         unsigned int tx,
                                         float legacy_scrambler);
int radar_4tx4rx_bpm_tx_phase_sign(unsigned int chirp,
                                   unsigned int tx,
                                   int legacy_scrambler_sign);
void radar_4tx4rx_bpm_get_calibration(unsigned int virtual_index,
                                      float *real,
                                      float *imag);

#endif
