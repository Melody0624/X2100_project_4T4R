#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <float.h>
#include <string.h>

#include <NE10_dsp.h>
#include <fft.h>
#include "common.h"
#include <include_bin.h>
#include <os.h>
#include <driver/systick.h>
#include <lds_symbol.h>
#include "complex_abs_f32.h"
#include "motorcycle_output.h"
#include "radar_can_protocol.h"
#include "radar_frontend.h"
#include "radar_4tx4rx_profile.h"
#include "radar_pipeline.h"
#include "radar_synthetic.h"
#include "radar_tracking.h"
#include "radar_ego_motion.h"
#include "radar_warning.h"

/* Compile the original BPM table without importing the old application's
 * large radar_types.h dependency graph.  stdint.h above provides int8_t. */
#define USE_BPM 1
#define RADAR_TYPES_H
#include "bpm_code.h"

#define ADC_PACKET_BYTES      (RADAR_PAYLOAD_BYTES + ADC_PAYLOAD_OFFSET)
#define ADC_PAYLOAD_OFFSET    36u
#define ADC_PAYLOAD_BYTES     RADAR_PAYLOAD_BYTES
#define ADC_NUM_SAMPLES       RADAR_ADC_SAMPLES
#define ADC_NUM_CHIRPS        RADAR_CHIRPS
#define ADC_NUM_RX            RADAR_NUM_RX
#define ADC_CHIRP_HEADER_BYTES RADAR_CHIRP_HEADER_BYTES
#define ADC_BYTES_PER_CHIRP   RADAR_CHIRP_DATA_BYTES
#define ADC_CHIRP_BYTES       RADAR_CHIRP_BYTES
#define ADC_FIRST_VALUES      8u

/* Optimized live build.  The numerical pipeline is unchanged, while its
 * persistent workspace and hot loops are reduced for the X2100 target. */
#define REPLAY_PRODUCTION_MODE 1
#define DDMA_OPTIMIZED_BUILD   1
#define REPLAY_FRAME_PERIOD_US RADAR_FRAME_PERIOD_US
#define REPLAY_REPORT_FRAMES   RADAR_REPORT_FRAMES

struct replay_stage_perf {
    uint64_t range_fft_us;
    uint64_t doppler_fft_us;
    uint64_t detection_us;
};

static struct replay_stage_perf replay_stage_perf;
#define replay_validate_frame 0
static uint32_t replay_output_frame_id;
static uint16_t replay_output_detection_count;
static struct radar_pipeline_stats pipeline_stats;
static int pipeline_initialized;
static struct motorcycle_detection last_output[MOTORCYCLE_OUTPUT_MAX_DETECTIONS];
static unsigned int last_output_count;
static struct motorcycle_track last_tracks[RADAR_TRACKING_MAX_TRACKS];
static uint16_t last_track_count;
static struct motorcycle_warning last_warning;

#if REPLAY_PRODUCTION_MODE
#define printf(...) ((void)0)
#endif

#define RADAR_PI 3.14159265358979323846f

static void publish_complete_frame(uint32_t frame_id,
                                   const struct motorcycle_detection *detections,
                                   uint16_t detection_count)
{
    radar_tracking_step(detections, detection_count,
                        (float)RADAR_FRAME_PERIOD_US / 1000000.0f);
    last_track_count = radar_tracking_copy_tracks(
        last_tracks, RADAR_TRACKING_MAX_TRACKS);
    radar_warning_evaluate(last_tracks, last_track_count, &last_warning);
    motorcycle_output_publish_full(frame_id, detections, detection_count,
                                   last_tracks, last_track_count,
                                   &last_warning);
}

static float range_blackman_window[ADC_NUM_SAMPLES];

#if !REPLAY_PRODUCTION_MODE
static uint32_t read_le32(const unsigned char *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint32_t checksum_sum32(const unsigned char *data,
                               unsigned int size)
{
    uint32_t sum = 0;

    for (unsigned int i = 0; i < size; ++i)
        sum += data[i];

    return sum;
}

struct adc_channel_stats {
    int minimum;
    int maximum;
    int sum;
    unsigned int low_nibble_errors;
    int first[ADC_FIRST_VALUES];
};

static const int expected_minimum[ADC_NUM_RX] = {
    -1134, -269, -673, -295
};

static const int expected_maximum[ADC_NUM_RX] = {
    1036, 247, 643, 336
};

static const int expected_sum[ADC_NUM_RX] = {
    -1418511, -951329, -12724, 1369426
};

static const int expected_first[ADC_NUM_RX][ADC_FIRST_VALUES] = {
    {-332, -302, -305, -330, -281, -280, -307, -276},
    {72, 51, 65, 44, 39, 50, 50, 55},
    {148, 141, 143, 159, 164, 161, 174, 167},
    {287, 259, 250, 234, 213, 188, 189, 168},
};

static int unpack_and_validate_adc(const unsigned char *payload)
{
    struct adc_channel_stats stats[ADC_NUM_RX];
    int passed = 1;

    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        stats[rx].minimum = 32767;
        stats[rx].maximum = -32768;
        stats[rx].sum = 0;
        stats[rx].low_nibble_errors = 0;
        for (unsigned int i = 0; i < ADC_FIRST_VALUES; ++i)
            stats[rx].first[i] = 0;
    }

    for (unsigned int chirp = 0; chirp < ADC_NUM_CHIRPS; ++chirp) {
        const unsigned char *samples = payload +
            chirp * ADC_CHIRP_BYTES + ADC_CHIRP_HEADER_BYTES;

        for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample) {
            for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
                unsigned int offset = (sample * ADC_NUM_RX + rx) * 2u;
                uint16_t raw = (uint16_t)samples[offset] |
                    ((uint16_t)samples[offset + 1u] << 8);
                int value = (int)(raw >> 4) - 2048;

                if ((raw & 0x000fu) != 0)
                    ++stats[rx].low_nibble_errors;
                if (value < stats[rx].minimum)
                    stats[rx].minimum = value;
                if (value > stats[rx].maximum)
                    stats[rx].maximum = value;
                stats[rx].sum += value;
                if (chirp == 0 && sample < ADC_FIRST_VALUES)
                    stats[rx].first[sample] = value;
            }
        }
    }

    printf("[ADC] layout=%ux%ux%u samples/chirps/rx\n",
           ADC_NUM_SAMPLES, ADC_NUM_CHIRPS, ADC_NUM_RX);

    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        printf("[ADC] RX%u min=%d max=%d sum=%d low_nibble_errors=%u\n",
               rx, stats[rx].minimum, stats[rx].maximum,
               stats[rx].sum, stats[rx].low_nibble_errors);
        printf("[ADC] RX%u first8:", rx);
        for (unsigned int i = 0; i < ADC_FIRST_VALUES; ++i)
            printf(" %d", stats[rx].first[i]);
        printf("\n");

        if (stats[rx].minimum != expected_minimum[rx] ||
            stats[rx].maximum != expected_maximum[rx] ||
            stats[rx].sum != expected_sum[rx] ||
            stats[rx].low_nibble_errors != 0) {
            passed = 0;
        }
        for (unsigned int i = 0; i < ADC_FIRST_VALUES; ++i) {
            if (stats[rx].first[i] != expected_first[rx][i])
                passed = 0;
        }
    }

    if (passed)
        printf("[ADC] unpack validation passed\n");
    else
        printf("[ADC] unpack validation FAILED\n");

    return passed;
}
#endif

static float absolute_float(float value)
{
    return value < 0.0f ? -value : value;
}

/* Direct port of motor_cycle_demo/src/general_functions.c. */
static void generate_blackman_window(float *window, int length)
{
    float a0 = 0.42f;
    float a1 = 0.5f;
    float a2 = 0.08f;

    if (window == NULL || length <= 0)
        return;
    if (length == 1) {
        window[0] = 1.0f;
        return;
    }

    for (int n = 0; n < length; ++n) {
        float arg1 = 2.0f * (float)RADAR_PI * n / (length - 1);
        float arg2 = 4.0f * (float)RADAR_PI * n / (length - 1);
        window[n] = a0 - a1 * cosf(arg1) + a2 * cosf(arg2);
    }
}

static int read_adc_sample(const unsigned char *payload,
                           unsigned int chirp,
                           unsigned int sample,
                           unsigned int rx)
{
    const unsigned char *samples = payload +
        chirp * ADC_CHIRP_BYTES + ADC_CHIRP_HEADER_BYTES;
    unsigned int offset = (sample * ADC_NUM_RX + rx) * 2u;
    uint16_t raw = (uint16_t)samples[offset] |
        ((uint16_t)samples[offset + 1u] << 8);

    pipeline_stats.adc_low_nibble_errors += (raw & 15u) != 0u;
    pipeline_stats.adc_clipped_samples +=
        (raw >> 4) == 0u || (raw >> 4) == 4095u;
    if (radar_diagnostics_level >= 2u)
        radar_diagnostics_adc(rx, (int)(raw >> 4) - 2048, raw);
    return (int)(raw >> 4) - 2048;
}

#if !REPLAY_PRODUCTION_MODE
#define PRE_SELECTED_COUNT 12u

static const unsigned int pre_selected_indices[PRE_SELECTED_COUNT] = {
    0, 1, 2, 63, 126, 252, 253, 254, 379, 503, 504, 505
};

static const int expected_chirp0_sum[ADC_NUM_RX] = {
    -18249, 11073, -25594, 12493
};

static const float expected_selected_windowed[ADC_NUM_RX]
                                             [PRE_SELECTED_COUNT] = {
    { 0.000004409772f, -0.003705159f, -0.014985824f,
      2.45171547f, 28.4515686f, 27.064785f,
      13.0650082f, 8.06406403f, 16.944376f,
      0.002400039f, 0.000431893f, -0.000000611919f },
    { -0.000000746796f, 0.000405670f, 0.002402582f,
      -2.90271115f, 16.6233349f, 29.1161366f,
      20.1162796f, 7.1155839f, 27.1151485f,
      0.002291442f, 0.000599441f, -0.000000359365f },
    { -0.000002959088f, 0.002669219f, 0.010786895f,
      8.43897343f, 44.1946754f, 13.5808115f,
      -9.41882229f, -8.41776943f, 59.4247093f,
      -0.003868743f, -0.001104145f, 0.000001243040f },
    { -0.000003908728f, 0.003264548f, 0.012554940f,
      -0.442498446f, -30.0167179f, -36.6891403f,
      -19.6894112f, -52.6821976f, -23.2477684f,
      -0.001320236f, -0.000426673f, 0.000000427510f },
};

static const float expected_frame_abs_sum[ADC_NUM_RX] = {
    1945274.625f, 2130746.5f, 3317997.75f, 2559934.75f
};

static const float expected_frame_energy[ADC_NUM_RX] = {
    155294736.0f, 177737184.0f, 451663808.0f, 255473952.0f
};

static int preprocess_and_validate(const unsigned char *payload)
{
    float *window = range_blackman_window;
    float selected[ADC_NUM_RX][PRE_SELECTED_COUNT] = {{0}};
    float frame_abs_sum[ADC_NUM_RX] = {0};
    float frame_energy[ADC_NUM_RX] = {0};
    int chirp0_sum[ADC_NUM_RX] = {0};
    int passed = 1;

    generate_blackman_window(window, ADC_NUM_SAMPLES);
    printf("[PRE] Blackman generated by original C formula\n");
    printf("[PRE] Blackman edge=(%.9f, %.9f) center=(%.9f, %.9f)\n",
           window[0], window[ADC_NUM_SAMPLES - 1u],
           window[252], window[253]);

    if (absolute_float(window[0]) > 0.000001f ||
        absolute_float(window[ADC_NUM_SAMPLES - 1u]) > 0.000001f ||
        absolute_float(window[252] - 0.999984086f) > 0.000002f ||
        absolute_float(window[253] - 0.999984086f) > 0.000002f) {
        printf("[PRE] Blackman formula validation FAILED\n");
        return 0;
    }
    for (unsigned int i = 0; i < ADC_NUM_SAMPLES / 2u; ++i) {
        if (absolute_float(window[i] -
                           window[ADC_NUM_SAMPLES - 1u - i]) > 0.000002f) {
            printf("[PRE] Blackman symmetry validation FAILED at %u\n", i);
            return 0;
        }
    }

    /* Keep the same RX -> Chirp -> Sample loop order as the original C. */
    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        for (unsigned int chirp = 0; chirp < ADC_NUM_CHIRPS; ++chirp) {
            int integer_sum = 0;
            float mean;

            for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample)
                integer_sum += read_adc_sample(payload, chirp, sample, rx);

            mean = (float)integer_sum / (float)ADC_NUM_SAMPLES;
            if (chirp == 0)
                chirp0_sum[rx] = integer_sum;

            for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample) {
                float dc_removed =
                    (float)read_adc_sample(payload, chirp, sample, rx) - mean;
                float windowed = dc_removed * window[sample];

                frame_abs_sum[rx] += absolute_float(windowed);
                frame_energy[rx] += windowed * windowed;

                if (chirp == 0) {
                    for (unsigned int k = 0; k < PRE_SELECTED_COUNT; ++k) {
                        if (sample == pre_selected_indices[k])
                            selected[rx][k] = windowed;
                    }
                }
            }
        }
    }

    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        float mean0 = (float)chirp0_sum[rx] / (float)ADC_NUM_SAMPLES;

        printf("[PRE] RX%u chirp0_sum=%d mean=%.6f\n",
               rx, chirp0_sum[rx], mean0);
        printf("[PRE] RX%u selected:", rx);
        for (unsigned int k = 0; k < PRE_SELECTED_COUNT; ++k)
            printf(" %.6f", selected[rx][k]);
        printf("\n");
        printf("[PRE] RX%u abs_sum=%.3f energy=%.3f\n",
               rx, frame_abs_sum[rx], frame_energy[rx]);

        if (chirp0_sum[rx] != expected_chirp0_sum[rx])
            passed = 0;
        for (unsigned int k = 0; k < PRE_SELECTED_COUNT; ++k) {
            if (absolute_float(selected[rx][k] -
                               expected_selected_windowed[rx][k]) > 0.002f) {
                passed = 0;
            }
        }
        if (absolute_float(frame_abs_sum[rx] -
                           expected_frame_abs_sum[rx]) > 256.0f ||
            absolute_float(frame_energy[rx] -
                           expected_frame_energy[rx]) > 65536.0f) {
            passed = 0;
        }
    }

    if (passed)
        printf("[PRE] DC removal + Blackman validation passed\n");
    else
        printf("[PRE] DC removal + Blackman validation FAILED\n");

    return passed;
}
#endif

