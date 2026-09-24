#include "radar_synthetic.h"
#include <math.h>
#include <string.h>
#define USE_BPM 1
#define RADAR_TYPES_H
#include "bpm_code.h"

#define SYN_PI 3.14159265358979323846f
static uint32_t random_word(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

int radar_synthetic_frame(unsigned char *payload, size_t length,
    const struct radar_synthetic_target *targets, unsigned int count,
    float noise_std, uint32_t seed, unsigned int bpm_offset)
{
    /* Persistent scratch keeps the RTOS task's stack small. Single caller. */
    static float fast_cos[RADAR_SYNTHETIC_MAX_TARGETS][RADAR_ADC_SAMPLES];
    static float fast_sin[RADAR_SYNTHETIC_MAX_TARGETS][RADAR_ADC_SAMPLES];
    if (!payload || length != RADAR_SYNTHETIC_BYTES ||
        count > RADAR_SYNTHETIC_MAX_TARGETS || (count && !targets) ||
        !isfinite(noise_std) || noise_std < 0.0f) return -1;
    for (unsigned int t = 0; t < count; ++t) {
        if (!isfinite(targets[t].range_bin) || !isfinite(targets[t].doppler_bin) ||
            !isfinite(targets[t].azimuth_deg) || !isfinite(targets[t].amplitude) ||
            targets[t].range_bin <= 0.0f || targets[t].range_bin >= RADAR_RANGE_FFT_SIZE / 2u ||
            targets[t].amplitude < 0.0f) return -1;
        for (unsigned int tx = 0; tx < RADAR_NUM_TX; ++tx)
            if (!isfinite(targets[t].tx_gain[tx]) || targets[t].tx_gain[tx] < 0.0f ||
                !isfinite(targets[t].tx_phase_error_deg[tx])) return -1;
        for (unsigned int s = 0; s < RADAR_ADC_SAMPLES; ++s) {
            float p = 2.0f * SYN_PI * targets[t].range_bin * (float)s / RADAR_RANGE_FFT_SIZE;
            fast_cos[t][s] = cosf(p);
            fast_sin[t][s] = sinf(p);
        }
    }
    memset(payload, 0, length);
    if (!seed) seed = 1;
    for (unsigned int chirp = 0; chirp < RADAR_CHIRPS; ++chirp) {
        float cr[RADAR_SYNTHETIC_MAX_TARGETS][RADAR_NUM_RX] = {{0}};
        float ci[RADAR_SYNTHETIC_MAX_TARGETS][RADAR_NUM_RX] = {{0}};
        float common = (float)bpm_code[(chirp + RADAR_BPM_START + bpm_offset % RADAR_BPM_LENGTH) % RADAR_BPM_LENGTH];
        for (unsigned int t = 0; t < count; ++t) {
            /* Independent documented coarray: relative TX centres 0/7.84/
             * 15.68/23.52 mm, RX centres 0/1.96/3.92/5.88 mm. */
            float spatial = 2.0f * SYN_PI * 0.00196f *
                RADAR_CENTER_FREQUENCY_HZ / 299792458.0f *
                sinf(targets[t].azimuth_deg * SYN_PI / 180.0f);
            for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
                for (unsigned int tx = 0; tx < RADAR_NUM_TX; ++tx) {
                    /* Independent analytic transmitter, not the resolver. */
                    const unsigned int offsets[RADAR_NUM_TX] = RADAR_TX_SUBBAND_OFFSETS;
                    float p = 2.0f * SYN_PI * (targets[t].doppler_bin / RADAR_CHIRPS +
                        (float)offsets[tx] / RADAR_DDMA_NUM_SUBBANDS) * chirp + spatial * (tx * RADAR_NUM_RX + rx) +
                        targets[t].tx_phase_error_deg[tx] * SYN_PI / 180.0f;
                    float a = common * targets[t].amplitude * targets[t].tx_gain[tx];
                    cr[t][rx] += a * cosf(p);
                    ci[t][rx] += a * sinf(p);
                }
            }
        }
        for (unsigned int s = 0; s < RADAR_ADC_SAMPLES; ++s) {
            for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
                float value = 0.0f;
                for (unsigned int t = 0; t < count; ++t)
                    value += cr[t][rx] * fast_cos[t][s] - ci[t][rx] * fast_sin[t][s];
                if (noise_std > 0.0f) {
                    float uniform = (float)(random_word(&seed) >> 8) / 16777216.0f;
                    value += noise_std * 1.7320508075688772f * (2.0f * uniform - 1.0f);
                }
                value = fmaxf(-2048.0f, fminf(2047.0f, value));
                int q = (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
                uint16_t word = (uint16_t)((q + 2048) << 4);
                unsigned int offset = chirp * RADAR_CHIRP_BYTES + RADAR_CHIRP_HEADER_BYTES +
                    (s * RADAR_NUM_RX + rx) * RADAR_SAMPLE_BYTES;
                payload[offset] = (unsigned char)word;
                payload[offset + 1u] = (unsigned char)(word >> 8);
            }
        }
    }
    return 0;
}

unsigned int radar_synthetic_scene(uint32_t index,
    struct radar_synthetic_target targets[RADAR_SYNTHETIC_MAX_TARGETS])
{
    const float range_bin = 299792458.0f * RADAR_ADC_SAMPLE_RATE_HZ /
        (2.0f * RADAR_CHIRP_SLOPE_HZ_PER_SECOND * RADAR_RANGE_FFT_SIZE);
    const float velocity_bin = 299792458.0f / RADAR_CENTER_FREQUENCY_HZ /
        (2.0f * RADAR_CHIRP_PERIOD_SECONDS * RADAR_CHIRPS);
    unsigned int n = index % 150u, count;
    float dt = (float)RADAR_FRAME_PERIOD_US * 1e-6f;
    if (!targets) return 0;
    memset(targets, 0, sizeof(*targets) * RADAR_SYNTHETIC_MAX_TARGETS);
    if (n < 40u) {
        count = 1;
        targets[0].range_bin = 50.25f + 25.0f * n * dt / range_bin;
        targets[0].doppler_bin = 25.0f / velocity_bin;
        targets[0].azimuth_deg = 15.0f;
    } else if (n >= 50u && n < 130u) {
        float t = (n - 50u) * dt;
        count = 2;
        targets[0].range_bin = (20.0f + 2.0f * t) / range_bin;
        targets[1].range_bin = (28.0f - 2.0f * t) / range_bin;
        targets[0].doppler_bin = 2.0f / velocity_bin;
        targets[1].doppler_bin = -2.0f / velocity_bin;
        targets[0].azimuth_deg = 15.0f;
        targets[1].azimuth_deg = -15.0f;
    } else return 0;
    for (unsigned int i = 0; i < count; ++i) {
        targets[i].amplitude = i ? 70.0f : 100.0f;
        for (unsigned int tx = 0; tx < RADAR_NUM_TX; ++tx)
            targets[i].tx_gain[tx] = 1.0f - 0.1f * tx;
    }
    return count;
}
