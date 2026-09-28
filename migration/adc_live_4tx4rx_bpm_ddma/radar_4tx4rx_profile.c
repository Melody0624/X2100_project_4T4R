#include <stdint.h>
#include <math.h>
#include <stddef.h>

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

/* Source: user-provided 天线相位中心坐标.docx, mm converted to m.
 * All second coordinates are zero. Document lists physical left-to-right
 * positions. User corrected the channel labels on 2026-09-28: TX1..4 and
 * RX1..4 run right-to-left (code indices TX0..3 and RX0..3 respectively).
 * The sum coarray is uniform with -1.96 mm signed spacing; no sorting is needed under
 * that assignment. Removing its common origin changes no beam power. */
static const float rx_position_m[4] = {0.00588f, 0.00392f, 0.00196f, 0.0f};
static const float tx_position_m[4] = {0.0313612f, 0.0235212f, 0.0156812f, 0.0078412f};

float radar_4tx4rx_virtual_position_m(unsigned int tx, unsigned int rx)
{
    if (tx >= RADAR_NUM_TX || rx >= RADAR_NUM_RX) return NAN;
    return tx_position_m[tx] + rx_position_m[rx];
}

float radar_4tx4rx_spacing_wavelengths(void)
{
    float spacing = radar_4tx4rx_virtual_position_m(0, 1) -
                    radar_4tx4rx_virtual_position_m(0, 0);
    return spacing * RADAR_CENTER_FREQUENCY_HZ / 299792458.0f;
}

/* Provisional DDMA allocation.  Eight total subbands leave four signal
 * subbands and four guard/empty subbands, matching the old 2TX profile's
 * signal-to-total-subband ratio.  Replace after the 4TX waveform is known. */
static const uint8_t tx_subband_offset[RADAR_NUM_TX] = RADAR_TX_SUBBAND_OFFSETS;

static const struct radar_waveform waveform = {
    RADAR_ADC_SAMPLE_RATE_HZ, RADAR_CHIRP_SLOPE_HZ_PER_SECOND,
    RADAR_CENTER_FREQUENCY_HZ, RADAR_CHIRP_PERIOD_SECONDS,
    RADAR_FRAME_PERIOD_US, RADAR_ADC_SAMPLES, RADAR_CHIRPS,
    RADAR_RANGE_FFT_SIZE, RADAR_NUM_TX, RADAR_NUM_RX, RADAR_DDMA_NUM_SUBBANDS,
    RADAR_BPM_START, RADAR_BPM_RESET_EACH_FRAME
};
const struct radar_waveform *radar_waveform_get(void) { return &waveform; }
const char *radar_waveform_error(const struct radar_waveform *w)
{
    if (!w) return "null profile";
    if (!isfinite(w->sample_rate_hz) || w->sample_rate_hz <= 0 ||
        !isfinite(w->slope_hz_per_s) || w->slope_hz_per_s <= 0 ||
        !isfinite(w->center_hz) || w->center_hz <= 0 ||
        !isfinite(w->chirp_s) || w->chirp_s <= 0) return "invalid RF scalar";
    if (w->samples != RADAR_ADC_SAMPLES || w->chirps != RADAR_CHIRPS ||
        w->range_fft != RADAR_RANGE_FFT_SIZE || w->tx != RADAR_NUM_TX ||
        w->rx != RADAR_NUM_RX || w->bands != RADAR_DDMA_NUM_SUBBANDS)
        return "unsupported layout/FFT/DDMA dimensions";
    if ((double)w->samples / w->sample_rate_hz >= w->chirp_s)
        return "ADC window does not fit chirp (RF delay also needs verification)";
    if (!w->frame_us || (double)w->chirps * w->chirp_s * 1e6 > w->frame_us)
        return "chirp burst exceeds frame period";
    if (w->bpm_start >= RADAR_BPM_LENGTH || w->bpm_reset_each_frame != 1u)
        return "unsupported BPM epoch: this revision requires frame reset";
    return NULL;
}

/* Experimental board-specific broadside candidate from
 * Record_20260928_202805_adc.dat, range bin 5, TX-major RX0..RX3.
 * Flash CAL4 values, when present, override these firmware defaults. */
#if RADAR_EXPERIMENTAL_LIVE && !RADAR_SELFTEST_INPUT
static const float firmware_calibration_real[RADAR_NUM_VIRTUAL_ANTS] = {
    1.000000f, -0.935064f, -0.697341f, 1.191529f,
    -0.091294f, -0.278997f, 0.881429f, -0.049072f,
    0.497096f, -0.784077f, 0.282913f, 0.674274f,
    0.022635f, 0.505457f, -1.150743f, -0.063695f,
};