#define RANGE_FFT_SIZE       RADAR_RANGE_FFT_SIZE
#define RANGE_FFT_BINS       (RADAR_RANGE_FFT_SIZE / 2u)
#define RANGE_FFT_GUARD      16u
#define RANGE_SELECTED_COUNT 12u

static float range_fft_input[RANGE_FFT_SIZE + RANGE_FFT_GUARD]
    __attribute__((aligned(16)));
static float range_fft_output[RANGE_FFT_SIZE + 2u + RANGE_FFT_GUARD]
    __attribute__((aligned(16)));

#if !REPLAY_PRODUCTION_MODE
static const unsigned int range_selected_bins[RANGE_SELECTED_COUNT] = {
    0, 1, 2, 3, 7, 15, 31, 63, 127, 191, 254, 255
};

static const unsigned int expected_peak_bin[ADC_NUM_RX] = {
    0, 8, 7, 8
};

static const float expected_range_selected[ADC_NUM_RX]
                                          [RANGE_SELECTED_COUNT][2] = {
    {
        {9818.30762f, 0.0f}, {-8719.12988f, -2910.70410f},
        {5092.40332f, 2096.45703f}, {-1784.98389f, -115.325386f},
        {6638.23047f, -1884.10437f}, {593.151062f, -511.321106f},
        {-284.056061f, 42.958763f}, {-90.541817f, -32.565407f},
        {5.176658f, -125.430649f}, {2.942070f, -42.624672f},
        {38.524658f, 68.819580f}, {-71.055664f, -75.232788f},
    },
    {
        {2584.98145f, 0.0f}, {-3546.37988f, -3259.95898f},
        {3247.08008f, 4851.40039f}, {-1006.97009f, -2331.51855f},
        {1988.56152f, -7924.18506f}, {-679.163940f, -992.778992f},
        {345.828217f, 255.402924f}, {116.142067f, 114.194931f},
        {29.190382f, -46.821529f}, {-29.862591f, 57.463985f},
        {-46.475464f, 51.367676f}, {74.759155f, -25.735962f},
    },
    {
        {-8041.60352f, 0.0f}, {8993.18066f, -953.154114f},
        {-7074.49609f, -1440.92896f}, {1601.08142f, 1993.72949f},
        {-6608.13281f, -11768.4189f}, {-279.863586f, -654.644043f},
        {-553.975952f, -165.897217f}, {149.265060f, 71.239647f},
        {59.621834f, 137.578400f}, {14.513268f, -64.409531f},
        {107.203369f, 41.148743f}, {-42.653320f, -13.937317f},
    },
    {
        {-1078.25537f, 0.0f}, {280.600494f, -5299.26025f},
        {1049.68921f, 3996.38379f}, {-970.594727f, -1479.07886f},
        {7659.68164f, -5233.04395f}, {290.727661f, -177.617126f},
        {-369.958923f, 212.523605f}, {43.040352f, 59.539047f},
        {10.456963f, 57.332035f}, {-88.381813f, -34.306744f},
        {-20.975342f, -54.424927f}, {12.259613f, 45.571289f},
    },
};

static const float expected_range_energy[ADC_NUM_RX] = {
    4.36026409e10f, 4.62469325e10f, 1.17782610e11f, 6.64409211e10f
};

static int range_value_matches(float actual, float expected)
{
    float tolerance = 0.5f + absolute_float(expected) * 0.0001f;
    return absolute_float(actual - expected) <= tolerance;
}

static int range_fft_and_validate(const unsigned char *payload)
{
    ne10_fft_r2c_cfg_float32_t cfg;
    float selected[ADC_NUM_RX][RANGE_SELECTED_COUNT][2] = {{{0}}};
    float frame_energy[ADC_NUM_RX] = {0};
    float chirp0_peak_energy[ADC_NUM_RX] = {0};
    unsigned int peak_bin[ADC_NUM_RX] = {0};
    int passed = 1;

    cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)RANGE_FFT_SIZE);
    if (cfg == NULL) {
        printf("[RFFT] failed to allocate NE10 configuration\n");
        return 0;
    }

    printf("[RFFT] original NE10/MXU R2C FFT, size=%u bins=%u\n",
           RANGE_FFT_SIZE, RANGE_FFT_BINS);

    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        for (unsigned int chirp = 0; chirp < ADC_NUM_CHIRPS; ++chirp) {
            int integer_sum = 0;
            float mean;

            for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample)
                integer_sum += read_adc_sample(payload, chirp, sample, rx);
            mean = (float)integer_sum / (float)ADC_NUM_SAMPLES;

            for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample) {
                float dc_removed =
                    (float)read_adc_sample(payload, chirp, sample, rx) - mean;
                range_fft_input[sample] =
                    dc_removed * range_blackman_window[sample];
            }
            for (unsigned int sample = ADC_NUM_SAMPLES;
                 sample < RANGE_FFT_SIZE; ++sample) {
                range_fft_input[sample] = 0.0f;
            }

            /* Same function used by the original range_fft(). */
            fft_msa(range_fft_input, range_fft_output, cfg);

            for (unsigned int bin = 0; bin < RANGE_FFT_BINS; ++bin) {
                float real = range_fft_output[2u * bin];
                float imag = range_fft_output[2u * bin + 1u];
                float energy = real * real + imag * imag;

                frame_energy[rx] += energy;
                if (chirp == 0) {
                    if (bin == 0 || energy > chirp0_peak_energy[rx]) {
                        peak_bin[rx] = bin;
                        chirp0_peak_energy[rx] = energy;
                    }
                    for (unsigned int k = 0;
                         k < RANGE_SELECTED_COUNT; ++k) {
                        if (bin == range_selected_bins[k]) {
                            selected[rx][k][0] = real;
                            selected[rx][k][1] = imag;
                        }
                    }
                }
            }
        }
    }

    ne10_fft_destroy_r2c_float32(cfg);

    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        printf("[RFFT] RX%u chirp0_peak_bin=%u energy=%.3e\n",
               rx, peak_bin[rx], frame_energy[rx]);
        printf("[RFFT] RX%u bins:", rx);
        for (unsigned int k = 0; k < RANGE_SELECTED_COUNT; ++k) {
            printf(" %u:(%.3f,%.3f)", range_selected_bins[k],
                   selected[rx][k][0], selected[rx][k][1]);
        }
        printf("\n");

        if (peak_bin[rx] != expected_peak_bin[rx])
            passed = 0;
        for (unsigned int k = 0; k < RANGE_SELECTED_COUNT; ++k) {
            if (!range_value_matches(selected[rx][k][0],
                                     expected_range_selected[rx][k][0]) ||
                !range_value_matches(selected[rx][k][1],
                                     expected_range_selected[rx][k][1])) {
                passed = 0;
            }
        }
        if (absolute_float(frame_energy[rx] - expected_range_energy[rx]) >
            expected_range_energy[rx] * 0.005f) {
            passed = 0;
        }
    }

    if (passed)
        printf("[RFFT] 512-point Range FFT validation passed\n");
    else
        printf("[RFFT] 512-point Range FFT validation FAILED\n");

    return passed;
}
#endif

#define DOPPLER_FFT_SIZE       RADAR_DOPPLER_FFT_SIZE
#define DOPPLER_FFT_GUARD      16u
#define DOPPLER_SELECTED_COUNT 12u
#define NONCOHERENT_SELECTED_COUNT 17u

static ne10_fft_cpx_float32_t doppler_fft_input[
    DOPPLER_FFT_SIZE + DOPPLER_FFT_GUARD] __attribute__((aligned(16)));
static ne10_fft_cpx_float32_t doppler_fft_output[
    DOPPLER_FFT_SIZE + DOPPLER_FFT_GUARD] __attribute__((aligned(16)));
static float doppler_hanning_window[DOPPLER_FFT_SIZE];
static float doppler_weight[DOPPLER_FFT_SIZE];

typedef char adc_chirp_layout_must_match[
    (ADC_CHIRP_HEADER_BYTES + ADC_NUM_SAMPLES * ADC_NUM_RX * 2u ==
     ADC_CHIRP_BYTES) ? 1 : -1];
typedef char adc_payload_layout_must_match[
    (ADC_CHIRP_BYTES * ADC_NUM_CHIRPS == ADC_PAYLOAD_BYTES) ? 1 : -1];
typedef char doppler_layout_must_match[
    (ADC_NUM_CHIRPS == DOPPLER_FFT_SIZE) ? 1 : -1];

static const unsigned int doppler_selected_bins[DOPPLER_SELECTED_COUNT] = {
    0, 1, 2, 3, 7, 15, 31, 47, 63, 95, 126, 127
};

static const unsigned int doppler_signature_range[ADC_NUM_RX] = {
    0, 8, 7, 8
};

static const unsigned int expected_doppler_peak_range[ADC_NUM_RX] = {
    8, 8, 7, 8
};

static const unsigned int expected_doppler_peak_bin[ADC_NUM_RX] = {
    32, 32, 0, 32
};

static const float expected_doppler_energy[ADC_NUM_RX] = {
    2.11373772e12f, 2.24373427e12f, 5.70057084e12f, 3.21625968e12f
};

static const float expected_doppler_selected[ADC_NUM_RX]
                                             [DOPPLER_SELECTED_COUNT][2] = {
    {
        {-308700.188f, 0.0f}, {149903.625f, 26403.3633f},
        {6992.95703f, -20714.1992f}, {-4415.53516f, 20138.8672f},
        {-12079.1934f, -623.333618f}, {3053.32422f, 15059.1475f},
        {120225.891f, 25205.3281f}, {-307.493835f, 10303.4736f},
        {12379.875f, -15429.6992f}, {119581.484f, -79123.2109f},
        {6992.95703f, 20714.1992f}, {149903.625f, -26403.3633f},
    },
    {
        {-369902.375f, -143163.281f}, {188066.344f, 78455.9922f},
        {-1440.94189f, 229.933411f}, {2842.49756f, -3403.53784f},
        {369.936523f, 1840.00903f}, {-189.140228f, -4.030762f},
        {-172936.578f, 190388.875f}, {301.277161f, -349.207520f},
        {3282.36084f, 3355.22876f}, {-8321.125f, -6300.05469f},
        {2306.50098f, 219.568848f}, {176485.406f, 66035.4766f},
    },
    {
        {685206.062f, 454030.156f}, {-350419.094f, -241639.891f},
        {3438.94141f, -1053.49683f}, {-3681.71436f, 4088.77539f},
        {25.387207f, -2744.65820f}, {1218.72034f, 3105.61108f},
        {172076.047f, -178956.297f}, {-2246.66309f, -290.730713f},
        {-3843.90723f, -672.104980f}, {9571.67188f, 7202.21875f},
        {-3606.96631f, -2075.09155f}, {-326805.156f, -209863.172f},
    },
    {
        {-63500.1406f, -666817.5f}, {23059.6133f, 346720.812f},
        {-819.712158f, -2253.69214f}, {4838.30762f, 804.720947f},
        {-1022.58740f, -176.507568f}, {968.721802f, 202.098206f},
        {-301273.281f, -93394.2656f}, {1490.13391f, 1397.72583f},
        {-622.102539f, 4705.91016f}, {2604.90625f, -13133.918f},
        {16.075409f, 4415.76855f}, {36248.0742f, 313213.938f},
    },
};

struct noncoherent_reference {
    unsigned int range;
    unsigned int doppler;
    float magnitude;
};

static const struct noncoherent_reference expected_noncoherent_selected[
    NONCOHERENT_SELECTED_COUNT] = {
    {0, 0, 541713.875f}, {0, 32, 801319.562f},
    {0, 64, 49513.4961f}, {0, 96, 801319.562f},
    {7, 0, 1608429.12f}, {7, 32, 1803118.38f},
    {7, 64, 43324.8281f}, {7, 96, 64819.4609f},
    {8, 0, 2003023.0f}, {8, 32, 2390720.25f},
    {8, 64, 53428.4453f}, {8, 96, 80557.9688f},
    {31, 7, 2642.67969f}, {63, 15, 1357.07385f},
    {127, 31, 3621.01343f}, {191, 63, 2204.13867f},
    {255, 127, 1383.81421f},
};

static void generate_doppler_hanning(void)
{
    for (unsigned int n = 0; n < DOPPLER_FFT_SIZE; ++n) {
        float arg = 2.0f * RADAR_PI * (float)(n + 1u) /
                    (float)(DOPPLER_FFT_SIZE + 1u);
        doppler_hanning_window[n] = 0.5f * (1.0f - cosf(arg));
        doppler_weight[n] = doppler_hanning_window[n] *
            radar_4tx4rx_chirp_code(n, (float)bpm_code[(n + RADAR_BPM_START) % RADAR_BPM_LENGTH]);
    }
}

static int doppler_value_matches(float actual, float expected)
{
    float tolerance = 5.0f + absolute_float(expected) * 0.001f;
    return absolute_float(actual - expected) <= tolerance;
}

#define DDMA_NUM_SUBBANDS RADAR_DDMA_NUM_SUBBANDS
#define DDMA_NUM_TX RADAR_NUM_TX
#define DDMA_BINS_PER_SUBBAND RADAR_DDMA_BINS_PER_SUBBAND
#define DDMA_MAP_COUNT (RANGE_FFT_BINS * DDMA_BINS_PER_SUBBAND)
static struct ddma_result ddma_results[DDMA_MAP_COUNT];
#define DDMA_SELECTED_COUNT 17u

typedef char ddma_subband_count_must_be_eight[
    (DDMA_NUM_SUBBANDS == 8u) ? 1 : -1];
