#!/usr/bin/env bash
set -euo pipefail
project=$(cd "$(dirname "$0")/.." && pwd)
sdk="$project/X2100_project-main/firmware/x2100/freertos"
output="${TEST_OUTPUT:-$project/artifacts/2t4r_source_calibration_fix_20261015/tests}"
mkdir -p "$output"
ulimit -c 0
gcc -std=gnu99 -Wall -Wextra -Werror \
    -I"$project/tools/tests/host_stubs" \
    -I"$sdk/vendor/motor_cycle_demo/inc" -I"$sdk/vendor/ne10_fft_lib/inc" \
    "$project/tools/tests/test_default_calibration.c" \
    "$sdk/vendor/motor_cycle_demo/src/board_calib_config.c" \
    -lm -o "$output/test_default_calibration"
"$output/test_default_calibration" | tee "$output/default_calibration.log"