static const float firmware_calibration_imag[RADAR_NUM_VIRTUAL_ANTS] = {
    0.000000f, 0.398385f, -0.786336f, -0.129519f,
    1.004782f, -0.996838f, -0.629665f, 1.230434f,
    0.775442f, -0.567465f, -0.929807f, 0.901324f,
    -1.420770f, 1.368615f, 0.940266f, -1.674663f,
};
#else
/* Synthetic and capture-only builds retain the identity calibration. */
static const float firmware_calibration_real[RADAR_NUM_VIRTUAL_ANTS] = {
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};
static const float firmware_calibration_imag[RADAR_NUM_VIRTUAL_ANTS] = {0};
#endif
static float calibration_real[RADAR_NUM_VIRTUAL_ANTS];
static float calibration_imag[RADAR_NUM_VIRTUAL_ANTS];
static int has_flash_calibration;
static float custom_angle_axis[128];
static int has_custom_angle_axis;

int radar_4tx4rx_profile_validate(void)
{
    uint32_t seen = 0u;
    uint32_t seen_subbands = 0u;

    if (radar_waveform_error(&waveform)) return -1;

    if (RADAR_DOPPLER_FFT_SIZE % RADAR_DDMA_NUM_SUBBANDS != 0u)
        return -1;
    if (RADAR_NUM_VIRTUAL_ANTS > 32u)
        return -1;
    if (RADAR_PHASE_WORD_PER_SUBBAND * RADAR_DDMA_NUM_SUBBANDS !=
        RADAR_PHASE_WORD_MODULUS)
        return -1;

    for (unsigned int tx = 0; tx < RADAR_NUM_TX; ++tx) {
        uint32_t subband_bit;
        /* Resolver/CFAR assume this particular contiguous allocation. */
        if (tx_subband_offset[tx] != tx)
            return -1;
        if (tx_subband_offset[tx] >= RADAR_DDMA_NUM_SUBBANDS)
            return -1;
        subband_bit = 1u << tx_subband_offset[tx];
        if ((seen_subbands & subband_bit) != 0u)
            return -1;
        seen_subbands |= subband_bit;
        for (unsigned int rx = 0; rx < RADAR_NUM_RX; ++rx) {
            unsigned int index = virtual_channel_map[tx][rx];
            uint32_t bit;

            /* FFT angle processing is valid only for this uniform coarray. */
            float expected = radar_4tx4rx_virtual_position_m(0, 0) + index *
                             (rx_position_m[1] - rx_position_m[0]);
            if (fabsf(radar_4tx4rx_virtual_position_m(tx, rx) - expected) > 1e-7f)
                return -1;

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
    float wavelength = 299792458.0f / RADAR_CENTER_FREQUENCY_HZ;
    float velocity_resolution = wavelength /
        (2.0f * RADAR_CHIRP_PERIOD_SECONDS * RADAR_DOPPLER_FFT_SIZE);
    float sampling_velocity_limit = wavelength /
        (4.0f * RADAR_CHIRP_PERIOD_SECONDS);

    printf("[CONFIG] id=%s samples=%u chirps=%u payload=%u fs=%.0f slope=%.0f center=%.0f chirp_us=%.3f frame_us=%llu bpm_start=%u reset=%u\n",
        RADAR_CONFIG_ID, RADAR_ADC_SAMPLES, RADAR_CHIRPS, RADAR_PAYLOAD_BYTES,
        RADAR_ADC_SAMPLE_RATE_HZ, RADAR_CHIRP_SLOPE_HZ_PER_SECOND,
        RADAR_CENTER_FREQUENCY_HZ, RADAR_CHIRP_PERIOD_SECONDS * 1e6f,
        RADAR_FRAME_PERIOD_US, RADAR_BPM_START, RADAR_BPM_RESET_EACH_FRAME);

    printf("[DDMA4] software candidate: tx=%u rx=%u virtual=%u\n",
           RADAR_NUM_TX, RADAR_NUM_RX, RADAR_NUM_VIRTUAL_ANTS);
    printf("[DDMA4] subbands=%u bins/subband=%u TX offsets=0,1,2,3\n",
           RADAR_DDMA_NUM_SUBBANDS, RADAR_DDMA_BINS_PER_SUBBAND);
    printf("[DDMA4] phase slopes=0,45,90,135 deg/chirp; 6-bit steps=0,8,16,24\n");
    printf("[DDMA4] BPM=verified 512-code table; frame start=0 reset=1\n");
    printf("[DDMA4] Doppler bin=%.3f m/s sampling interval=+/-%.1f m/s (NOT a validated DDMA range)\n",
           velocity_resolution, sampling_velocity_limit);
    printf("[DDMA4] common BPM + DDMA; ambiguous/contaminated peaks are rejected\n");
    printf("[ARRAY] document geometry: 16-element ULA signed d=-1.960 mm d/lambda=%.7f\n", radar_4tx4rx_spacing_wavelengths());
    printf("[ARRAY] right-to-left TX1..4/RX1..4 routing user-confirmed; live build uses compiled broadside candidate unless Flash has a nonzero matrix\n");
#if RADAR_CAPTURE_ONLY
    printf("[DDMA4] algorithm decoding disabled in raw-capture build\n");
    printf("[DDMA4] raw ADC output does not require array calibration\n");
#elif RADAR_EXPERIMENTAL_LIVE
    printf("[DDMA4] EXPERIMENTAL LIVE: assumed DDMA TX offsets; compiled amplitude/phase candidate unverified on hardware\n");
    printf("[DDMA4] detections are diagnostic only, not validated 4TX measurements\n");
#elif !RADAR_FRONTEND_4TX_PROFILE_READY
    printf("[DDMA4] BLOCKED: verified 4TX RF profile and array calibration pending\n");
    printf("[DDMA4] legacy 2TX register table will NOT be sent as a 4TX profile\n");
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
    *real = has_flash_calibration ? calibration_real[virtual_index] :
                                    firmware_calibration_real[virtual_index];
    *imag = has_flash_calibration ? calibration_imag[virtual_index] :
                                    firmware_calibration_imag[virtual_index];
}

void radar_4tx4rx_get_firmware_calibration(unsigned int virtual_index,
                                           float *real,
                                           float *imag)
{
    if (virtual_index >= RADAR_NUM_VIRTUAL_ANTS)
        virtual_index = 0u;
    *real = firmware_calibration_real[virtual_index];
    *imag = firmware_calibration_imag[virtual_index];
}

void radar_4tx4rx_reset_calibration(void)
{
    has_flash_calibration = 0;
}

void radar_4tx4rx_set_calibration(const float values[32])
{
    for (unsigned int i = 0; i < RADAR_NUM_VIRTUAL_ANTS; ++i) {
        calibration_real[i] = values[2u * i];
        calibration_imag[i] = values[2u * i + 1u];
    }
    has_flash_calibration = 1;
}

void radar_4tx4rx_get_angle_axis(float values[128])
{
    if (has_custom_angle_axis) {
        for (unsigned int i = 0; i < 128u; ++i) values[i] = custom_angle_axis[i];
        return;
    }
    for (unsigned int bin = 0; bin < 128u; ++bin) {
        int signed_bin = bin < 64u ? (int)bin : (int)bin - 128;
        float sine = (float)signed_bin /
                     (128.0f * radar_4tx4rx_spacing_wavelengths());
        if (sine > 1.0f) sine = 1.0f;
        if (sine < -1.0f) sine = -1.0f;
        values[bin] = asinf(sine) * (180.0f / 3.14159265358979323846f);
    }
}

void radar_4tx4rx_set_angle_axis(const float values[128])
{
    for (unsigned int i = 0; i < 128u; ++i) custom_angle_axis[i] = values[i];
    has_custom_angle_axis = 1;
}

int radar_4tx4rx_has_angle_axis_override(void)
{
    return has_custom_angle_axis;
}

float radar_4tx4rx_chirp_code(unsigned int chirp, float legacy_bpm_code)
{
    (void)chirp;
    /* Hardware documentation must define the real 4TX code.  Passing the
     * legacy value through preserves the known-good capture path and keeps
     * the replacement point explicit. */
    return legacy_bpm_code;
}

unsigned int radar_4tx4rx_tx_phase_word(unsigned int chirp,
                                        unsigned int tx,
                                        int legacy_bpm_sign)
{
    unsigned int common_phase = legacy_bpm_sign < 0 ?
        RADAR_PHASE_WORD_180_DEG : 0u;
    unsigned int slope;

    if (tx >= RADAR_NUM_TX)
        return common_phase;
    slope = tx_subband_offset[tx] * RADAR_PHASE_WORD_PER_SUBBAND;
    return (common_phase + chirp * slope) % RADAR_PHASE_WORD_MODULUS;
}