typedef char ddma_tx_count_must_be_four[
    (DDMA_NUM_TX == 4u) ? 1 : -1];

/* Current software candidate uses four adjacent cyclic subbands.  Keeping
 * the map local removes more than 260k cross-module helper calls per frame. */
static const unsigned char ddma_subband_lut[DDMA_NUM_SUBBANDS][DDMA_NUM_TX] = {
    {0u, 1u, 2u, 3u}, {1u, 2u, 3u, 4u},
    {2u, 3u, 4u, 5u}, {3u, 4u, 5u, 6u},
    {4u, 5u, 6u, 7u}, {5u, 6u, 7u, 0u},
    {6u, 7u, 0u, 1u}, {7u, 0u, 1u, 2u},
};

#if !REPLAY_PRODUCTION_MODE
struct ddma_reference {
    unsigned int range;
    unsigned int sub_doppler;
    unsigned int best_subband;
    unsigned int full_doppler;
    float amplitude;
    float db;
    float metric;
};

static const struct ddma_reference expected_ddma_selected[
    DDMA_SELECTED_COUNT] = {
    {0, 0, 0, 0, 1343033.5f, 122.561737f, 541713.875f},
    {0, 7, 2, 71, 48105.0547f, 93.643814f, 22588.2695f},
    {0, 15, 0, 15, 49103.7422f, 93.822289f, 20497.0996f},
    {0, 31, 2, 95, 675234.125f, 116.589088f, 268777.594f},
    {7, 0, 0, 0, 3411547.5f, 130.659027f, 1608429.12f},
    {7, 7, 2, 71, 19847.9668f, 85.954323f, 6436.89502f},
    {7, 15, 2, 79, 21193.9609f, 86.524239f, 10395.2354f},
    {7, 31, 3, 127, 1606328.25f, 124.116684f, 760985.375f},
    {8, 0, 0, 0, 4393743.0f, 132.856689f, 2003023.0f},
    {8, 7, 2, 71, 22751.1406f, 87.140060f, 7695.45215f},
    {8, 15, 2, 79, 22338.6914f, 86.981155f, 10525.8008f},
    {8, 31, 3, 127, 2066243.0f, 126.303627f, 945018.438f},
    {31, 3, 3, 99, 6148.31641f, 75.775124f, 2338.448f},
    {63, 7, 1, 39, 4930.64062f, 73.858063f, 2384.82812f},
    {127, 15, 0, 15, 7101.49023f, 77.026993f, 3345.51025f},
    {191, 23, 1, 55, 7011.72266f, 76.916496f, 2817.40894f},
    {255, 31, 1, 63, 4675.70312f, 73.396942f, 2100.21411f},
};
#endif

static int cfar_2d_and_validate(const float *rd_map_db,
                                const unsigned int *best_subband,
                                const ne10_fft_cpx_float32_t *doppler_cube);

static int ddma_decode_and_validate(
    const float *noncoherent,
    const ne10_fft_cpx_float32_t *doppler_cube)
{
    static unsigned int *best_subband;
#if !REPLAY_PRODUCTION_MODE
    static float *best_metric;
    static float *rd_amplitude;
#endif
    static float *rd_map_db;
#if !REPLAY_PRODUCTION_MODE
    unsigned int subband_counts[DDMA_NUM_SUBBANDS] = {0};
    unsigned int peak_range = 0;
    unsigned int peak_sub_doppler = 0;
    float peak_amplitude = 0.0f;
    float amplitude_sum = 0.0f;
    float amplitude_energy = 0.0f;
    float db_sum = 0.0f;
    float metric_sum = 0.0f;
#endif
    int passed = 1;
    int cfar_passed = 1;

    if (best_subband == NULL)
        best_subband = (unsigned int *)malloc(
            DDMA_MAP_COUNT * sizeof(*best_subband));
#if !REPLAY_PRODUCTION_MODE
    if (best_metric == NULL)
        best_metric = (float *)malloc(DDMA_MAP_COUNT * sizeof(*best_metric));
    if (rd_amplitude == NULL)
        rd_amplitude = (float *)malloc(
            DDMA_MAP_COUNT * sizeof(*rd_amplitude));
#endif
    if (rd_map_db == NULL)
        rd_map_db = (float *)malloc(DDMA_MAP_COUNT * sizeof(*rd_map_db));
    if (best_subband == NULL || rd_map_db == NULL
#if !REPLAY_PRODUCTION_MODE
        || best_metric == NULL || rd_amplitude == NULL
#endif
        ) {
        printf("[DDMA] allocation failed\n");
        passed = 0;
        goto cleanup;
    }

    printf("[DDMA] original C max-min cyclic subband decode\n");
    printf("[DDMA] layout=%ux%u range/subdoppler, %u subbands, %u TX\n",
           RANGE_FFT_BINS, DDMA_BINS_PER_SUBBAND,
           DDMA_NUM_SUBBANDS, DDMA_NUM_TX);

    /* Keep the fused loop and original max-min score. v2 deliberately changes
     * tie/contamination acceptance; outputs are NOT identical to the unsafe
     * single-winner baseline in ambiguous scenes. */
    for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
        for (unsigned int sub_doppler = 0;
             sub_doppler < DDMA_BINS_PER_SUBBAND; ++sub_doppler) {
            float subband_values[DDMA_NUM_SUBBANDS];
#if !REPLAY_PRODUCTION_MODE
            float current_max_metric = -FLT_MAX;
#endif
            unsigned int selected_subband = 0;
            unsigned int map_index =
                sub_doppler * RANGE_FFT_BINS + range;

            for (unsigned int subband = 0;
                 subband < DDMA_NUM_SUBBANDS; ++subband) {
                unsigned int doppler =
                    sub_doppler + subband * DDMA_BINS_PER_SUBBAND;
                subband_values[subband] =
                    noncoherent[doppler * RANGE_FFT_BINS + range];
            }

            ddma_resolve(subband_values, &ddma_default_policy,
                         &ddma_results[map_index]);
            if (radar_diagnostics_level >= 2u)
                radar_diagnostics_cell(range, sub_doppler, subband_values,
                                       &ddma_results[map_index]);
            selected_subband = ddma_results[map_index].anchor;
#if !REPLAY_PRODUCTION_MODE
            current_max_metric = ddma_results[map_index].best;
#endif
            ++pipeline_stats.status_cells[ddma_results[map_index].status];

            best_subband[map_index] = selected_subband;
#if !REPLAY_PRODUCTION_MODE
            best_metric[map_index] = current_max_metric;
#endif
            {
                float amplitude = 0.0f;
                float db;

                for (unsigned int tx = 0; tx < DDMA_NUM_TX; ++tx) {
                    unsigned int subband =
                        ddma_subband_lut[selected_subband][tx];
                    unsigned int doppler = sub_doppler +
                        subband * DDMA_BINS_PER_SUBBAND;
                    amplitude +=
                        noncoherent[doppler * RANGE_FFT_BINS + range];
                }
                /* Leave rejected-cell energy in the CFAR noise estimate;
                 * zeroing it would create artificial peaks at its edges. */
                if (!isfinite(amplitude)) {
                    passed = 0;
                    goto cleanup;
                }
#if !REPLAY_PRODUCTION_MODE
            rd_amplitude[map_index] = amplitude;
#endif
                db = 20.0f * log10f(
                    amplitude > FLT_MIN ? amplitude : FLT_MIN);
                rd_map_db[map_index] = db;
#if !REPLAY_PRODUCTION_MODE
                if (replay_validate_frame) {
                    amplitude_sum += amplitude;
                    amplitude_energy += amplitude * amplitude;
                    db_sum += db;
                    metric_sum += current_max_metric;
                    ++subband_counts[selected_subband];
                    if ((range == 0 && sub_doppler == 0) ||
                        amplitude > peak_amplitude) {
                        peak_amplitude = amplitude;
                        peak_range = range;
                        peak_sub_doppler = sub_doppler;
                    }
                }
#endif
            }
        }
    }

#if !REPLAY_PRODUCTION_MODE
    if (replay_validate_frame) {
      {
        unsigned int peak_index =
            peak_sub_doppler * RANGE_FFT_BINS + peak_range;
        unsigned int peak_start = best_subband[peak_index];
        unsigned int peak_full =
            peak_sub_doppler + peak_start * DDMA_BINS_PER_SUBBAND;
        float peak_db = 20.0f * log10f(peak_amplitude);

        printf("[DDMA] peak=(r%u,sd%u) amp=%.3f db=%.6f sb=%u full_d=%u metric=%.3f\n",
               peak_range, peak_sub_doppler, peak_amplitude, peak_db,
               peak_start, peak_full, best_metric[peak_index]);
        if (peak_range != 8u || peak_sub_doppler != 0u ||
            peak_start != 0u || peak_full != 0u ||
            !doppler_value_matches(peak_amplitude, 4393743.0f) ||
            absolute_float(peak_db - 132.856689f) > 0.05f ||
            !doppler_value_matches(best_metric[peak_index], 2003023.0f)) {
            passed = 0;
        }
    }

    printf("[DDMA] best_subband_counts=%u,%u,%u,%u\n",
           subband_counts[0], subband_counts[1],
           subband_counts[2], subband_counts[3]);
    if (subband_counts[0] != 2747u || subband_counts[1] != 2073u ||
        subband_counts[2] != 1993u || subband_counts[3] != 1379u) {
        passed = 0;
    }

    printf("[DDMA] sums amp=%.3e energy=%.3e db=%.3f metric=%.3e\n",
           amplitude_sum, amplitude_energy, db_sum, metric_sum);
    if (absolute_float(amplitude_sum - 98013840.0f) > 98013840.0f * 0.005f ||
        absolute_float(amplitude_energy - 8.77221653e13f) >
            8.77221653e13f * 0.005f ||
        absolute_float(db_sum - 617610.25f) > 617610.25f * 0.001f ||
        absolute_float(metric_sum - 43082040.0f) > 43082040.0f * 0.005f) {
        passed = 0;
    }

    printf("[DDMA] selected:");
    for (unsigned int k = 0; k < DDMA_SELECTED_COUNT; ++k) {
        const struct ddma_reference *expected = &expected_ddma_selected[k];
        unsigned int map_index =
            expected->sub_doppler * RANGE_FFT_BINS + expected->range;
        unsigned int actual_subband = best_subband[map_index];
        unsigned int actual_full = expected->sub_doppler +
            actual_subband * DDMA_BINS_PER_SUBBAND;
        float actual_amplitude = rd_amplitude[map_index];
        float actual_db = 20.0f * log10f(actual_amplitude);

        printf(" r%usd%u:(sb%u,d%u,%.3f)",
               expected->range, expected->sub_doppler,
               actual_subband, actual_full, actual_amplitude);
        if (actual_subband != expected->best_subband ||
            actual_full != expected->full_doppler ||
            !doppler_value_matches(actual_amplitude, expected->amplitude) ||
            absolute_float(actual_db - expected->db) > 0.05f ||
            !doppler_value_matches(best_metric[map_index], expected->metric)) {
            passed = 0;
        }
    }
    printf("\n");
    }
#endif

    if (passed)
        cfar_passed = cfar_2d_and_validate(
            rd_map_db, best_subband, doppler_cube);

cleanup:
    /* Persistent work buffers are intentionally retained and reused by every
     * frame.  This removes allocator churn and heap fragmentation. */
    if (passed)
        printf("[DDMA] 4-subband decode + 256x32 RD map validation passed\n");
    else
        printf("[DDMA] 4-subband decode + 256x32 RD map validation FAILED\n");
    return passed && cfar_passed;
}

#define CFAR_WINDOW_LENGTH 4u
#define CFAR_GUARD_LENGTH 2u
#define CFAR_DOPPLER_THRESHOLD_DB 6.0f
#define CFAR_RANGE_THRESHOLD_DB 3.0f
#define CFAR_SELECTED_COUNT 17u
#define CFAR_EXPECTED_DETECTIONS 5u
#define LIVE_MAX_DETECTIONS MOTORCYCLE_OUTPUT_MAX_DETECTIONS

struct cfar_cell {
    float doppler_threshold;
    float range_threshold;
    unsigned char doppler_peak;
    unsigned char doppler_detection;
    unsigned char range_peak;
    unsigned char range_detection;
};

struct cfar_reference {
    unsigned int range;
    unsigned int doppler;
    float doppler_threshold;
    unsigned int doppler_peak;
    unsigned int doppler_detection;
    float range_threshold;
    unsigned int range_peak;
    unsigned int range_detection;
    unsigned int final_detection;
    unsigned int final_peak;
};

static const struct cfar_reference expected_cfar_selected[
    CFAR_SELECTED_COUNT] = {
    {0, 0, 100.012070f, 1, 1, 121.074249f, 0, 0, 0, 0},
    {0, 7, 101.919930f, 1, 0, 0.0f, 0, 0, 0, 0},
    {0, 15, 100.751839f, 1, 0, 0.0f, 0, 0, 0, 0},
    {0, 31, 99.067438f, 0, 0, 0.0f, 0, 0, 0, 0},
    {7, 0, 91.483694f, 1, 1, 124.160028f, 0, 0, 0, 0},
    {7, 7, 92.062256f, 1, 0, 0.0f, 0, 0, 0, 0},
    {7, 15, 91.291727f, 0, 0, 0.0f, 0, 0, 0, 0},
    {7, 31, 91.313583f, 0, 0, 0.0f, 0, 0, 0, 0},
    {8, 0, 93.655191f, 1, 1, 121.591841f, 1, 1, 1, 1},
    {8, 7, 92.511305f, 1, 0, 0.0f, 0, 0, 0, 0},
    {8, 15, 92.191498f, 0, 0, 0.0f, 0, 0, 0, 0},
    {8, 31, 92.356957f, 0, 0, 0.0f, 0, 0, 0, 0},
    {31, 3, 81.792095f, 0, 0, 79.655945f, 0, 0, 0, 0},
    {63, 7, 79.351069f, 0, 0, 0.0f, 0, 0, 0, 0},
    {127, 15, 80.461157f, 1, 0, 0.0f, 0, 0, 0, 0},
    {191, 23, 80.139900f, 1, 0, 0.0f, 0, 0, 0, 0},
    {255, 31, 78.657532f, 1, 0, 0.0f, 0, 0, 0, 0},
};

