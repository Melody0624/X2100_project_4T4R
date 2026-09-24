#ifndef RADAR_CONFIG_H
#define RADAR_CONFIG_H

/* Single build-time software profile. Hardware register tables are a separate,
 * verified deliverable. Changing this file does NOT program the RF chip. */
#define RADAR_CONFIG_ID "bpm-ddma-4x4-128-8band-v3"
#define RADAR_NUM_TX 4u
#define RADAR_NUM_RX 4u
#define RADAR_NUM_VIRTUAL_ANTS (RADAR_NUM_TX * RADAR_NUM_RX)
#define RADAR_ADC_SAMPLES 506u
#define RADAR_CHIRPS 128u
#define RADAR_RANGE_FFT_SIZE 512u
#define RADAR_DOPPLER_FFT_SIZE RADAR_CHIRPS
#define RADAR_CHIRP_HEADER_BYTES 32u
#define RADAR_SAMPLE_BYTES 2u
#define RADAR_CHIRP_DATA_BYTES (RADAR_ADC_SAMPLES * RADAR_NUM_RX * RADAR_SAMPLE_BYTES)
#define RADAR_CHIRP_BYTES (RADAR_CHIRP_HEADER_BYTES + RADAR_CHIRP_DATA_BYTES)
#define RADAR_PAYLOAD_BYTES (RADAR_CHIRPS * RADAR_CHIRP_BYTES)
#define RADAR_ADC_SAMPLE_RATE_HZ 26.665e6f
#define RADAR_CHIRP_SLOPE_HZ_PER_SECOND 19.531e12f
#define RADAR_CENTER_FREQUENCY_HZ 76.5e9f
#define RADAR_CHIRP_PERIOD_SECONDS 26.0e-6f
#define RADAR_FRAME_PERIOD_US 50328ull
#define RADAR_DDMA_NUM_SUBBANDS 8u
#define RADAR_DDMA_BINS_PER_SUBBAND (RADAR_DOPPLER_FFT_SIZE / RADAR_DDMA_NUM_SUBBANDS)
#define RADAR_TX_SUBBAND_OFFSETS {0u, 1u, 2u, 3u}
#define RADAR_PHASE_WORD_MODULUS 64u
#define RADAR_PHASE_WORD_180_DEG (RADAR_PHASE_WORD_MODULUS / 2u)
#define RADAR_PHASE_WORD_PER_SUBBAND (RADAR_PHASE_WORD_MODULUS / RADAR_DDMA_NUM_SUBBANDS)
/* Supplier-confirmed on 2026-09-23: the hardware supports at most 512 BPM
 * codes, starts each frame at code 0, and resets the code index every frame.
 * bpm_code_512.mat matches bpm_code.h entry-for-entry. */
#define RADAR_BPM_LENGTH 512u
#define RADAR_BPM_START 0u
#define RADAR_BPM_RESET_EACH_FRAME 1u
/* CDC ACM line coding reported to the PC. USB payload transport is bulk and
 * is not rate-limited to this UART-style number. */
#define RADAR_USB_CDC_LINE_CODING_BPS 460800u
/* The v2 512-byte USB sender has been verified to sustain capture, so publish
 * every processed frame to MotorCycle Tools (about 19.87 Hz). */
#define RADAR_HOST_OUTPUT_DECIMATION 1u
#define RADAR_FRONTEND_4TX_PROFILE_READY 0
#define RADAR_ARRAY_CALIBRATION_READY 0
/* User-confirmed on 2026-09-21: cheetah_128_512_config applies to
 * MT-4T4R-01 REV-A and all four TX channels are enabled.  This permits only
 * the raw-capture build; processing remains blocked by the two flags above. */
#define RADAR_CAPTURE_FRONTEND_READY 1
#ifndef RADAR_SELFTEST_INPUT
#define RADAR_SELFTEST_INPUT 1
#endif
#ifndef RADAR_CAPTURE_ONLY
#define RADAR_CAPTURE_ONLY 0
#endif
#ifndef RADAR_EXPERIMENTAL_LIVE
#define RADAR_EXPERIMENTAL_LIVE 0
#endif
#if RADAR_CAPTURE_ONLY && RADAR_EXPERIMENTAL_LIVE
#error Raw capture and experimental live processing are separate firmware modes
#endif
/* Zero means continuous raw capture.  The PC recorder chooses how many
 * complete TLV frames to retain and may close the CDC port at any time. */
#define RADAR_CAPTURE_MAX_FRAMES 0u
#define RADAR_LEGACY_POST_FILTERS 0
#define RADAR_MAX_CLIPPED_SAMPLES (RADAR_ADC_SAMPLES * RADAR_CHIRPS * RADAR_NUM_RX / 100u)
#define RADAR_MAX_DETECTIONS 16u
#define RADAR_REPORT_FRAMES 100u
#define RADAR_DDMA_MIN_AMPLITUDE 1.0e-9f
#define RADAR_DDMA_MIN_TX_TO_PEAK 0.04f
#define RADAR_DDMA_WINNER_RATIO 1.5f
#define RADAR_DDMA_EMPTY_CONTRAST 2.0f
/* 0: off, 1: frame/stream summary, 2: per-RX and strongest DDMA cells.
 * Full mode is for bring-up: measure its added cost on the target. */
#define RADAR_DIAGNOSTICS_DEFAULT 1u
#define RADAR_DIAGNOSTIC_CELLS 4u
#ifndef RADAR_SELFTEST_SEQUENCE
#define RADAR_SELFTEST_SEQUENCE 1
#endif

/* These restrictions expose actual implementation limits instead of allowing
 * a plausible-looking profile that silently corrupts buffer interpretation. */
#if RADAR_NUM_TX != 4 || RADAR_NUM_RX != 4 || RADAR_ADC_SAMPLES != 506 || RADAR_CHIRPS != 128
#error This transport/algorithm revision supports only 4TX4RX and 506x128 ADC
#endif
#if RADAR_RANGE_FFT_SIZE != 512 || RADAR_DDMA_NUM_SUBBANDS != 8
#error This FFT/CFAR/resolver revision requires 512 range FFT and 8 DDMA bands
#endif
#endif
