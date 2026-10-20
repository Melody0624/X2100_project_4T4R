#!/usr/bin/env bash
set -euo pipefail
project=$(cd "$(dirname "$0")/.." && pwd)
sdk="$project/X2100_project-main/firmware/x2100/freertos"
vendor="$sdk/vendor"
output="${TEST_OUTPUT:-$project/artifacts/2t4r_rcs_internal_20261012/tests}"
mkdir -p "$output"
ulimit -c 0
gcc -std=gnu99 -Wall -Wextra -Werror -g -fno-pie -no-pie \
    -fsanitize=address,undefined \
    -I"$project/tools/tests/host_stubs" -I"$vendor/motor_cycle_demo/inc" \
    -I"$vendor/ne10_fft_lib/inc" \
    "$project/tools/tests/test_rcs_estimation.c" \
    "$vendor/motor_cycle_demo/src/rcs_estimation.c" -lm -o "$output/test_rcs"
"$output/test_rcs" | tee "$output/rcs.log"