struct cfar_detection_reference {
    unsigned int range;
    unsigned int doppler;
    float power;
    float snr;
    unsigned int best_subband;
};

static const struct cfar_detection_reference expected_cfar_detections[
    CFAR_EXPECTED_DETECTIONS] = {
    {1, 0, 127.249336f, 36.061455f, 0},
    {8, 0, 132.856689f, 45.201500f, 0},
    {15, 3, 87.917824f, 10.587679f, 2},
    {26, 0, 98.580688f, 23.489727f, 0},
    {31, 0, 100.466904f, 26.345512f, 0},
};

#define AZIM_FFT_SIZE 128u
#define AZIM_NUM_VIRTUAL_ANTS RADAR_NUM_VIRTUAL_ANTS
#define AZIM_SPECTRUM_SELECTED_COUNT 11u

struct angle_detection {
    unsigned int range;
    unsigned int doppler;
    unsigned int best_subband;
    unsigned int raw_doppler;
    int decoded_doppler;
    float range_bin_interpolated;
    float power;
    float snr;
    unsigned int is_peak;
};

struct point_result {
    float range;
    float velocity_ambiguous;
    float velocity_ground;
    float azimuth;
    float x_rcs;
    float y_rcs;
    float x_output;
    float y_output;
    float power;
    float snr;
    unsigned int motion_state;
    unsigned int is_peak;
    unsigned int rejection_mask;
    unsigned int relative_rd_index;
};

static const float expected_range_bin_interpolated[
    CFAR_EXPECTED_DETECTIONS] = {
    1.44128728f, 7.86643362f, 15.3620787f, 25.9184895f, 31.2056808f
};

static const float expected_point_range[CFAR_EXPECTED_DETECTIONS] = {
    0.582918525f, 3.1815238f, 6.2130847f, 10.4825506f, 12.6209183f
};

static const float expected_point_velocity[CFAR_EXPECTED_DETECTIONS] = {
    0.0f, 0.0f, 39.4476156f, 0.0f, 0.0f
};

static const float expected_point_x[CFAR_EXPECTED_DETECTIONS] = {
    0.582889259f, 2.34669805f, 5.57076502f, 9.92709923f, 9.8449955f
};

static const float expected_point_y[CFAR_EXPECTED_DETECTIONS] = {
    -0.00584270433f, -2.14827871f, 2.7511816f,
    -3.36698174f, 7.89706516f
};

static const unsigned int expected_post_mask[CFAR_EXPECTED_DETECTIONS] = {
    0u, 0u, 4u, 0u, 0u
};

static const unsigned int expected_post_motion[CFAR_EXPECTED_DETECTIONS] = {
    0u, 0u, 2u, 0u, 0u
};

/* Active source default from board_calib_config.c:
 * BOARD_TYPE_LONG_STRIP / BOARD_ID_LONG_STRIP == 5.  A product fitted with
 * a radar front-end may replace these values with its Flash calibration. */
static const unsigned int expected_angle_peak[CFAR_EXPECTED_DETECTIONS] = {
    127u, 85u, 28u, 107u, 40u
};

static const float expected_angle_deg[CFAR_EXPECTED_DETECTIONS] = {
    -0.574296207f, -42.4724634f, 26.2829547f,
    -18.735434f, 38.7344965f
};

static const float expected_angle_peak_mag[CFAR_EXPECTED_DETECTIONS] = {
    286027.344f, 719747.0f, 3003.49658f, 10197.4463f, 20795.7012f
};

static const float expected_angle_sum[CFAR_EXPECTED_DETECTIONS] = {
    18180054.0f, 38741368.0f, 267628.188f, 752021.75f, 939447.875f
};

static const float expected_angle_energy[CFAR_EXPECTED_DETECTIONS] = {
    3.45119497e12f, 1.69897278e13f, 602432384.0f,
    5.29781299e9f, 1.08863089e10f
};

static const float expected_virtual[CFAR_EXPECTED_DETECTIONS]
                                   [AZIM_NUM_VIRTUAL_ANTS][2] = {
    {
        {112178.047f,-40828.4883f}, {18359.6504f,-5998.96729f},
        {3161.87305f,45495.9297f}, {-70917.5625f,37160.7578f},
        {3305.87598f,72691.6094f}, {68004.5312f,113402.055f},
        {-11795.2734f,164368.531f}, {-62010.6641f,-3967.79883f},
    },
    {
        {-39201.8867f,40575.4766f}, {28442.6094f,227191.516f},
        {271328.562f,-53403.9688f}, {16075.7285f,-206651.719f},
        {73545.7812f,86056.3281f}, {166302.172f,-107407.5f},
        {-133976.375f,-126227.531f}, {-154069.344f,87162.8281f},
    },
    {
        {604.432129f,-1364.48169f}, {-400.703247f,2629.02466f},
        {-1225.86597f,-1644.12085f}, {-576.034912f,-435.237946f},
        {185.756104f,665.955383f}, {36.7457237f,199.629776f},
        {162.271912f,887.369446f}, {-84.2056122f,-238.973358f},
    },
    {
        {1767.74146f,-2159.67139f}, {322.988159f,-3104.53662f},
        {-4351.97119f,3421.83447f}, {2463.34521f,3355.95654f},
        {-1119.2522f,1185.59375f}, {641.766235f,657.385498f},
        {269.383301f,-4048.46973f}, {-3310.50928f,-3230.95825f},
    },
    {
        {1602.38184f,-1317.38037f}, {-3791.45166f,7040.03125f},
        {-4900.16357f,-2799.31567f}, {5891.21777f,574.179993f},
        {20.5010681f,2246.35571f}, {-5039.86816f,-1455.58032f},
        {3111.81177f,-2657.73096f}, {1015.31433f,2793.05859f},
    },
};

static const unsigned int angle_spectrum_bins[
    AZIM_SPECTRUM_SELECTED_COUNT] = {
    0u, 1u, 2u, 3u, 31u, 32u, 63u, 64u, 95u, 126u, 127u
};

static const float expected_angle_spectrum[CFAR_EXPECTED_DETECTIONS]
                                          [AZIM_SPECTRUM_SELECTED_COUNT] = {
    {285930.562f,284981.156f,283009.594f,279853.219f,178562.516f,
     183170.766f,51597.2461f,58642.5742f,93784.5391f,285439.781f,
     286027.344f},
    {393039.562f,374580.375f,356262.656f,338350.094f,135079.484f,
     128999.805f,166909.0f,190873.156f,473698.312f,429150.25f,
     411340.031f},
    {1369.48621f,1278.474f,1187.422f,1100.42456f,2966.45996f,
     2937.75098f,1137.55725f,1170.81433f,1895.35364f,1540.4375f,
     1457.50452f},
    {4128.00977f,4167.59766f,4253.28857f,4374.9751f,2229.40454f,
     1967.97205f,6274.4209f,6654.57715f,5697.37842f,4222.6543f,
     4143.90137f},
    {2282.65723f,2380.20874f,2482.66919f,2579.71289f,15814.4336f,
     16776.5254f,4536.17285f,4703.62891f,6131.51123f,2143.19043f,
     2200.39819f},
};

static ne10_fft_cpx_float32_t angle_fft_input[
    AZIM_FFT_SIZE + DOPPLER_FFT_GUARD] __attribute__((aligned(16)));
static ne10_fft_cpx_float32_t angle_fft_output[
    AZIM_FFT_SIZE + DOPPLER_FFT_GUARD] __attribute__((aligned(16)));
static float angle_magnitude[AZIM_FFT_SIZE];
static float angle_window[AZIM_NUM_VIRTUAL_ANTS];
static float angle_axis[AZIM_FFT_SIZE];

static float angle_parab_peak(const float *x, const float *y,
                              int n, int k)
{
    int km1 = (k - 1 + n) % n;
    int kp1 = (k + 1) % n;
    float x0 = x[km1];
    float x1 = x[k];
    float x2 = x[kp1];
    float y0 = y[km1];
    float y1 = y[k];
    float y2 = y[kp1];
    float dx10 = x1 - x0;
    float dx21 = x2 - x1;
    float s1;
    float s2;
    float a;
    float b;
    float peak;
    float minimum;
    float maximum;

    if (fabsf(dx10) < 1.0e-9f || fabsf(dx21) < 1.0e-9f)
        return x1;
    s1 = (y1 - y0) / dx10;
    s2 = (y2 - y1) / dx21;
    a = (s2 - s1) / (x2 - x0);
    if (a >= 0.0f)
        return x1;
    b = s1 - a * (x1 + x0);
    peak = -b / (2.0f * a);
    minimum = fminf(x0, x2);
    maximum = fmaxf(x0, x2);
    if (peak < minimum)
        peak = minimum;
    if (peak > maximum)
        peak = maximum;
    return peak;
}

static float firmware_range_resolution(void)
{
    /* FFT-bin spacing uses 512, NOT the 506 valid samples.  Physical range
     * resolution c/(2*B_sampled) is a different quantity. */
    return 299792458.0f * RADAR_ADC_SAMPLE_RATE_HZ /
        (2.0f * RADAR_CHIRP_SLOPE_HZ_PER_SECOND * RANGE_FFT_SIZE);
}

static float firmware_doppler_resolution(void)
{
    float wavelength = 299792458.0f / RADAR_CENTER_FREQUENCY_HZ;
    return wavelength /
        (2.0f * RADAR_CHIRP_PERIOD_SECONDS * (float)DOPPLER_FFT_SIZE);
}

static int postprocess_and_validate(const struct point_result *raw_points,
                                    unsigned int count,
                                    struct motorcycle_detection *output,
                                    uint16_t *output_count)
{
    struct point_result points[LIVE_MAX_DETECTIONS];
    unsigned int valid_count = 0;
    unsigned int rejected_count = 0;
    unsigned int angle_count = 0;
    unsigned int bounce_count = 0;
    unsigned int near_count = 0;
    unsigned int ground_count = 0;
    unsigned int very_near_count = 0;
    float install_angle = 180.0f * RADAR_PI / 180.0f;
    float cosine = cosf(install_angle);
    float sine = sinf(install_angle);
    float ego_vx = 0.0f, ego_vy = 0.0f;
    float ego_angles[LIVE_MAX_DETECTIONS];
    float ego_velocities[LIVE_MAX_DETECTIONS];
    unsigned int ego_count = 0u;
    int ego_valid;
    int passed = 1;

    if (count > LIVE_MAX_DETECTIONS || output == NULL ||
        output_count == NULL)
        return 0;
    *output_count = 0u;
    for (unsigned int i = 0; i < count; ++i)
        points[i] = raw_points[i];

    for (unsigned int i = 0; i < count; ++i) {
        if (points[i].range >= 0.5f &&
            fabsf(points[i].azimuth) <= 60.0f &&
            isfinite(points[i].azimuth) &&
            isfinite(points[i].velocity_ambiguous)) {
            ego_angles[ego_count] = points[i].azimuth;
            ego_velocities[ego_count++] = points[i].velocity_ambiguous;
        }
    }
    ego_valid = radar_ego_estimate(ego_angles, ego_velocities, ego_count,
                                   &ego_vx, &ego_vy);

    /* Same simple range sort as detection_post_processing.c. */
    for (unsigned int i = 0; i + 1u < count; ++i) {
        for (unsigned int j = i + 1u; j < count; ++j) {
            if (points[i].range > points[j].range) {
                struct point_result temporary = points[i];
                points[i] = points[j];
                points[j] = temporary;
            }
        }
    }

    for (unsigned int i = 0; i < count; ++i) {
        points[i].x_output =
            points[i].x_rcs * cosine + points[i].y_rcs * sine;
        points[i].y_output =
            points[i].y_rcs * cosine - points[i].x_rcs * sine;
        points[i].velocity_ground = radar_ego_correct_velocity(
            points[i].velocity_ambiguous, points[i].azimuth,
            ego_vx, ego_vy, 180.0f, ego_valid);
        points[i].motion_state =
            fabsf(points[i].velocity_ground) > 1.0f ? 2u : 0u;
        if (fabsf(points[i].azimuth) > 60.0f)
            points[i].rejection_mask |= 0x01u;
    }

#if RADAR_LEGACY_POST_FILTERS
    for (unsigned int a = 0; a < count; ++a) {
        if (points[a].rejection_mask != 0u)
            continue;
        for (unsigned int b = a + 1u; b < count; ++b) {
            if (fabsf(points[a].azimuth - points[b].azimuth) > 2.0f)
                continue;
            for (unsigned int times = 2u; times <= 3u; ++times) {
                float range_difference = fabsf(
                    points[b].range - (float)times * points[a].range);
                float velocity_difference = fabsf(
                    points[b].velocity_ambiguous -
                    (float)times * points[a].velocity_ambiguous);
                if (range_difference > 0.0f && range_difference < 2.0f &&
                    velocity_difference > 0.0f &&
                    velocity_difference < 1.0f) {
                    points[b].rejection_mask |= 0x02u;
                    break;
                }
            }
        }
    }

    for (unsigned int i = 0; i < count; ++i) {
        if (points[i].range < 5.0f &&
            fabsf(points[i].azimuth) < 45.0f &&
            points[i].power < 120.0f) {
            points[i].rejection_mask |= 0x08u;
        } else if (fabsf(points[i].y_rcs) < 5.0f &&
                   points[i].x_rcs < 10.0f &&
                   points[i].power < 105.0f &&
                   points[i].motion_state == 2u) {
            points[i].rejection_mask |= 0x04u;
        }
    }
#endif
    for (unsigned int i = 0; i < count; ++i) {
        if (points[i].range < 0.5f)
            points[i].rejection_mask |= 0x05u;

        angle_count += (points[i].rejection_mask & 0x01u) != 0u;
        bounce_count += (points[i].rejection_mask & 0x02u) != 0u;
        near_count += (points[i].rejection_mask & 0x08u) != 0u;
        ground_count += (points[i].rejection_mask & 0x04u) != 0u;
        very_near_count += points[i].range < 0.5f;
        if (points[i].rejection_mask == 0u)
            ++valid_count;
        else
            ++rejected_count;

        printf("[POST] p%u range=%.6f v=%.6f motion=%u mask=0x%02x valid=%u\n",
               i, points[i].range, points[i].velocity_ground,
               points[i].motion_state, points[i].rejection_mask,
               points[i].rejection_mask == 0u);
        printf("[POST] p%u output=(%.6f,%.6f) azimuth=%.6f\n",
               i, points[i].x_output, points[i].y_output,
               points[i].azimuth);

        if (replay_validate_frame &&
            (points[i].motion_state != expected_post_motion[i] ||
             points[i].rejection_mask != expected_post_mask[i] ||
             absolute_float(points[i].x_output + expected_point_x[i]) > 0.003f ||
             absolute_float(points[i].y_output + expected_point_y[i]) > 0.003f)) {
            passed = 0;
        }
        if (points[i].rejection_mask == 0u &&
            *output_count < MOTORCYCLE_OUTPUT_MAX_DETECTIONS) {
            struct motorcycle_detection *destination =
                &output[*output_count];

            memset(destination, 0, sizeof(*destination));
            destination->rel_rd_index = (uint16_t)points[i].relative_rd_index;
            destination->motion_state = (uint8_t)points[i].motion_state;
            destination->is_peak = (uint8_t)points[i].is_peak;
            destination->velocity_mps = points[i].velocity_ground;
            destination->x_output_m = points[i].x_output;
            destination->y_output_m = points[i].y_output;
            destination->range_m = points[i].range;
            destination->azimuth_deg = points[i].azimuth;
            destination->velocity_ambiguous_mps =
                points[i].velocity_ambiguous;
            destination->x_rcs_m = points[i].x_rcs;
            destination->y_rcs_m = points[i].y_rcs;
            destination->power = points[i].power;
            destination->snr = points[i].snr;
            ++*output_count;
        }
    }

    printf("[POST] counts input=%u valid=%u rejected=%u angle=%u bounce=%u near=%u ground=%u verynear=%u\n",
           count, valid_count, rejected_count, angle_count, bounce_count,
           near_count, ground_count, very_near_count);
    if (replay_validate_frame &&
        (valid_count != 4u || rejected_count != 1u || angle_count != 0u ||
         bounce_count != 0u || near_count != 0u || ground_count != 1u ||
         very_near_count != 0u)) {
        passed = 0;
    }

    if (passed)
        printf("[POST] source module validation passed (call disabled in active v2 pipeline)\n");
    else
        printf("[POST] source module validation FAILED\n");
    return passed;
}

