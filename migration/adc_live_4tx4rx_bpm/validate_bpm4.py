#!/usr/bin/env python3
"""Host-side algebra check for the candidate 4TX Walsh-BPM profile."""

WALSH = (
    (1, 1, 1, 1),
    (1, -1, 1, -1),
    (1, 1, -1, -1),
    (1, -1, -1, 1),
)


def main():
    for a in range(4):
        for b in range(4):
            dot = sum(WALSH[slot][a] * WALSH[slot][b]
                      for slot in range(4))
            assert dot == (4 if a == b else 0)

    maximum_error = 0.0
    for block in range(32):
        source = tuple(
            complex((tx + 1) * (block + 1), (tx - 1.5) * (block + 0.25))
            for tx in range(4)
        )
        encoded = []
        for slot in range(4):
            chirp = block * 4 + slot
            common = -1 if ((chirp * 17 + 3) & 1) else 1
            encoded.append(common * sum(
                WALSH[slot][tx] * source[tx] for tx in range(4)
            ))

        recovered = []
        for tx in range(4):
            value = 0j
            for slot in range(4):
                chirp = block * 4 + slot
                common = -1 if ((chirp * 17 + 3) & 1) else 1
                value += encoded[slot] * common * WALSH[slot][tx] / 4.0
            recovered.append(value)

        for expected, actual in zip(source, recovered):
            maximum_error = max(maximum_error, abs(expected - actual))

    assert maximum_error < 1.0e-12
    print("BPM4_MATRIX_ORTHOGONAL=PASS")
    print("BPM4_SYNTHETIC_DEMODULATION=PASS")
    print(f"MAXIMUM_ERROR={maximum_error:.3e}")


if __name__ == "__main__":
    main()
