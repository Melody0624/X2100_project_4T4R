#!/usr/bin/env python3
"""Algebraic 4TX DDMA subband and performance check."""

import cmath

FFT_SIZE = 128
SUBBANDS = 8
BINS_PER_SUBBAND = 16
TX_OFFSETS = (0, 1, 2, 3)
CHIRP_PERIOD = 26.0e-6
CENTER_FREQUENCY = 76.5e9
LIGHT_SPEED = 299792458.0


def dft(sequence):
    return [
        sum(value * cmath.exp(-2j * cmath.pi * k * n / FFT_SIZE)
            for n, value in enumerate(sequence))
        for k in range(FFT_SIZE)
    ]


def main():
    # Exercise cyclic wrap: the four TX peaks occupy subbands 6, 7, 0, 1.
    anchor = 6
    sub_doppler = 5
    expected_bins = tuple(
        sub_doppler + ((anchor + offset) % SUBBANDS) * BINS_PER_SUBBAND
        for offset in TX_OFFSETS
    )
    amplitudes = (1.0, 0.9, 0.8, 0.7)

    received = []
    for chirp in range(FFT_SIZE):
        common = -1 if ((chirp * 17 + 3) & 1) else 1
        value = sum(
            amplitudes[tx] * cmath.exp(
                2j * cmath.pi * expected_bins[tx] * chirp / FFT_SIZE)
            for tx in range(4)
        )
        received.append(common * value)

    # Firmware removes the common legacy +/-1 BPM sequence before the FFT.
    descrambled = [
        value * (-1 if ((chirp * 17 + 3) & 1) else 1)
        for chirp, value in enumerate(received)
    ]
    spectrum = dft(descrambled)
    magnitudes = [abs(value) for value in spectrum]
    actual_bins = tuple(sorted(
        range(FFT_SIZE), key=lambda k: magnitudes[k], reverse=True
    )[:4])
    assert set(actual_bins) == set(expected_bins)

    subband_values = [
        magnitudes[sub_doppler + band * BINS_PER_SUBBAND]
        for band in range(SUBBANDS)
    ]
    metrics = [
        min(subband_values[(start + tx) % SUBBANDS] for tx in range(4))
        for start in range(SUBBANDS)
    ]
    selected_anchor = max(range(SUBBANDS), key=lambda index: metrics[index])
    assert selected_anchor == anchor

    wavelength = LIGHT_SPEED / CENTER_FREQUENCY
    velocity_resolution = wavelength / (2 * CHIRP_PERIOD * FFT_SIZE)
    maximum_velocity = wavelength / (4 * CHIRP_PERIOD)
    assert velocity_resolution <= 1.0
    assert maximum_velocity >= 25.0

    # One subband means 16 FFT bins, hence a 45-degree phase step per chirp.
    phase_steps = tuple(offset * 360 / SUBBANDS for offset in TX_OFFSETS)
    assert phase_steps == (0.0, 45.0, 90.0, 135.0)

    print("DDMA4_SYNTHETIC_SUBBAND_SEPARATION=PASS")
    print(f"EXPECTED_TX_BINS={expected_bins}")
    print(f"SELECTED_ANCHOR={selected_anchor}")
    print(f"VELOCITY_RESOLUTION_MPS={velocity_resolution:.6f}")
    print(f"MAX_UNAMBIGUOUS_VELOCITY_MPS={maximum_velocity:.6f}")


if __name__ == "__main__":
    main()