static int angle_estimation_and_validate(
    const struct angle_detection *detections,
    unsigned int num_detections,
    const ne10_fft_cpx_float32_t *doppler_cube)
{
    static ne10_fft_r2c_cfg_float32_t cfg;
    static int angle_tables_initialized;
    struct point_result points[LIVE_MAX_DETECTIONS] = {{0}};
    struct motorcycle_detection output_points[LIVE_MAX_DETECTIONS] = {{0}};
    uint16_t output_point_count = 0u;
    float range_resolution = firmware_range_resolution();
    float doppler_resolution = firmware_doppler_resolution();
    int passed = 1;
    int post_passed = 1;

    if (num_detections > LIVE_MAX_DETECTIONS) {
        printf("[ANGLE] too many detections=%u\n", num_detections);
        return 0;
    }

    if (cfg == NULL)
        cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)AZIM_FFT_SIZE);
    if (cfg == NULL) {
        printf("[ANGLE] FFT configuration allocation failed\n");
        return 0;
    }

    if (!angle_tables_initialized) {
        for (unsigned int n = 0; n < AZIM_NUM_VIRTUAL_ANTS; ++n) {
            float arg = 2.0f * RADAR_PI * (float)(n + 1u) /
                        (float)(AZIM_NUM_VIRTUAL_ANTS + 1u);
            angle_window[n] = 0.5f * (1.0f - cosf(arg));
        }
        for (unsigned int bin = 0; bin < AZIM_FFT_SIZE; ++bin) {
            int signed_bin = bin < AZIM_FFT_SIZE / 2u ?
                             (int)bin : (int)bin - (int)AZIM_FFT_SIZE;
            float sine = (float)signed_bin /
                         ((float)AZIM_FFT_SIZE * radar_4tx4rx_spacing_wavelengths());
            if (sine > 1.0f)
                sine = 1.0f;
            if (sine < -1.0f)
                sine = -1.0f;
            angle_axis[bin] = asinf(sine) * (180.0f / RADAR_PI);
        }
        angle_tables_initialized = 1;
    }

    printf("[ANGLE] 4TXx4RX virtual array + Hanning + 128-point FFT\n");
    printf("[ANGLE] document 1.960mm geometry; confirmed chip routing, identity calibration\n");
    printf("[ANGLE] Hanning:");
    for (unsigned int n = 0; n < AZIM_NUM_VIRTUAL_ANTS; ++n)
        printf(" %.6f", angle_window[n]);
    printf("\n");
    printf("[POINT] constants range_res=%.9f doppler_res=%.9f wrap=%.6f\n",
           range_resolution, doppler_resolution, 160.0f / 3.6f);

    if (replay_validate_frame) {
        if (absolute_float(angle_window[0] - 0.116977781f) > 0.000002f ||
            absolute_float(angle_window[3] - 0.969846368f) > 0.000002f ||
            absolute_float(angle_axis[1] - 0.895282987f) > 0.00002f ||
            absolute_float(angle_axis[64] + 90.0f) > 0.00002f) {
            passed = 0;
        }
    }

    for (unsigned int det = 0; det < num_detections; ++det) {
        unsigned int peak_bin = 0;
        float peak_magnitude = 0.0f;
        float magnitude_sum = 0.0f;
        float magnitude_energy = 0.0f;
        float azimuth;
        int virtual_passed = 1;
        int spectrum_passed = 1;

#if REPLAY_PRODUCTION_MODE
        (void)virtual_passed;
        (void)spectrum_passed;
#endif

        for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
            for (unsigned int tx = 0; tx < DDMA_NUM_TX; ++tx) {
                unsigned int subband = radar_4tx4rx_tx_subband(
                    detections[det].best_subband, tx);
                unsigned int full_doppler;
                unsigned int cube_index;
                unsigned int virtual_index =
                    radar_4tx4rx_virtual_index(tx, rx);
                ne10_fft_cpx_float32_t value;
                ne10_fft_cpx_float32_t calibration;

                radar_4tx4rx_get_calibration(virtual_index,
                                             &calibration.r,
                                             &calibration.i);
                full_doppler = detections[det].doppler +
                               subband * DDMA_BINS_PER_SUBBAND;
                cube_index = rx * DOPPLER_FFT_SIZE * RANGE_FFT_BINS +
                             full_doppler * RANGE_FFT_BINS +
                             detections[det].range;
                value = doppler_cube[cube_index];
                angle_fft_input[virtual_index].r =
                    value.r * calibration.r - value.i * calibration.i;
                angle_fft_input[virtual_index].i =
                    value.r * calibration.i + value.i * calibration.r;
            }
        }

        for (unsigned int n = 0; n < AZIM_NUM_VIRTUAL_ANTS; ++n) {
            if (replay_validate_frame) {
                if (!doppler_value_matches(angle_fft_input[n].r,
                                            expected_virtual[det][n][0]) ||
                    !doppler_value_matches(angle_fft_input[n].i,
                                            expected_virtual[det][n][1])) {
                    passed = 0;
                    virtual_passed = 0;
                }
            }
            angle_fft_input[n].r *= angle_window[n];
            angle_fft_input[n].i *= angle_window[n];
        }
        printf("[ANGLE] det%u r%u d%u sb%u virtual=%s\n", det,
               detections[det].range, detections[det].doppler,
               detections[det].best_subband,
               virtual_passed ? "passed" : "FAILED");
        for (unsigned int n = AZIM_NUM_VIRTUAL_ANTS;
             n < AZIM_FFT_SIZE; ++n) {
            angle_fft_input[n].r = 0.0f;
            angle_fft_input[n].i = 0.0f;
        }

        ne10_fft_c2c_1d_float32_mxu_ai(
            angle_fft_output, angle_fft_input, cfg);
        for (unsigned int bin = 0; bin < AZIM_FFT_SIZE; ++bin) {
            float real = angle_fft_output[bin].r;
            float imag = angle_fft_output[bin].i;
            float magnitude = sqrtf(real * real + imag * imag);
            angle_magnitude[bin] = magnitude;
            if (replay_validate_frame) {
                magnitude_sum += magnitude;
                magnitude_energy += magnitude * magnitude;
            }
            if (bin == 0u || magnitude > peak_magnitude) {
                peak_magnitude = magnitude;
                peak_bin = bin;
            }
        }
        azimuth = angle_parab_peak(angle_axis, angle_magnitude,
                                   (int)AZIM_FFT_SIZE, (int)peak_bin);

        printf("[ANGLE] det%u peak=%u azimuth=%.6f mag=%.3f sum=%.3e energy=%.3e\n",
               det, peak_bin, azimuth, peak_magnitude,
               magnitude_sum, magnitude_energy);
        if (replay_validate_frame) {
            for (unsigned int k = 0;
                 k < AZIM_SPECTRUM_SELECTED_COUNT; ++k) {
                unsigned int bin = angle_spectrum_bins[k];
                if (!doppler_value_matches(
                        angle_magnitude[bin],
                        expected_angle_spectrum[det][k])) {
                    passed = 0;
                    spectrum_passed = 0;
                }
            }
        }
        printf("[ANGLE] det%u spectrum=%s\n", det,
               spectrum_passed ? "passed" : "FAILED");

        if (replay_validate_frame) {
            if (peak_bin != expected_angle_peak[det] ||
                absolute_float(azimuth - expected_angle_deg[det]) > 0.05f ||
                !doppler_value_matches(peak_magnitude,
                                       expected_angle_peak_mag[det]) ||
                absolute_float(magnitude_sum - expected_angle_sum[det]) >
                    expected_angle_sum[det] * 0.005f ||
                absolute_float(magnitude_energy - expected_angle_energy[det]) >
                    expected_angle_energy[det] * 0.005f) {
                passed = 0;
            }
        }

        points[det].range =
            detections[det].range_bin_interpolated * range_resolution;
        points[det].velocity_ambiguous =
            (float)detections[det].decoded_doppler * doppler_resolution;
        points[det].velocity_ground = points[det].velocity_ambiguous;
        points[det].azimuth = azimuth;
        points[det].x_rcs = points[det].range *
                            cosf(azimuth * RADAR_PI / 180.0f);
        points[det].y_rcs = points[det].range *
                            sinf(azimuth * RADAR_PI / 180.0f);
        points[det].power = detections[det].power;
        points[det].snr = detections[det].snr;
        points[det].is_peak = detections[det].is_peak;
        points[det].relative_rd_index = detections[det].doppler *
            RANGE_FFT_BINS + detections[det].range;

        printf("[POINT] p%u rbin=%.6f range=%.6f rawd=%u decd=%d velocity=%.6f\n",
               det, detections[det].range_bin_interpolated,
               points[det].range, detections[det].raw_doppler,
               detections[det].decoded_doppler,
               points[det].velocity_ambiguous);
        printf("[POINT] p%u azimuth=%.6f rcs=(%.6f,%.6f) power=%.3f snr=%.3f peak=%u\n",
               det, points[det].azimuth, points[det].x_rcs,
               points[det].y_rcs, detections[det].power,
               detections[det].snr, detections[det].is_peak);

        if (replay_validate_frame) {
            if (absolute_float(detections[det].range_bin_interpolated -
                               expected_range_bin_interpolated[det]) > 0.002f ||
                absolute_float(points[det].range -
                               expected_point_range[det]) > 0.003f ||
                absolute_float(points[det].velocity_ambiguous -
                               expected_point_velocity[det]) > 0.003f ||
                absolute_float(points[det].x_rcs -
                               expected_point_x[det]) > 0.003f ||
                absolute_float(points[det].y_rcs -
                               expected_point_y[det]) > 0.003f) {
                passed = 0;
            }
        }
    }

    if (passed)
        printf("[ANGLE] 5 detections AoA validation passed\n");
    else
        printf("[ANGLE] AoA validation FAILED\n");
    if (passed) {
        printf("[POINT] 5 raw detections physical output validation passed\n");
        post_passed = postprocess_and_validate(
            points, num_detections, output_points, &output_point_count);
        if (post_passed)
            publish_complete_frame(replay_output_frame_id, output_points,
                                   output_point_count);
        if (post_passed) {
            last_output_count = output_point_count;
            memcpy(last_output, output_points, output_point_count * sizeof(*output_points));
        }
        replay_output_detection_count = output_point_count;
    } else {
        printf("[POINT] physical output validation FAILED\n");
    }
    return passed && post_passed;
}

