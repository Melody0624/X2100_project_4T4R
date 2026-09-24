#include <stdint.h>

#include "common.h"
#include "radar_4tx4rx_profile.h"

/* TX-major virtual-channel order.  Change this table when the PCB antenna
 * geometry and RF routing are known; algorithm code must not depend on it. */
static const uint8_t virtual_channel_map[RADAR_NUM_TX][RADAR_NUM_RX] = {
    { 0u,  1u,  2u,  3u},
    { 4u,  5u,  6u,  7u},
    { 8u,  9u, 10u, 11u},
    {12u, 13u, 14u, 15u},
};

/* Provisional DDMA allocation.  Eight total subbands leave four signal
 * subbands and four guard/empty subbands, matching the old 2TX profile's
 * signal-to-total-subband ratio.  Replace after the 4TX waveform is known. */
static const uint8_t tx_subband_offset[RADAR_NUM_TX] = {
    0u, 1u, 2u, 3u,
};

/* Identity values prevent unknown board calibration from corrupting data.
 * A production build must load 16 measured complex coefficients instead. */
static const float calibration_real[RADAR_NUM_VIRTUAL_ANTS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

static const float calibration_imag[RADAR_NUM_VIRTUAL_ANTS] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};

int radar_4tx4rx_profile_validate(void)
{
    uint32_t seen = 0u;

    if (RADAR_DOPPLER_FFT_SIZE % RADAR_DDMA_NUM_SUBBANDS != 0u)
        return -1;
    if (RADAR_NUM_VIRTUAL_ANTS > 32u)
        return -1;

    for (unsigned int tx = 0; tx < RADAR_NUM_TX; ++tx) {
        if (tx_subband_offset[tx] >= RADAR_DDMA_NUM_SUBBANDS)
            return -1;
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

void radar_4tx4rx_profile_log(void)
{
    printf("[4T4R] software profile: tx=%u rx=%u virtual=%u\n",
           RADAR_NUM_TX, RADAR_NUM_RX, RADAR_NUM_VIRTUAL_ANTS);
    printf("[4T4R] DDMA placeholder: subbands=%u bins/subband=%u offsets=0,1,2,3\n",
           RADAR_DDMA_NUM_SUBBANDS, RADAR_DDMA_BINS_PER_SUBBAND);
    printf("[4T4R] array placeholder: TX-major mapping, identity calibration, ULA angle axis\n");
    printf("[4T4R] timing/code placeholder: period=%llu us, legacy BPM sequence\n",
           (unsigned long long)RADAR_FRAME_PERIOD_US);
#if !RADAR_FRONTEND_4TX_PROFILE_READY
    printf("[4T4R] WARNING: RF registers still use legacy 2TX Cheetah profile\n");
#endif
}

unsigned int radar_4tx4rx_virtual_index(unsigned int tx,
                                        unsigned int rx)
{
    if (tx >= RADAR_NUM_TX || rx >= RADAR_NUM_RX)
        return 0u;
    return virtual_channel_map[tx][rx];
}

unsigned int radar_4tx4rx_tx_subband(unsigned int anchor_subband,
                                     unsigned int tx)
{
    if (tx >= RADAR_NUM_TX)
        return anchor_subband % RADAR_DDMA_NUM_SUBBANDS;
    return (anchor_subband + tx_subband_offset[tx]) %
           RADAR_DDMA_NUM_SUBBANDS;
}

void radar_4tx4rx_get_calibration(unsigned int virtual_index,
                                  float *real,
                                  float *imag)
{
    if (virtual_index >= RADAR_NUM_VIRTUAL_ANTS)
        virtual_index = 0u;
    *real = calibration_real[virtual_index];
    *imag = calibration_imag[virtual_index];
}

float radar_4tx4rx_chirp_code(unsigned int chirp, float legacy_bpm_code)
{
    (void)chirp;
    /* Hardware documentation must define the real 4TX code.  Passing the
     * legacy value through preserves the known-good capture path and keeps
     * the replacement point explicit. */
    return legacy_bpm_code;
}
