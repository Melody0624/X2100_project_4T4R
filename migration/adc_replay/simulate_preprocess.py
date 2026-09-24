from pathlib import Path
import ctypes
import struct


def f32(value):
    return ctypes.c_float(value).value


project = Path("/home/melody/Manhattan_Project/freertos")
payload = (project / "vendor/testdata/adc_frame_0001.dat").read_bytes()[36:]
window_bytes = Path(
    "/mnt/d/downloads/X2100_project-main/migration/adc_replay/blackman_506_f32.bin"
).read_bytes()
window = struct.unpack("<506f", window_bytes)

num_samples = 506
num_chirps = 128
num_rx = 4
chirp_bytes = 4080
header_bytes = 32
selected = (0, 1, 2, 63, 126, 252, 253, 254, 379, 503, 504, 505)

abs_sum = [f32(0) for _ in range(num_rx)]
energy = [f32(0) for _ in range(num_rx)]

for chirp in range(num_chirps):
    base = chirp * chirp_bytes + header_bytes
    for rx in range(num_rx):
        values = []
        integer_sum = 0
        for sample in range(num_samples):
            offset = base + (sample * num_rx + rx) * 2
            raw = payload[offset] | payload[offset + 1] << 8
            value = (raw >> 4) - 2048
            values.append(value)
            integer_sum += value
        mean = f32(f32(integer_sum) / f32(num_samples))
        if chirp == 0:
            print(f"RX{rx}_CHIRP0_SUM={integer_sum} MEAN={mean:.9g}")
        for sample, value in enumerate(values):
            dc = f32(f32(value) - mean)
            result = f32(dc * window[sample])
            abs_sum[rx] = f32(abs_sum[rx] + f32(abs(result)))
            energy[rx] = f32(energy[rx] + f32(result * result))
            if chirp == 0 and sample in selected:
                print(
                    f"RX{rx} IDX{sample + 1} DC={dc:.9g} WIN={result:.9g}"
                )

for rx in range(num_rx):
    print(f"RX{rx}_FRAME_ABS_SUM={abs_sum[rx]:.9g} ENERGY={energy[rx]:.9g}")