static int cfar_2d_and_validate(const float *rd_map_db,
                                const unsigned int *best_subband,
                                const ne10_fft_cpx_float32_t *doppler_cube)
{
    static struct cfar_cell *cells;
    struct angle_detection angle_detections[LIVE_MAX_DETECTIONS];
    unsigned char active_lines[DDMA_BINS_PER_SUBBAND] = {0};
    unsigned int doppler_detection_count = 0;
    unsigned int range_detection_count = 0;
    unsigned int final_detection_count = 0;
    unsigned int final_peak_count = 0;
    unsigned int active_count = 0;
    unsigned int detection_index = 0;
    float doppler_threshold_sum = 0.0f;
    float range_threshold_sum = 0.0f;
    int passed = 1;
    int angle_passed = 1;

    if (cells == NULL)
        cells = (struct cfar_cell *)calloc(DDMA_MAP_COUNT, sizeof(*cells));
    if (cells == NULL) {
        printf("[CFAR] allocation failed\n");
        return 0;
    }
#if !REPLAY_PRODUCTION_MODE
    /* Oracle mode inspects inactive-line fields, so it needs deterministic
     * zeroes.  Production writes every field that can be observed and avoids
     * clearing roughly 96 KiB on every frame. */
    memset(cells, 0, DDMA_MAP_COUNT * sizeof(*cells));
#endif

    printf("[CFAR] original C Doppler CASO + Range CAGO\n");
    printf("[CFAR] window=%u guard=%u thresholds=(dop %.1f, range %.1f) dB\n",
           CFAR_WINDOW_LENGTH, CFAR_GUARD_LENGTH,
           CFAR_DOPPLER_THRESHOLD_DB, CFAR_RANGE_THRESHOLD_DB);

    /* Doppler CFAR: cyclic 4-cell left/right windows.  The running-sum
     * update and asymmetric >= / > peak test match cfar_func_v2.c. */
    for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
        int left_win[CFAR_WINDOW_LENGTH];
        int right_win[CFAR_WINDOW_LENGTH];
        float left_sum = 0.0f;
        float right_sum = 0.0f;
        int left_start =
            (0 - (int)CFAR_GUARD_LENGTH - (int)CFAR_WINDOW_LENGTH +
             (int)DDMA_BINS_PER_SUBBAND) %
            (int)DDMA_BINS_PER_SUBBAND;
        int right_start =
            (0 + (int)CFAR_GUARD_LENGTH + 1) %
            (int)DDMA_BINS_PER_SUBBAND;

        for (unsigned int i = 0; i < CFAR_WINDOW_LENGTH; ++i) {
            left_win[i] = (left_start + (int)i) %
                          (int)DDMA_BINS_PER_SUBBAND;
            right_win[i] = (right_start + (int)i) %
                           (int)DDMA_BINS_PER_SUBBAND;
            left_sum += rd_map_db[left_win[i] * RANGE_FFT_BINS + range];
            right_sum += rd_map_db[right_win[i] * RANGE_FFT_BINS + range];
        }

        for (unsigned int doppler = 0;
             doppler < DDMA_BINS_PER_SUBBAND; ++doppler) {
            unsigned int index = doppler * RANGE_FFT_BINS + range;
            unsigned int previous =
                (doppler + DDMA_BINS_PER_SUBBAND - 1u) %
                DDMA_BINS_PER_SUBBAND;
            unsigned int following =
                (doppler + 1u) % DDMA_BINS_PER_SUBBAND;
            float noise = fminf(left_sum, right_sum) /
                          (float)CFAR_WINDOW_LENGTH;
            unsigned char peak =
                rd_map_db[index] >=
                    rd_map_db[previous * RANGE_FFT_BINS + range] &&
                rd_map_db[index] >
                    rd_map_db[following * RANGE_FFT_BINS + range];

            cells[index].doppler_threshold =
                noise + CFAR_DOPPLER_THRESHOLD_DB;
            cells[index].doppler_peak = peak;
            cells[index].doppler_detection =
                rd_map_db[index] > cells[index].doppler_threshold && peak;
            if (cells[index].doppler_detection)
                active_lines[doppler] = 1;

            left_sum = left_sum -
                rd_map_db[left_win[0] * RANGE_FFT_BINS + range] +
                rd_map_db[((left_win[CFAR_WINDOW_LENGTH - 1u] + 1) %
                           (int)DDMA_BINS_PER_SUBBAND) *
                          RANGE_FFT_BINS + range];
            right_sum = right_sum -
                rd_map_db[right_win[0] * RANGE_FFT_BINS + range] +
                rd_map_db[((right_win[CFAR_WINDOW_LENGTH - 1u] + 1) %
                           (int)DDMA_BINS_PER_SUBBAND) *
                          RANGE_FFT_BINS + range];
            for (unsigned int i = 0; i < CFAR_WINDOW_LENGTH - 1u; ++i) {
                left_win[i] = left_win[i + 1u];
                right_win[i] = right_win[i + 1u];
            }
            left_win[CFAR_WINDOW_LENGTH - 1u] =
                (left_win[CFAR_WINDOW_LENGTH - 2u] + 1) %
                (int)DDMA_BINS_PER_SUBBAND;
            right_win[CFAR_WINDOW_LENGTH - 1u] =
                (right_win[CFAR_WINDOW_LENGTH - 2u] + 1) %
                (int)DDMA_BINS_PER_SUBBAND;
        }
    }

    /* Range CFAR runs only on Doppler lines activated above. */
    for (unsigned int doppler = 0;
         doppler < DDMA_BINS_PER_SUBBAND; ++doppler) {
        int upper_win[CFAR_WINDOW_LENGTH];
        int lower_win[CFAR_WINDOW_LENGTH];
        float upper_sum = 0.0f;
        float lower_sum = 0.0f;
        int upper_start;
        int lower_start;

        if (!active_lines[doppler])
            continue;
        upper_start =
            (0 - (int)CFAR_GUARD_LENGTH - (int)CFAR_WINDOW_LENGTH +
             (int)RANGE_FFT_BINS) % (int)RANGE_FFT_BINS;
        lower_start =
            (0 + (int)CFAR_GUARD_LENGTH + 1) % (int)RANGE_FFT_BINS;
        for (unsigned int i = 0; i < CFAR_WINDOW_LENGTH; ++i) {
            upper_win[i] = (upper_start + (int)i) % (int)RANGE_FFT_BINS;
            lower_win[i] = (lower_start + (int)i) % (int)RANGE_FFT_BINS;
            upper_sum += rd_map_db[
                doppler * RANGE_FFT_BINS + (unsigned int)upper_win[i]];
            lower_sum += rd_map_db[
                doppler * RANGE_FFT_BINS + (unsigned int)lower_win[i]];
        }

        for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
            unsigned int index = doppler * RANGE_FFT_BINS + range;
            float noise = fmaxf(upper_sum, lower_sum) /
                          (float)CFAR_WINDOW_LENGTH;
            unsigned char peak = 0;

            cells[index].range_threshold = noise + CFAR_RANGE_THRESHOLD_DB;
            if (range > 0u && range < RANGE_FFT_BINS - 1u) {
                peak = rd_map_db[index] >= rd_map_db[index - 1u] &&
                       rd_map_db[index] > rd_map_db[index + 1u];
            }
            cells[index].range_peak = peak;
            cells[index].range_detection =
                rd_map_db[index] > cells[index].range_threshold && peak;

            upper_sum = upper_sum - rd_map_db[
                doppler * RANGE_FFT_BINS + (unsigned int)upper_win[0]] +
                rd_map_db[doppler * RANGE_FFT_BINS +
                    (unsigned int)((upper_win[CFAR_WINDOW_LENGTH - 1u] + 1) %
                                   (int)RANGE_FFT_BINS)];
            lower_sum = lower_sum - rd_map_db[
                doppler * RANGE_FFT_BINS + (unsigned int)lower_win[0]] +
                rd_map_db[doppler * RANGE_FFT_BINS +
                    (unsigned int)((lower_win[CFAR_WINDOW_LENGTH - 1u] + 1) %
                                   (int)RANGE_FFT_BINS)];
            for (unsigned int i = 0; i < CFAR_WINDOW_LENGTH - 1u; ++i) {
                upper_win[i] = upper_win[i + 1u];
                lower_win[i] = lower_win[i + 1u];
            }
            upper_win[CFAR_WINDOW_LENGTH - 1u] =
                (upper_win[CFAR_WINDOW_LENGTH - 2u] + 1) %
                (int)RANGE_FFT_BINS;
            lower_win[CFAR_WINDOW_LENGTH - 1u] =
                (lower_win[CFAR_WINDOW_LENGTH - 2u] + 1) %
                (int)RANGE_FFT_BINS;
        }
    }

    if (replay_validate_frame) {
        for (unsigned int doppler = 0;
             doppler < DDMA_BINS_PER_SUBBAND; ++doppler) {
            if (active_lines[doppler])
                ++active_count;
            for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
                unsigned int index = doppler * RANGE_FFT_BINS + range;
                unsigned int final_detection =
                    cells[index].doppler_detection &&
                    cells[index].range_detection;
                unsigned int final_peak =
                    cells[index].doppler_peak && cells[index].range_peak;
                doppler_threshold_sum += cells[index].doppler_threshold;
                range_threshold_sum += cells[index].range_threshold;
                doppler_detection_count += cells[index].doppler_detection;
                range_detection_count += cells[index].range_detection;
                final_detection_count += final_detection;
                final_peak_count += final_peak;
            }
        }

    printf("[CFAR] counts dop=%u range=%u final=%u ispeak=%u active:",
           doppler_detection_count, range_detection_count,
           final_detection_count, final_peak_count);
    for (unsigned int doppler = 0;
         doppler < DDMA_BINS_PER_SUBBAND; ++doppler) {
        if (active_lines[doppler])
            printf(" %u", doppler);
    }
    printf("\n");
    printf("[CFAR] threshold_sums dop=%.3f range=%.3f\n",
           doppler_threshold_sum, range_threshold_sum);

    if (doppler_detection_count != 46u || range_detection_count != 7u ||
        final_detection_count != 5u || final_peak_count != 57u ||
        active_count != 2u || !active_lines[0] || !active_lines[3] ||
        absolute_float(doppler_threshold_sum - 660228.962f) > 100.0f ||
        absolute_float(range_threshold_sum - 41745.475f) > 20.0f) {
        passed = 0;
    }

    printf("[CFAR] selected:");
    for (unsigned int k = 0; k < CFAR_SELECTED_COUNT; ++k) {
        const struct cfar_reference *expected = &expected_cfar_selected[k];
        unsigned int index =
            expected->doppler * RANGE_FFT_BINS + expected->range;
        struct cfar_cell *actual = &cells[index];
        unsigned int final_detection =
            actual->doppler_detection && actual->range_detection;
        unsigned int final_peak = actual->doppler_peak && actual->range_peak;

        printf(" r%ud%u:(%.3f,%u%u%u%u)",
               expected->range, expected->doppler,
               actual->doppler_threshold,
               actual->doppler_detection, actual->range_detection,
               final_detection, final_peak);
        if (absolute_float(actual->doppler_threshold -
                           expected->doppler_threshold) > 0.05f ||
            actual->doppler_peak != expected->doppler_peak ||
            actual->doppler_detection != expected->doppler_detection ||
            absolute_float(actual->range_threshold -
                           expected->range_threshold) > 0.05f ||
            actual->range_peak != expected->range_peak ||
            actual->range_detection != expected->range_detection ||
            final_detection != expected->final_detection ||
            final_peak != expected->final_peak) {
            passed = 0;
        }
    }
    printf("\n");
    }

    printf("[CFAR] detections:");
    {
        for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
            for (unsigned int doppler = 0;
                 doppler < DDMA_BINS_PER_SUBBAND; ++doppler) {
                unsigned int index = doppler * RANGE_FFT_BINS + range;
                if (cells[index].doppler_detection &&
                    cells[index].range_detection) {
                    if (ddma_results[index].status != DDMA_RESOLVED) {
                        ++pipeline_stats.rejected_peaks;
                        pipeline_stats.last_rejected_candidates =
                            ddma_results[index].candidates;
                        continue;
                    }
                    float snr = rd_map_db[index] -
                                cells[index].doppler_threshold +
                                CFAR_DOPPLER_THRESHOLD_DB;
                    float range_bin_interpolated = (float)range;
                    unsigned int raw_doppler = doppler +
                        best_subband[index] * DDMA_BINS_PER_SUBBAND;
                    int decoded_doppler = (int)raw_doppler;

                    if (cells[index].range_peak && range > 0u &&
                        range < RANGE_FFT_BINS - 1u) {
                        float y1 = rd_map_db[index - 1u];
                        float y2 = rd_map_db[index];
                        float y3 = rd_map_db[index + 1u];
                        float denominator = y1 - 2.0f * y2 + y3;
                        if (fabsf(denominator) >= 1.0e-6f) {
                            float delta = 0.5f * (y1 - y3) / denominator;
                            range_bin_interpolated = (float)range +
                                fmaxf(-0.5f, fminf(0.5f, delta));
                        }
                    }
                    /* Only reconstruct a velocity after the hypothesis and
                     * empty-band tests above.  The sampled +/-37.7 m/s span
                     * alone is NOT evidence of reliable DDMA disambiguation. */
                    if (raw_doppler >= DOPPLER_FFT_SIZE / 2u) {
                        decoded_doppler -= (int)DOPPLER_FFT_SIZE;
                    }
                    printf(" r%ud%u:(p%.3f,s%.3f,sb%u)",
                           range, doppler, rd_map_db[index], snr,
                           best_subband[index]);
                    if (detection_index < LIVE_MAX_DETECTIONS) {
                        angle_detections[detection_index].range = range;
                        angle_detections[detection_index].doppler = doppler;
                        angle_detections[detection_index].best_subband =
                            best_subband[index];
                        angle_detections[detection_index].raw_doppler =
                            raw_doppler;
                        angle_detections[detection_index].decoded_doppler =
                            decoded_doppler;
                        angle_detections[detection_index].range_bin_interpolated =
                            range_bin_interpolated;
                        angle_detections[detection_index].power =
                            rd_map_db[index];
                        angle_detections[detection_index].snr = snr;
                        angle_detections[detection_index].is_peak =
                            cells[index].doppler_peak && cells[index].range_peak;
                    }
                    if (replay_validate_frame) {
                        if (detection_index >= CFAR_EXPECTED_DETECTIONS ||
                            range != expected_cfar_detections[
                                detection_index].range ||
                            doppler != expected_cfar_detections[
                                detection_index].doppler ||
                            absolute_float(rd_map_db[index] -
                                expected_cfar_detections[
                                    detection_index].power) > 0.05f ||
                            absolute_float(snr -
                                expected_cfar_detections[
                                    detection_index].snr) > 0.05f ||
                            best_subband[index] !=
                                expected_cfar_detections[
                                    detection_index].best_subband) {
                            passed = 0;
                        }
                    }
                    if (detection_index < LIVE_MAX_DETECTIONS)
                        ++detection_index;
                    else
                        ++pipeline_stats.detection_overflow;
                }
            }
        }
        if (replay_validate_frame &&
            detection_index != CFAR_EXPECTED_DETECTIONS)
            passed = 0;
    }
    printf("\n");

    if (passed)
        printf("[CFAR] 2D CFAR mask + 5 detections validation passed\n");
    else
        printf("[CFAR] 2D CFAR mask + detections validation FAILED\n");
    if (passed)
        angle_passed = angle_estimation_and_validate(
            angle_detections, detection_index, doppler_cube);
    return passed && angle_passed;
}

