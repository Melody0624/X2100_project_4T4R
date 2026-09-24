# X2100 4TX4RX DDMA optimized live build

This directory is an independent optimized derivative of
`migration/adc_live_4tx4rx_ddma`.  The 2026-08-27 source and artifact are not
overwritten.

## Optimizations

- Reuse one 4 MiB complex cube in place for Range FFT and Doppler FFT output.
- Decode each packed ADC sample once instead of twice.
- Generate Hanning/BPM weights once during task initialization.
- Write RX0 magnitudes directly to the noncoherent map, avoiding one clear and
  one full-map accumulation.
- Fuse DDMA selection and RD-map generation, remove two diagnostic-only 32 KiB
  buffers, and replace inner helper calls with a checked 8x4 lookup table.
- Skip the per-frame 96 KiB CFAR clear in production; every observable active
  cell is overwritten before use.
- Propagate Cheetah SPI ACK failures instead of silently continuing.
- Add compile-time checks for packet, chirp, FFT, TX and DDMA dimensions.
- Remove the unused MAC/lwIP stack, symbol index and interactive Shell from
  this dedicated USB production configuration.  UART console logging remains
  enabled; use the unoptimized 2026-08-27 build when an interactive CLI is
  required for board diagnosis.
- Disable unrelated display conversion, audio, PWM backlight, DTRNG and EFUSE
  drivers; radar capture, USB, UART, camera, SPI, I2C and GPIO stay enabled.

The CFAR thresholds, FFT sizes, loop order within each transform, output
protocol, provisional virtual-array mapping and calibration are unchanged.

## Build

From WSL:

```bash
cd /mnt/d/downloads/X2100_project-main/migration/adc_live_4tx4rx_ddma_opt
sed -i 's/\r$//' install_and_build_4tx4rx_ddma_opt.sh
chmod +x install_and_build_4tx4rx_ddma_opt.sh
./install_and_build_4tx4rx_ddma_opt.sh
```

Artifacts are written to
`artifacts/live_adc_4tx4rx_ddma_opt_20260830`.

## Hardware limitation

This remains a software 4TX4RX DDMA candidate.  The Cheetah RF register table
is still the legacy 2TX placeholder.  Do not claim real 4TX operation until a
vendor-confirmed 4TX register table, chirp phase schedule, antenna coordinates
and channel calibration are installed and validated on the radar front end.
