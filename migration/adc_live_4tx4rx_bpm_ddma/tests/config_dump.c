#include "radar_config.h"
#include <stdio.h>
int main(void)
{
    const unsigned int offsets[RADAR_NUM_TX] = RADAR_TX_SUBBAND_OFFSETS;
    printf("{\"config_id\":\"%s\",\"samples\":%u,\"chirps\":%u,\"tx\":%u,\"rx\":%u,"
        "\"range_fft\":%u,\"ddma_bands\":%u,\"payload_bytes\":%u,"
        "\"sample_rate_hz\":%.9g,\"slope_hz_per_s\":%.9g,\"center_hz\":%.9g,"
        "\"chirp_period_s\":%.9g,\"frame_period_us\":%llu,\"bpm_start\":%u,"
        "\"bpm_reset_each_frame\":%u,\"diagnostics_default\":%u,\"selftest_sequence\":%u,"
        "\"tx_subband_offsets\":[%u,%u,%u,%u]}\n",
        RADAR_CONFIG_ID, RADAR_ADC_SAMPLES, RADAR_CHIRPS, RADAR_NUM_TX, RADAR_NUM_RX,
        RADAR_RANGE_FFT_SIZE, RADAR_DDMA_NUM_SUBBANDS, RADAR_PAYLOAD_BYTES,
        RADAR_ADC_SAMPLE_RATE_HZ, RADAR_CHIRP_SLOPE_HZ_PER_SECOND, RADAR_CENTER_FREQUENCY_HZ,
        RADAR_CHIRP_PERIOD_SECONDS, RADAR_FRAME_PERIOD_US, RADAR_BPM_START,
        RADAR_BPM_RESET_EACH_FRAME, RADAR_DIAGNOSTICS_DEFAULT, RADAR_SELFTEST_SEQUENCE,
        offsets[0], offsets[1], offsets[2], offsets[3]);
    return 0;
}
