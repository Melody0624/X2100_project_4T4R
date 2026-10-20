#!/usr/bin/env bash
set -euo pipefail
project=$(cd "$(dirname "$0")/.." && pwd)
source=${SOURCE_SDK:-$project/X2100_project-main/firmware/x2100/freertos}
compiler=${MIPS_TOOLCHAIN_PATH:-/mnt/d/downloads/X2100_project-main/MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin}
output=${TEST_OUTPUT:-$project/artifacts/source4t4r_adaptation_20261012/abi}
mkdir -p "$output"
for mode in 2 4; do
    "$compiler/mips-sde-elf-gcc" -c -std=gnu99 -EL -mabi=32 -mfp64 -march=mips32r2 \
        -DBOARD_ANTENNA_CONFIG=BOARD_ANTENNA_${mode}T4R \
        -I"$source/vendor/motor_cycle_demo/inc" -I"$source/vendor/ne10_fft_lib/inc" \
        -I"$source/vendor/mmw_msg_pkt" "$project/tools/tests/test_mips_shared_abi.c" -o "$output/abi${mode}.o"
done
python3 - "$project" "$output" <<'PY'
from pathlib import Path
import struct,sys
project,out=map(Path,sys.argv[1:]);sys.path.insert(0,str(project/'tools'))
from verify_vendor2t4r_tx01_cal import symbol_bytes
two=symbol_bytes((out/'abi2.o').read_bytes(),'shared_abi')
four=symbol_bytes((out/'abi4.o').read_bytes(),'shared_abi')
assert two==four,'Shared MIPS struct sizes or offsets differ'
result='PASS: actual MIPS 2TX/4TX shared struct sizes and critical offsets match '+str(struct.unpack('<15I',four))
(out/'abi.log').write_text(result+'\n');print(result)
PY
