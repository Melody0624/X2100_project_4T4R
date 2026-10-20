#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
project=$(cd "$(dirname "$0")/.." && pwd)
sdk="${SOURCE_SDK:-$project/X2100_project-main/firmware/x2100/freertos}"
vendor="$sdk/vendor"
output="${TEST_OUTPUT:-$project/artifacts/vendor2t4r_source_update_20261010/tests}"
mkdir -p "$output"
theory_flags=()
default_four=1
if [[ ${TEST_TWO_TX:-0} == 1 ]]; then default_four=0; fi
if [[ ${TEST_THEORY:-$default_four} == 1 ]]; then theory_flags=(-DEXPECT_THEORY); fi
if [[ ${TEST_FOUR_TX:-$default_four} == 1 ]]; then theory_flags+=(-DEXPECT_FOUR_TX); fi
if [[ ${TEST_TWO_TX:-0} == 1 ]]; then theory_flags+=(-DBOARD_ANTENNA_CONFIG=BOARD_ANTENNA_2T4R); fi
gcc -std=gnu99 -O1 -g -msse3 -fno-pie -no-pie -fsanitize=address,undefined -DHEAP_MALLOC_H \
    "${theory_flags[@]}" \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$project/tools/tests/host_stubs" -I"$vendor/motor_cycle_demo/inc" -I"$vendor/ne10_fft_lib/inc" -I"$vendor/heap_malloc" -I"$vendor/InitPhaseCalib" -I"$vendor" \
    "$project/tools/tests/test_vendor2t4r_source_pipeline.c" \
    "$vendor/motor_cycle_demo/src/detection/"{detection_processing,cfar_detection,cfar_func_v2,detection_post_processing}.c \
    "${ANGLE_SOURCE:-$vendor/motor_cycle_demo/src/detection/angle_estimation_v1.c}" \
    "$vendor/motor_cycle_demo/src/general_functions.c" \
    "$vendor/motor_cycle_demo/src/set_params.c" \
    "$vendor/motor_cycle_demo/src/memory_pool.c" \
    "$vendor/motor_cycle_demo/src/board_calib_config.c" \
    "$vendor/motor_cycle_demo/src/rcs_estimation.c" \
    "$vendor/InitPhaseCalib/InitPhaseCalib.c" \
    "$vendor/InitPhaseCalib/zero_phase_correction.c" \
    "$vendor/InitPhaseCalib/mapping_gen_processing.c" \
    "$vendor/motor_cycle_demo/src/complex_abs_f32.c" -lm -o "$output/test_pipeline"
ASAN_OPTIONS=detect_leaks=0 "$output/test_pipeline" | tee "$output/pipeline.log"
