#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
project=$(cd "$(dirname "$0")/.." && pwd)
sdk="$project/X2100_project-main/firmware/x2100/freertos"
output="${TEST_OUTPUT:-$project/artifacts/source4t4r_adaptation_20261012/config}"
mkdir -p "$output"
flags=(-DEXPECT_FOUR_TX)
if [[ ${TEST_TWO_TX:-0} == 1 ]]; then flags=(-DBOARD_ANTENNA_CONFIG=BOARD_ANTENNA_2T4R); fi
gcc -std=gnu99 -g -fno-pie -no-pie -fsanitize=address,undefined -DHEAP_MALLOC_H \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast "${flags[@]}" \
    -I"$project/tools/tests/config_stubs" -I"$sdk/vendor/motor_cycle_demo/inc" \
    -I"$sdk/vendor/ne10_fft_lib/inc" -I"$sdk/vendor/mmw_msg_pkt" -I"$sdk/vendor/heap_malloc" \
    "$project/tools/tests/test_config_layout.c" -o "$output/test_config"
"$output/test_config" >"$output/config.log" 2>&1
tail -1 "$output/config.log"
