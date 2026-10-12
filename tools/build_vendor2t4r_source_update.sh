#!/usr/bin/env bash
set -euo pipefail
project=/mnt/d/downloads/X2100_project-main
source_sdk="$project/X2100_project-main/firmware/x2100/freertos"
new_sdk="$project/MotorCycle_Ingenic/MotorCycle_Ingenic/freertos"
output="$project/artifacts/vendor2t4r_source_update_20261010/firmware"
build_sdk=/home/melody/Manhattan_Project/freertos_vendor2t4r_source_update_20261010
compiler="$project/MotorCycle_Ingenic/MotorCycle_Ingenic/tools/toolchains/mips-xburst2-newlib430/bin"
mkdir -p "$output"

# Isolate the one new CRT/cache object. No closed detection/CFAR/AoA/post objects.
"$compiler/mips-sde-elf-ar" p "$new_sdk/vendor/motor_cycle_demo/mmwlib/libmmw_detection.a" velocity_disambiguation.o >"$output/velocity_disambiguation.o"
"$compiler/mips-sde-elf-ar" rcs "$source_sdk/vendor/motor_cycle_demo/mmwlib/libmmw_velocity.a" "$output/velocity_disambiguation.o"
"$compiler/mips-sde-elf-nm" -A --defined-only "$source_sdk/vendor/motor_cycle_demo/mmwlib/libmmw_velocity.a" >"$output/velocity_symbols.txt"
if [[ -e "$build_sdk" ]]; then
    [[ -f "$build_sdk/.vendor2t4r-source-update" ]] || exit 1
else
    mkdir -p "$build_sdk"
    touch "$build_sdk/.vendor2t4r-source-update"
fi
# Vendor bundles contain read-only SPL files; permit refreshing this marked copy.
find "$build_sdk" -type f -exec chmod u+w {} +
cp -a "$source_sdk/." "$build_sdk/"
find "$build_sdk" -type f -exec chmod u+w {} +
python3 - "$build_sdk" "$compiler" <<'PY'
from pathlib import Path
import sys
root, compiler = Path(sys.argv[1]), sys.argv[2]
for path in root.rglob('*'):
    if path.is_file() and not path.is_symlink() and (path.suffix in {'.c','.h','.S','.mk','.sh','.py','.in','.lds'} or path.name == 'Makefile' or path.name.endswith('_defconfig')):
        raw = path.read_bytes()
        if b'\0' not in raw[:4096]:
            path.write_bytes(raw.replace(b'\r', b''))
path = root / '.config.in'
text = path.read_text()
text = '\n'.join('RTOS_TOOLCHAIN_PATH=' + compiler if line.startswith('RTOS_TOOLCHAIN_PATH=') else line for line in text.splitlines()) + '\n'
path.write_text(text)
PY
cd "$build_sdk"
export PATH="$compiler:$PATH"
chmod +x tools/configparser tools/menuconfig
if [[ ${CLEAN_BUILD:-1} == 1 ]]; then
    make clean >"$output/clean.log" 2>&1
fi
make -j8 V=1 >"$output/build.log" 2>&1 || { grep -E 'error:|Error|undefined reference|multiple definition' "$output/build.log" | tail -30; exit 1; }
cp zero.bin "$output/zero_ota.bin"
cp zero.elf "$output/zero.elf"
cp .config.in "$output/build.config"
spl=x2100l-spl-sfc-nor-ota-1200M-500M.bin
cp "xburst2/soc-x2000/spl/$spl" "$output/$spl"
"$compiler/mips-sde-elf-nm" -A --print-size zero.elf >"$output/linked_symbols.txt"
"$compiler/mips-sde-elf-size" -A zero.elf >"$output/elf_sections.txt"
python3 "$project/tools/verify_vendor2t4r_2M_ota.py" "$output/zero_ota.bin" --image-only
python3 "$project/tools/package_vendor2t4r_source_update.py" "$project" "$source_sdk" "$build_sdk" "$output" "$compiler"
python3 "$project/tools/verify_vendor2t4r_2M_ota.py" "$output" >"$output/verification.txt"
cat "$output/verification.txt"
echo "Build completed: $output"
