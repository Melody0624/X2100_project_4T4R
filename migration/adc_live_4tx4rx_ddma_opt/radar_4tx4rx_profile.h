#ifndef RADAR_4TX4RX_PROFILE_H
#define RADAR_4TX4RX_PROFILE_H

/*
 * Software-side 4TX4RX profile.
 *
 * The ADC transport still contains four physical RX channels and 128 chirps.
 * Four transmit channels are separated in Doppler/DDMA space, producing
 * 4 x 4 = 16 virtual antenna samples for angle processing.
 *
 * IMPORTANT: the actual Cheetah RF register table, TX coding, antenna
 * positions and per-channel calibration are not available yet.  The values
 * below are deliberately isolated so that the hardware profile can be
 * replaced without rewriting the signal-processing pipeline.
 */
#define RADAR_NUM_TX                    4u
#define RADAR_NUM_RX                    4u
#define RADAR_NUM_VIRTUAL_ANTS          (RADAR_NUM_TX * RADAR_NUM_RX)

#define RADAR_DOPPLER_FFT_SIZE          128u
#define RADAR_DDMA_NUM_SUBBANDS         8u
#define RADAR_DDMA_BINS_PER_SUBBAND     \
    (RADAR_DOPPLER_FFT_SIZE / RADAR_DDMA_NUM_SUBBANDS)
#define RADAR_PHASE_WORD_MODULUS         64u
#define RADAR_PHASE_WORD_180_DEG         32u
#define RADAR_PHASE_WORD_PER_SUBBAND     \
    (RADAR_PHASE_WORD_MODULUS / RADAR_DDMA_NUM_SUBBANDS)

/* Legacy Cheetah timing placeholders; replace with the 4TX waveform data. */
#define RADAR_FRAME_PERIOD_US            50328ull
#define RADAR_CHIRP_PERIOD_SECONDS       26.0e-6f
#define RADAR_CENTER_FREQUENCY_HZ        76.5e9f

/* Set to 1 only after a verified 4TX Cheetah register profile is installed. */
#define RADAR_FRONTEND_4TX_PROFILE_READY 0

int radar_4tx4rx_profile_validate(void);
void radar_4tx4rx_profile_log(void);
unsigned int radar_4tx4rx_virtual_index(unsigned int tx,
                                        unsigned int rx);
unsigned int radar_4tx4rx_tx_subband(unsigned int anchor_subband,
                                     unsigned int tx);
void radar_4tx4rx_get_calibration(unsigned int virtual_index,
                                  float *real,
                                  float *imag);
float radar_4tx4rx_chirp_code(unsigned int chirp, float legacy_bpm_code);
unsigned int radar_4tx4rx_tx_phase_word(unsigned int chirp,
                                        unsigned int tx,
                                        int legacy_bpm_sign);

#endif