static int bpm_doppler_fft_and_validate(const unsigned char *payload)
{
    const unsigned int radar_cube_count =
        ADC_NUM_RX * ADC_NUM_CHIRPS * RANGE_FFT_BINS;
    const unsigned int noncoherent_count =
        RANGE_FFT_BINS * DOPPLER_FFT_SIZE;
    static ne10_fft_cpx_float32_t *radar_cube;
    static float *noncoherent;
    static float *magnitude_temp;
    static ne10_fft_r2c_cfg_float32_t range_cfg;
    static ne10_fft_r2c_cfg_float32_t doppler_cfg;
    float selected[ADC_NUM_RX][DOPPLER_SELECTED_COUNT][2] = {{{0}}};
    float energy[ADC_NUM_RX] = {0};
    float peak_energy[ADC_NUM_RX] = {0};
    unsigned int peak_range[ADC_NUM_RX] = {0};
    unsigned int peak_doppler[ADC_NUM_RX] = {0};
    unsigned int noncoherent_peak_range = 0;
    unsigned int noncoherent_peak_doppler = 0;
    float noncoherent_peak = 0.0f;
    float noncoherent_sum = 0.0f;
    float noncoherent_energy = 0.0f;
    int passed = 1;
    int ddma_passed = 1;
    uint64_t stage_start_us;

    if (radar_cube == NULL)
        radar_cube = (ne10_fft_cpx_float32_t *)malloc(
            radar_cube_count * sizeof(*radar_cube));
    if (noncoherent == NULL)
        noncoherent = (float *)malloc(
            noncoherent_count * sizeof(*noncoherent));
    if (magnitude_temp == NULL)
        magnitude_temp = (float *)malloc(
            noncoherent_count * sizeof(*magnitude_temp));
    if (range_cfg == NULL)
        range_cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)RANGE_FFT_SIZE);
    if (doppler_cfg == NULL)
        doppler_cfg = ne10_fft_alloc_r2c_float32((ne10_int32_t)DOPPLER_FFT_SIZE);
    if (radar_cube == NULL || noncoherent == NULL ||
        magnitude_temp == NULL || range_cfg == NULL || doppler_cfg == NULL) {
        printf("[DFFT] allocation failed\n");
        passed = 0;
        goto cleanup;
    }
    printf("[DFFT] original C BPM + Hanning + NE10/MXU C2C FFT\n");
    printf("[DFFT] layout=%ux%ux%u range/doppler/rx\n",
           RANGE_FFT_BINS, DOPPLER_FFT_SIZE, ADC_NUM_RX);
    printf("[DFFT] Hanning edge=(%.9f,%.9f) center=(%.9f,%.9f)\n",
           doppler_hanning_window[0],
           doppler_hanning_window[DOPPLER_FFT_SIZE - 1u],
           doppler_hanning_window[63], doppler_hanning_window[64]);

    if (replay_validate_frame) {
        if (!doppler_value_matches(doppler_hanning_window[0],
                                   0.000592976809f) ||
            !doppler_value_matches(doppler_hanning_window[63],
                                   0.999851704f)) {
            passed = 0;
        }
    }

    /* Reproduce the original range_fft() output layout:
     * [rx][chirp][range]. */
    stage_start_us = systick_get_time_us();
    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        for (unsigned int chirp = 0; chirp < ADC_NUM_CHIRPS; ++chirp) {
            int integer_sum = 0;
            float mean;
            unsigned int output_base =
                rx * ADC_NUM_CHIRPS * RANGE_FFT_BINS +
                chirp * RANGE_FFT_BINS;

            /* Decode every sample once.  The original path decoded the same
             * 16-bit value again after calculating the mean. */
            for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample) {
                int value = read_adc_sample(payload, chirp, sample, rx);
                integer_sum += value;
                range_fft_input[sample] = (float)value;
            }
            mean = (float)integer_sum / (float)ADC_NUM_SAMPLES;
            for (unsigned int sample = 0; sample < ADC_NUM_SAMPLES; ++sample) {
                range_fft_input[sample] = (range_fft_input[sample] - mean) *
                    range_blackman_window[sample];
            }
            for (unsigned int sample = ADC_NUM_SAMPLES;
                 sample < RANGE_FFT_SIZE; ++sample) {
                range_fft_input[sample] = 0.0f;
            }
            fft_msa(range_fft_input, range_fft_output, range_cfg);
            for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
                radar_cube[output_base + range].r = range_fft_output[2u * range];
                radar_cube[output_base + range].i =
                    range_fft_output[2u * range + 1u];
            }
        }
    }
    replay_stage_perf.range_fft_us +=
        systick_get_time_us() - stage_start_us;
    if (pipeline_stats.adc_low_nibble_errors != 0u ||
        pipeline_stats.adc_clipped_samples > RADAR_MAX_CLIPPED_SAMPLES) {
        passed = 0;
        goto cleanup;
    }

    /* This loop matches the original doppler_fft(): BPM de-spreading and
     * Hanning are applied before each 128-point complex transform. */
    stage_start_us = systick_get_time_us();
    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
            unsigned int input_base =
                rx * ADC_NUM_CHIRPS * RANGE_FFT_BINS + range;
            for (unsigned int chirp = 0; chirp < ADC_NUM_CHIRPS; ++chirp) {
                ne10_fft_cpx_float32_t value =
                    radar_cube[input_base + chirp * RANGE_FFT_BINS];
                float weight = doppler_weight[chirp];
                doppler_fft_input[chirp].r = value.r * weight;
                doppler_fft_input[chirp].i = value.i * weight;
            }

            ne10_fft_c2c_1d_float32_mxu_ai(
                doppler_fft_output, doppler_fft_input, doppler_cfg);

            for (unsigned int doppler = 0;
                 doppler < DOPPLER_FFT_SIZE; ++doppler) {
                float real = doppler_fft_output[doppler].r;
                float imag = doppler_fft_output[doppler].i;
                unsigned int cube_index =
                    rx * DOPPLER_FFT_SIZE * RANGE_FFT_BINS +
                    doppler * RANGE_FFT_BINS + range;

                /* Range and Doppler layouts are both [rx][slow][range].
                 * Once a complete range column is in the FFT input, its old
                 * range values can safely be replaced by Doppler results. */
                radar_cube[cube_index] = doppler_fft_output[doppler];
                if (replay_validate_frame) {
                    float bin_energy = real * real + imag * imag;
                    energy[rx] += bin_energy;
                    if ((range == 0 && doppler == 0) ||
                        bin_energy > peak_energy[rx]) {
                        peak_energy[rx] = bin_energy;
                        peak_range[rx] = range;
                        peak_doppler[rx] = doppler;
                    }
                    if (range == doppler_signature_range[rx]) {
                        for (unsigned int k = 0;
                             k < DOPPLER_SELECTED_COUNT; ++k) {
                            if (doppler == doppler_selected_bins[k]) {
                                selected[rx][k][0] = real;
                                selected[rx][k][1] = imag;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Original detection_processing_v2() optimization: calculate four
     * complex magnitudes per MSA instruction instead of calling scalar
     * sqrtf() in the innermost Doppler loop.  One temporary channel buffer is
     * reused to keep the additional working set at 128 KiB. */
    for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
        unsigned int rx_base =
            rx * DOPPLER_FFT_SIZE * RANGE_FFT_BINS;
        float *magnitude_output = rx == 0u ? noncoherent : magnitude_temp;

        complex_abs_f32_simd(
            magnitude_output,
            (const float _Complex *)(const void *)&radar_cube[rx_base],
            noncoherent_count);
        if (rx != 0u) {
            for (unsigned int index = 0; index < noncoherent_count; ++index)
                noncoherent[index] += magnitude_temp[index];
        }
    }
    replay_stage_perf.doppler_fft_us +=
        systick_get_time_us() - stage_start_us;

    if (replay_validate_frame) {
        for (unsigned int doppler = 0;
             doppler < DOPPLER_FFT_SIZE; ++doppler) {
            for (unsigned int range = 0; range < RANGE_FFT_BINS; ++range) {
                float value = noncoherent[doppler * RANGE_FFT_BINS + range];
                noncoherent_sum += value;
                noncoherent_energy += value * value;
                if ((range == 0 && doppler == 0) ||
                    value > noncoherent_peak) {
                    noncoherent_peak = value;
                    noncoherent_peak_range = range;
                    noncoherent_peak_doppler = doppler;
                }
            }
        }

        for (unsigned int rx = 0; rx < ADC_NUM_RX; ++rx) {
            printf("[DFFT] RX%u peak=(r%u,d%u) energy=%.3e signature_r=%u\n",
                   rx, peak_range[rx], peak_doppler[rx], energy[rx],
                   doppler_signature_range[rx]);
            printf("[DFFT] RX%u bins:", rx);
            for (unsigned int k = 0; k < DOPPLER_SELECTED_COUNT; ++k) {
                printf(" %u:(%.3f,%.3f)", doppler_selected_bins[k],
                       selected[rx][k][0], selected[rx][k][1]);
                if (!doppler_value_matches(
                        selected[rx][k][0],
                        expected_doppler_selected[rx][k][0]) ||
                    !doppler_value_matches(
                        selected[rx][k][1],
                        expected_doppler_selected[rx][k][1])) {
                    passed = 0;
                }
            }
            printf("\n");
            if (peak_range[rx] != expected_doppler_peak_range[rx] ||
                peak_doppler[rx] != expected_doppler_peak_bin[rx] ||
                absolute_float(energy[rx] - expected_doppler_energy[rx]) >
                    expected_doppler_energy[rx] * 0.005f) {
                passed = 0;
            }
        }

    printf("[DFFT] noncoherent peak=(r%u,d%u) mag=%.3f sum=%.3e energy=%.3e\n",
           noncoherent_peak_range, noncoherent_peak_doppler,
           noncoherent_peak, noncoherent_sum, noncoherent_energy);
    if (noncoherent_peak_range != 8u || noncoherent_peak_doppler != 32u ||
        !doppler_value_matches(noncoherent_peak, 2390720.25f) ||
        absolute_float(noncoherent_sum - 139544528.0f) > 139544528.0f * 0.005f ||
        absolute_float(noncoherent_energy - 4.60791438e13f) >
            4.60791438e13f * 0.005f) {
        passed = 0;
    }

        printf("[DFFT] noncoherent selected:");
        for (unsigned int k = 0; k < NONCOHERENT_SELECTED_COUNT; ++k) {
            unsigned int index = expected_noncoherent_selected[k].doppler *
                                 RANGE_FFT_BINS +
                                 expected_noncoherent_selected[k].range;
            float actual = noncoherent[index];
            printf(" r%ud%u:%.3f", expected_noncoherent_selected[k].range,
                   expected_noncoherent_selected[k].doppler, actual);
            if (!doppler_value_matches(
                    actual, expected_noncoherent_selected[k].magnitude)) {
                passed = 0;
            }
        }
        printf("\n");
    }

    stage_start_us = systick_get_time_us();
    if (passed)
        ddma_passed = ddma_decode_and_validate(noncoherent, radar_cube);
    replay_stage_perf.detection_us +=
        systick_get_time_us() - stage_start_us;

cleanup:
    /* FFT plans and persistent work buffers are reused on every frame. */

    if (passed)
        printf("[DFFT] BPM/Hanning/128-point Doppler FFT validation passed\n");
    else
        printf("[DFFT] BPM/Hanning/128-point Doppler FFT validation FAILED\n");
    return passed && ddma_passed;
}

#if REPLAY_PRODUCTION_MODE
#undef printf
#endif

int radar_pipeline_init(void)
{
    if (pipeline_initialized) return 0;
    if (radar_4tx4rx_profile_validate() < 0) return -1;
    generate_blackman_window(range_blackman_window, ADC_NUM_SAMPLES);
    generate_doppler_hanning();
    radar_tracking_reset();
    radar_warning_reset();
    last_track_count = 0u;
    memset(&last_warning, 0, sizeof(last_warning));
    pipeline_initialized = 1;
    return 0;
}

int radar_pipeline_process(const unsigned char *payload, size_t length,
                           uint32_t frame_id)
{
    return radar_pipeline_process_timed(payload, length, frame_id, systick_get_time_us());
}

int radar_pipeline_process_timed(const unsigned char *payload, size_t length,
                                uint32_t frame_id, uint64_t timestamp_us)
{
    uint64_t start = systick_get_time_us();
    enum radar_frame_error error = RADAR_FRAME_OK;
    replay_output_frame_id = frame_id;
    replay_output_detection_count = 0;
    last_output_count = 0;
    memset(&pipeline_stats, 0, sizeof(pipeline_stats));
    if (radar_diagnostics_begin(frame_id, timestamp_us) < 0) {
        struct radar_diagnostic_snapshot snapshot;
        radar_diagnostics_get(&snapshot, NULL);
        error = (enum radar_frame_error)snapshot.error;
    } else if (!payload || length != ADC_PAYLOAD_BYTES) error = RADAR_FRAME_LENGTH;
    else if (radar_pipeline_init() < 0) error = RADAR_FRAME_CONFIG;
    else if (!bpm_doppler_fft_and_validate(payload)) {
        error = pipeline_stats.adc_low_nibble_errors ||
            pipeline_stats.adc_clipped_samples > RADAR_MAX_CLIPPED_SAMPLES ?
            RADAR_FRAME_ADC : RADAR_FRAME_PROCESSING;
    }
    radar_diagnostics_end(error, systick_get_time_us() - start);
    if (error != RADAR_FRAME_OK) {
        /* Clear stale host detections on a malformed or failed frame. */
        motorcycle_output_publish(frame_id, NULL, 0);
        return -1;
    }
    return 0;
}

void radar_pipeline_get_stats(struct radar_pipeline_stats *stats)
{
    if (stats) *stats = pipeline_stats;
}

unsigned int radar_pipeline_copy_detections(struct motorcycle_detection *out,
                                             unsigned int capacity)
{
    unsigned int n = last_output_count < capacity ? last_output_count : capacity;
    if (!out) return 0;
    memcpy(out, last_output, n * sizeof(*out));
    return n;
}

float radar_range_bin_m(void) { return firmware_range_resolution(); }
float radar_velocity_bin_mps(void) { return firmware_doppler_resolution(); }

static void adc_live_task(void *arg)
{
    uint64_t report_total_us = 0;
    uint64_t report_max_us = 0;
    unsigned int report_count = 0;
    unsigned int total_frames = 0;
    unsigned int attempted_frames = 0;
    unsigned int overruns = 0;
    unsigned int capture_errors = 0;
#if RADAR_SELFTEST_INPUT
    uint64_t generation_report_us = 0;
    unsigned char *synthetic_payload = NULL;
    const struct radar_synthetic_target target = {
        50.25f, 25.0f / radar_velocity_bin_mps(), 15.0f, 100.0f,
        {1.0f, 0.9f, 0.8f, 0.7f}, {0.0f, 0.0f, 0.0f, 0.0f}
    };
#endif
    extern char *heap_ptr;

    (void)arg;

    printf("[BPM-DDMA] guarded 4TX4RX pipeline started\n");
#if RADAR_EXPERIMENTAL_LIVE
    printf("[EXPERIMENTAL] REAL ADC with UNVERIFIED 4TX decoding; outputs are provisional\n");
#endif
    printf("[CAN] codec selfcheck=%s; transport UNBOUND, no CAN frames sent\n",
           rcan_codec_selfcheck() == RCAN_OK ? "PASS" : "FAILED");
    radar_4tx4rx_profile_log();
    if (radar_4tx4rx_profile_validate() < 0) {
        printf("[DDMA4] software profile validation FAILED\n");
        return;
    }
#if RADAR_SELFTEST_INPUT
    printf("[SIM] SYNTHETIC ADC ONLY - NOT real radar measurements\n");
    synthetic_payload = malloc(RADAR_SYNTHETIC_BYTES);
    if (!synthetic_payload || radar_synthetic_frame(synthetic_payload,
            RADAR_SYNTHETIC_BYTES, &target, 1, 2.0f, 12345u, 0) < 0) {
        printf("[SIM] frame generation FAILED\n");
        return;
    }
    printf("[SIM] truth: range=20.085 m velocity=+25.000 m/s angle=+15 deg (ideal ULA)\n");
    printf("[SIM] sequence=%u: 150-frame loop, moving/empty/crossing/empty; no tracking\n",
           RADAR_SELFTEST_SEQUENCE);
#else
    if (radar_frontend_init() < 0) {
        printf("[LIVE] radar front-end initialization FAILED\n");
        return;
    }
#endif

#if !RADAR_SELFTEST_INPUT && RADAR_CAPTURE_ONLY
    {
        unsigned int captured = 0u;
        unsigned int capture_errors_only = 0u;
        int usb_result = motorcycle_output_init();

        if (RADAR_CAPTURE_MAX_FRAMES == 0u)
            printf("[ADC-CAPTURE] USB init=%s; TLV type=13 payload=%u bytes target=continuous\n",
                   usb_result == 0 ? "ok" : "FAILED", RADAR_PAYLOAD_BYTES);
        else
            printf("[ADC-CAPTURE] USB init=%s; TLV type=13 payload=%u bytes target=%u frames\n",
                   usb_result == 0 ? "ok" : "FAILED", RADAR_PAYLOAD_BYTES,
                   RADAR_CAPTURE_MAX_FRAMES);
        printf("[ADC-CAPTURE] RAW ADC ONLY - range/velocity/angle output disabled\n");
        if (usb_result != 0)
            return;
        printf("[ADC-CAPTURE] waiting for USB host connection\n");
        while (!motorcycle_output_is_connected())
            msleep(100);
        printf("[ADC-CAPTURE] USB connected; capture starts in 5 seconds\n");
        msleep(5000);
        while (RADAR_CAPTURE_MAX_FRAMES == 0u ||
               captured < RADAR_CAPTURE_MAX_FRAMES) {
            const unsigned char *payload = radar_frontend_wait_frame();
            int send_result;

            if (!payload) {
                ++capture_errors_only;
                msleep(10);
                continue;
            }
            send_result = motorcycle_output_publish_adc(captured + 1u, payload,
                                                        RADAR_PAYLOAD_BYTES);
            radar_frontend_release_frame(payload);
            if (send_result == 0) {
                ++captured;
                if (captured == 1u || captured % 10u == 0u) {
                    if (RADAR_CAPTURE_MAX_FRAMES == 0u)
                        printf("[ADC-CAPTURE] sent=%u continuous errors=%u\n",
                               captured, capture_errors_only);
                    else
                        printf("[ADC-CAPTURE] sent=%u/%u errors=%u\n", captured,
                               RADAR_CAPTURE_MAX_FRAMES, capture_errors_only);
                }
            }
            else if (send_result < 0) {
                ++capture_errors_only;
                printf("[ADC-CAPTURE] USB send error=%d count=%u\n",
                       send_result, capture_errors_only);
                msleep(10);
            }
            else {
                msleep(10);
            }
        }
        if (RADAR_CAPTURE_MAX_FRAMES != 0u) {
            printf("[ADC-CAPTURE] COMPLETE frames=%u errors=%u; RF remains running, no algorithm output\n",
                   captured, capture_errors_only);
            while (1)
                msleep(1000);
        }
    }
#endif

    if (radar_pipeline_init() < 0) return;
    printf("[BPM-DDMA] payload=%u range_bin=%.9f m velocity_bin=%.9f m/s\n",
           ADC_PAYLOAD_BYTES, radar_range_bin_m(), radar_velocity_bin_mps());
    printf("[BPM-DDMA] pipeline=4TX4RX RFFT/BPM-descramble/DFFT/DDMA/CFAR/AoA\n");
    printf("[BPM-DDMA] in-place range/Doppler cube=%u bytes; resolver state=%u bytes\n",
           (unsigned int)(ADC_NUM_RX * ADC_NUM_CHIRPS *
                          RANGE_FFT_BINS *
                          sizeof(ne10_fft_cpx_float32_t)),
           (unsigned int)sizeof(ddma_results));
    printf("[BPM-DDMA] expected period=%llu us, max detections=%u\n",
           (unsigned long long)REPLAY_FRAME_PERIOD_US,
           MOTORCYCLE_OUTPUT_MAX_DETECTIONS);
    {
        int usb_result = motorcycle_output_init();
        printf("[HOST] MotorCycle protocol init=%s, max_detections=%u\n",
               usb_result == 0 ? "ok" : "FAILED",
               MOTORCYCLE_OUTPUT_MAX_DETECTIONS);
    }

    while (1) {
#if RADAR_SELFTEST_INPUT
        uint64_t cycle_start = systick_get_time_us();
#if RADAR_SELFTEST_SEQUENCE
        struct radar_synthetic_target scene[RADAR_SYNTHETIC_MAX_TARGETS];
        unsigned int scene_count = radar_synthetic_scene(attempted_frames, scene);
        if (radar_synthetic_frame(synthetic_payload, RADAR_SYNTHETIC_BYTES,
                scene, scene_count, 2.0f, 12345u + attempted_frames, 0) < 0) {
            printf("[SIM] sequence generation FAILED\n");
            return;
        }
        generation_report_us += systick_get_time_us() - cycle_start;
#endif
        const unsigned char *payload = synthetic_payload;
#else
        const unsigned char *payload = radar_frontend_wait_frame();
#endif
        uint64_t frame_start;
        uint64_t elapsed_us;

        if (payload == NULL) {
            ++capture_errors;
            msleep(10);
            continue;
        }

        frame_start = systick_get_time_us();
        if (radar_pipeline_process(payload, ADC_PAYLOAD_BYTES, ++attempted_frames) < 0) {
            if (attempted_frames == 1u || attempted_frames % REPLAY_REPORT_FRAMES == 0u) {
                printf("[LIVE] pipeline FAILED at frame %u\n", replay_output_frame_id);
                radar_diagnostics_print();
            }
#if !RADAR_SELFTEST_INPUT
            radar_frontend_release_frame(payload);
#endif
            msleep(10);
            continue;
        }
#if !RADAR_SELFTEST_INPUT
        radar_frontend_release_frame(payload);
#endif

        if (total_frames == 0u) {
            printf("[HOST] first frame detections=%u tracks=%u\n",
                   replay_output_detection_count, last_track_count);
#if RADAR_SELFTEST_INPUT
            int matched = 0;
            for (unsigned int i = 0; i < last_output_count; ++i) {
                const struct motorcycle_detection *d = &last_output[i];
                if (fabsf(d->range_m - target.range_bin * radar_range_bin_m()) < 0.15f &&
                    fabsf(d->velocity_mps - 25.0f) < 0.36f &&
                    fabsf(d->azimuth_deg - 15.0f) < 1.0f) {
                    matched = 1;
                    printf("[SIM] full ADC pipeline check PASS r=%.4f v=%.4f a=%.4f\n",
                           d->range_m, d->velocity_mps, d->azimuth_deg);
                }
            }
            if (!matched) {
                printf("[SIM] full ADC pipeline check FAILED; stopped\n");
                motorcycle_output_publish(replay_output_frame_id, NULL, 0);
                return;
            }
#endif
        }

        elapsed_us = systick_get_time_us() - frame_start;
        ++total_frames;
        ++report_count;
        report_total_us += elapsed_us;
        if (elapsed_us > report_max_us)
            report_max_us = elapsed_us;
        if (elapsed_us > REPLAY_FRAME_PERIOD_US)
            ++overruns;

        if (report_count == REPLAY_REPORT_FRAMES) {
            radar_diagnostics_print();
#if RADAR_SELFTEST_INPUT
            printf("[SIM-TIMING] generation_avg_us=%llu (excluded from algorithm PERF; included in selftest cycle pacing)\n",
                (unsigned long long)(generation_report_us / report_count));
            generation_report_us = 0;
#endif
            struct motorcycle_output_stats usb_stats;
            size_t heap_used = (size_t)((uintptr_t)heap_ptr -
                                      (uintptr_t)&_user_heap_start);
            size_t heap_free = (size_t)((uintptr_t)&_user_heap_end -
                                      (uintptr_t)heap_ptr);
            printf("[PERF] frames=%u avg=%llu us max=%llu us "
                   "overruns=%u capture_errors=%u heap_used=%u heap_free=%u\n",
                   total_frames,
                   (unsigned long long)(report_total_us / report_count),
                   (unsigned long long)report_max_us, overruns, capture_errors,
                   (unsigned int)heap_used, (unsigned int)heap_free);
            printf("[STAGE] avg_rfft=%llu us avg_dfft=%llu us "
                   "avg_detect=%llu us avg_other=%llu us\n",
                   (unsigned long long)(replay_stage_perf.range_fft_us /
                                        report_count),
                   (unsigned long long)(replay_stage_perf.doppler_fft_us /
                                        report_count),
                   (unsigned long long)(replay_stage_perf.detection_us /
                                        report_count),
                   (unsigned long long)((report_total_us -
                       replay_stage_perf.range_fft_us -
                       replay_stage_perf.doppler_fft_us -
                       replay_stage_perf.detection_us) / report_count));
            motorcycle_output_get_stats(&usb_stats);
            printf("[DDMA] last_frame resolved_cells=%u ambiguous_cells=%u contaminated_cells=%u rejected_peaks=%u overflow=%u adc_errors=%u clipped=%u mask=0x%02x\n",
                pipeline_stats.status_cells[DDMA_RESOLVED],
                pipeline_stats.status_cells[DDMA_AMBIGUOUS],
                pipeline_stats.status_cells[DDMA_CONTAMINATED],
                pipeline_stats.rejected_peaks, pipeline_stats.detection_overflow,
                pipeline_stats.adc_low_nibble_errors,
                pipeline_stats.adc_clipped_samples,
                pipeline_stats.last_rejected_candidates);
            printf("[HOST] connected=%u produced=%u sent=%u dropped=%u errors=%u last_error=%d consecutive=%u\n",
                   usb_stats.connected, usb_stats.produced, usb_stats.sent,
                   usb_stats.dropped, usb_stats.errors,
                   usb_stats.last_error, usb_stats.consecutive_errors);
            {
                struct radar_tracking_stats track_stats;
                radar_tracking_get_stats(&track_stats);
                printf("[TRACK] current=%u created=%u confirmed=%u deleted=%u associations=%u warnings=%u%u%u%u%u%u%u ttc=%.2f\n",
                       last_track_count, track_stats.created,
                       track_stats.confirmed, track_stats.deleted,
                       track_stats.associations,
                       last_warning.active[0], last_warning.active[1],
                       last_warning.active[2], last_warning.active[3],
                       last_warning.active[4], last_warning.active[5],
                       last_warning.active[6], last_warning.ttc_min_s);
            }
            report_count = 0;
            report_total_us = 0;
            report_max_us = 0;
            memset(&replay_stage_perf, 0, sizeof(replay_stage_perf));
        }
#if RADAR_SELFTEST_INPUT
        uint64_t cycle_us = systick_get_time_us() - cycle_start;
        if (cycle_us < REPLAY_FRAME_PERIOD_US) {
            unsigned int remaining_ms =
                (unsigned int)((REPLAY_FRAME_PERIOD_US - cycle_us + 999u) / 1000u);
            msleep(remaining_ms);
        }
#endif
    }
}

void vendor_init(void *arg)
{
    (void)arg;

    printf("[KALE] vendor init (common BPM + guarded 4TX DDMA v2)\n");

    if (thread_create("adc_live", 8192,
                      adc_live_task, NULL) == NULL) {
        printf("[LIVE] failed to create task\n");
    }
}
