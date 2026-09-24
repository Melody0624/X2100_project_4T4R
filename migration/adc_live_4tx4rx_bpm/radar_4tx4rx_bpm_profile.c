#include <stdint.h>

#include "common.h"
#include "radar_4tx4rx_bpm_profile.h"

/* Rows are chirp slots and columns are TX channels.  H^T H = 4I.
 * +1 means 0 degrees and -1 means 180 degrees. */
static const int8_t bpm_walsh[RADAR_BPM_CODE_LENGTH][RADAR_NUM_TX] = {
    { 1,  1,  1,  1},
    { 1, -1,  1, -1},
    { 1,  1, -1, -1},
    { 1, -1, -1,  1},
};

static const uint8_t virtual_channel_map[RADAR_NUM_TX][RADAR_NUM_RX] = {
    { 0u,  1u,  2u,  3u},
    { 4u,  5u,  6u,  7u},
    { 8u,  9u, 10u, 11u},
    {12u, 13u, 14u, 15u},
};

static const float calibration_real[RADAR_NUM_VIRTUAL_ANTS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

static const float calibration_imag[RADAR_NUM_VIRTUAL_ANTS] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};

int radar_4tx4rx_bpm_profile_validate(void)
{
    uint32_t seen = 0u;

    if (RADAR_ADC_CHIRPS_PER_FRAME % RADAR_BPM_CODE_LENGTH != 0u)
        return -1;

    for (unsigned int a = 0; a < RADAR_NUM_TX; ++a) {
        for (unsigned int b = 0; b < RADAR_NUM_TX; ++b) {
            int dot = 0;
            for (unsigned int slot = 0;
                 slot < RADAR_BPM_CODE_LENGTH; ++slot) {
                dot += bpm_walsh[slot][a] * bpm_walsh[slot][b];
            }
            if (dot != (a == b ? (int)RADAR_BPM_CODE_LENGTH : 0))
                return -1;
        }
    }

    for (unsigned int tx = 0; tx < RADAR_NUM_TX; ++tx) {
        for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
            unsigned int index = virtual_channel_map[tx][rx];
            uint32_t bit;
            if (index >= RADAR_NUM_VIRTUAL_ANTS)
                return -1;
            bit = 1u << index;
            if ((seen & bit) != 0u)
                return -1;
            seen |= bit;
        }
    }
    return 0;
}

void radar_4tx4rx_bpm_profile_log(void)
{
    printf("[BPM4] software candidate: tx=4 rx=4 virtual=16 code_length=4\n");
    printf("[BPM4] Walsh rows: ++++  +-+-  ++--  +--+\n");
    printf("[BPM4] code=legacy 512-chip scrambler multiplied by Walsh sign\n");
    printf("[BPM4] slow_time=32 samples, Doppler FFT=32, interval=%.0f us\n",
           RADAR_BPM_SLOW_TIME_PERIOD_SECONDS * 1.0e6f);
    printf("[BPM4] array placeholder: TX-major map, identity calibration, ULA axis\n");
#if !RADAR_FRONTEND_4TX_BPM_READY
    printf("[BPM4] WARNING: Cheetah registers still use legacy 2TX profile\n");
    printf("[BPM4] WARNING: points are invalid until RF uses the same Walsh matrix\n");
#endif
}

unsigned int radar_4tx4rx_bpm_virtual_index(unsigned int tx,
                                            unsigned int rx)
{
    if (tx >= RADAR_NUM_TX || rx >= RADAR_NUM_RX)
        return 0u;
    return virtual_channel_map[tx][rx];
}

int radar_4tx4rx_bpm_tx_phase_sign(unsigned int chirp,
                                   unsigned int tx,
                                   int legacy_scrambler_sign)
{
    unsigned int slot = chirp % RADAR_BPM_CODE_LENGTH;
    if (tx >= RADAR_NUM_TX)
        return 0;
    return (legacy_scrambler_sign < 0 ? -1 : 1) * bpm_walsh[slot][tx];
}

float radar_4tx4rx_bpm_demod_coefficient(unsigned int chirp,
                                         unsigned int tx,
                                         float legacy_scrambler)
{
    int common_sign = legacy_scrambler < 0.0f ? -1 : 1;
    return (float)radar_4tx4rx_bpm_tx_phase_sign(
               chirp, tx, common_sign) /
           (float)RADAR_BPM_CODE_LENGTH;
}

void radar_4tx4rx_bpm_get_calibration(unsigned int virtual_index,
                                      float *real,
                                      float *imag)
{
    if (virtual_index >= RADAR_NUM_VIRTUAL_ANTS)
        virtual_index = 0u;
    *real = calibration_real[virtual_index];
    *imag = calibration_imag[virtual_index];
}
