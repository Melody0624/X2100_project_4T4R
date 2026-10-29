#!/usr/bin/env bash
set -euo pipefail
project=$(cd "$(dirname "$0")/.." && pwd)
sdk=/home/melody/Manhattan_Project/freertos_b2t4r_adc_rcs_20261029
out="$project/artifacts/b_2t4r_adc_rcs_20261029"
compiler="$project/MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin"
mkdir -p "$out/tests" "$out/mips"
python3 "$project/tools/prepare_b2t4r_adc_rcs_20261029.py"
vendor="$sdk/vendor"
gcc -std=gnu99 -Wall -Wextra -Werror -g -fno-pie -no-pie -fsanitize=address,undefined \
    -I"$project/tools/tests/host_stubs" -I"$vendor/motor_cycle_demo/inc" -I"$vendor/ne10_fft_lib/inc" \
    "$project/tools/tests/test_rcs_estimation.c" "$vendor/motor_cycle_demo/src/rcs_estimation.c" \
    -lm -o "$out/tests/test_rcs"
"$out/tests/test_rcs" > "$out/tests/rcs.log" 2>&1
tail -1 "$out/tests/rcs.log"
gcc -std=gnu99 -O1 -g -msse3 -fno-pie -no-pie -fsanitize=address,undefined -DHEAP_MALLOC_H \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--wrap=malloc \
    -I"$project/tools/tests/host_stubs" -I"$vendor/motor_cycle_demo/inc" -I"$vendor/ne10_fft_lib/inc" \
    -I"$vendor/heap_malloc" -I"$vendor/InitPhaseCalib" -I"$vendor" \
    "$project/tools/tests/test_2t4r_adc_buffer.c" \
    "$vendor/motor_cycle_demo/src/"{general_functions,set_params,memory_pool,board_calib_config}.c \
    "$vendor/mmw_msg_pkt/mmw_msg_pkt.c" -lm -o "$out/tests/test_adc"
ASAN_OPTIONS=detect_leaks=0 "$out/tests/test_adc" > "$out/tests/adc.log" 2>&1
tail -1 "$out/tests/adc.log"
cd "$sdk"
export PATH="$compiler:$PATH"
make clean > "$out/mips/clean.log" 2>&1
make -j8 V=1 > "$out/mips/build.log" 2>&1 || {
    grep -E 'error:|Error|undefined reference|multiple definition' "$out/mips/build.log" | tail -20
    exit 1
}
cp zero.elf "$out/mips/zero.elf"
cp zero.bin "$out/mips/zero_ota.bin"
cp .config.in "$out/mips/build.config"
echo 'PASS: B ADC + RCS clean MIPS build completed'
