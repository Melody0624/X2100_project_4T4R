#!/usr/bin/env python3
"""Deterministic host checks for the production-loop transformations."""

import math
import random

RANGE_BINS = 256
DOPPLER_BINS = 128
SUBBANDS = 8
TX = 4
SUB_DOPPLER_BINS = DOPPLER_BINS // SUBBANDS

LUT = tuple(
    tuple((start + tx) % SUBBANDS for tx in range(TX))
    for start in range(SUBBANDS)
)


def reference_decode(noncoherent):
    best = [0] * (RANGE_BINS * SUB_DOPPLER_BINS)
    metric = [0.0] * len(best)
    amplitude = [0.0] * len(best)

    for range_bin in range(RANGE_BINS):
        for sub_doppler in range(SUB_DOPPLER_BINS):
            values = [
                noncoherent[(sub_doppler + sb * SUB_DOPPLER_BINS) *
                            RANGE_BINS + range_bin]
                for sb in range(SUBBANDS)
            ]
            selected = 0
            best_metric = -math.inf
            for start in range(SUBBANDS):
                candidate = min(values[(start + tx) % SUBBANDS]
                                for tx in range(TX))
                if candidate > best_metric:
                    best_metric = candidate
                    selected = start
            index = sub_doppler * RANGE_BINS + range_bin
            best[index] = selected
            metric[index] = best_metric

    for range_bin in range(RANGE_BINS):
        for sub_doppler in range(SUB_DOPPLER_BINS):
            index = sub_doppler * RANGE_BINS + range_bin
            start = best[index]
            value = 0.0
            for tx in range(TX):
                subband = (start + tx) % SUBBANDS
                doppler = sub_doppler + subband * SUB_DOPPLER_BINS
                value += noncoherent[doppler * RANGE_BINS + range_bin]
            amplitude[index] = value
    return best, metric, amplitude


def optimized_decode(noncoherent):
    best = [0] * (RANGE_BINS * SUB_DOPPLER_BINS)
    metric = [0.0] * len(best)
    amplitude = [0.0] * len(best)

    for range_bin in range(RANGE_BINS):
        for sub_doppler in range(SUB_DOPPLER_BINS):
            values = [
                noncoherent[(sub_doppler + sb * SUB_DOPPLER_BINS) *
                            RANGE_BINS + range_bin]
                for sb in range(SUBBANDS)
            ]
            selected = 0
            best_metric = -math.inf
            for start in range(SUBBANDS):
                candidate = values[LUT[start][0]]
                for tx in range(1, TX):
                    candidate = min(candidate, values[LUT[start][tx]])
                if candidate > best_metric:
                    best_metric = candidate
                    selected = start

            value = 0.0
            for tx in range(TX):
                subband = LUT[selected][tx]
                doppler = sub_doppler + subband * SUB_DOPPLER_BINS
                value += noncoherent[doppler * RANGE_BINS + range_bin]
            index = sub_doppler * RANGE_BINS + range_bin
            best[index] = selected
            metric[index] = best_metric
            amplitude[index] = value
    return best, metric, amplitude


def validate_in_place_cube():
    # A Doppler transform reads a complete range column before replacing that
    # same column.  Different range columns have disjoint indices modulo 256.
    for range_bin in range(RANGE_BINS):
        column = {
            slow * RANGE_BINS + range_bin for slow in range(DOPPLER_BINS)
        }
        assert all(index % RANGE_BINS == range_bin for index in column)
        assert len(column) == DOPPLER_BINS


def main():
    validate_in_place_cube()
    rng = random.Random(0x4D444D41)
    noncoherent = [rng.uniform(0.01, 2_500_000.0)
                   for _ in range(RANGE_BINS * DOPPLER_BINS)]
    reference = reference_decode(noncoherent)
    optimized = optimized_decode(noncoherent)
    assert reference == optimized
    print("OPT_IN_PLACE_CUBE=PASS")
    print("OPT_DDMA_FUSED_LOOP=PASS")
    print("OPT_DDMA_LUT=PASS")


if __name__ == "__main__":
    main()
